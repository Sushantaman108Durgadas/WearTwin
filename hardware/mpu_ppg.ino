#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#include "MAX30105.h"
#include "spo2_algorithm.h"

// ============================================================
// CONFIGURATION
// ============================================================

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;

constexpr uint32_t MPU_PERIOD_US = 10000UL; // Target: 100 Hz

Adafruit_MPU6050 mpu;
MAX30105 maxSensor;

// ============================================================
// MPU6050 STATE
// ============================================================

float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;

uint32_t lastMPUSampleUs = 0;
uint32_t previousMPUSampleUs = 0;

// Latest raw MPU readings
float ax = 0.0f, ay = 0.0f, az = 0.0f;
float gx = 0.0f, gy = 0.0f, gz = 0.0f;

// ============================================================
// KALMAN FILTER
// ============================================================

class KalmanFilter {
private:
    float Q_angle = 0.001f;
    float Q_bias = 0.003f;
    float R_measure = 0.03f;

    float angle = 0.0f;
    float bias = 0.0f;

    float P[2][2] = {
        {0.0f, 0.0f},
        {0.0f, 0.0f}
    };

public:
    void setAngle(float initialAngle) {
        angle = initialAngle;
    }

    float update(float measuredAngle, float gyroRate, float dt) {
        float rate = gyroRate - bias;
        angle += dt * rate;

        P[0][0] += dt *
            (dt * P[1][1] - P[0][1] - P[1][0] + Q_angle);

        P[0][1] -= dt * P[1][1];
        P[1][0] -= dt * P[1][1];
        P[1][1] += Q_bias * dt;

        float innovation = measuredAngle - angle;
        float S = P[0][0] + R_measure;

        float K0 = P[0][0] / S;
        float K1 = P[1][0] / S;

        float P00 = P[0][0];
        float P01 = P[0][1];

        angle += K0 * innovation;
        bias += K1 * innovation;

        P[0][0] -= K0 * P00;
        P[0][1] -= K0 * P01;
        P[1][0] -= K1 * P00;
        P[1][1] -= K1 * P01;

        return angle;
    }
};

KalmanFilter kalmanRoll;
KalmanFilter kalmanPitch;

float filteredRoll = 0.0f;
float filteredPitch = 0.0f;

// ============================================================
// MAX30102 STATE
// ============================================================

uint32_t latestIR = 0;
uint32_t latestRed = 0;

bool havePPG = false;

// ============================================================
// SpO2 ALGORITHM BUFFERS
// ============================================================

constexpr int32_t SPO2_BUFFER_SIZE = 100;
constexpr int32_t SPO2_HOP_SIZE = 25;

uint32_t irBuffer[SPO2_BUFFER_SIZE];
uint32_t redBuffer[SPO2_BUFFER_SIZE];

uint32_t nextIR[SPO2_HOP_SIZE];
uint32_t nextRed[SPO2_HOP_SIZE];

int32_t spo2SamplesFilled = 0;
int32_t hopSamplesFilled = 0;

int32_t algorithmBPM = 0;
int32_t algorithmSpO2 = 0;

int8_t algorithmHRValid = 0;
int8_t algorithmSpO2Valid = 0;

bool spo2WindowReady = false;

// ============================================================
// SpO2 CALCULATION
// ============================================================

void calculateSpO2() {
    int32_t calculatedSpO2 = 0;
    int32_t calculatedHR = 0;

    int8_t calculatedSpO2Valid = 0;
    int8_t calculatedHRValid = 0;

    maxim_heart_rate_and_oxygen_saturation(
        irBuffer,
        SPO2_BUFFER_SIZE,
        redBuffer,
        &calculatedSpO2,
        &calculatedSpO2Valid,
        &calculatedHR,
        &calculatedHRValid
    );

    algorithmSpO2 = calculatedSpO2;
    algorithmSpO2Valid = calculatedSpO2Valid;

    algorithmBPM = calculatedHR;
    algorithmHRValid = calculatedHRValid;

    spo2WindowReady = true;
}

void processSpO2Sample(uint32_t redValue, uint32_t irValue) {
    // Fill the initial 100-sample window.
    if (spo2SamplesFilled < SPO2_BUFFER_SIZE) {
        redBuffer[spo2SamplesFilled] = redValue;
        irBuffer[spo2SamplesFilled] = irValue;

        spo2SamplesFilled++;

        if (spo2SamplesFilled == SPO2_BUFFER_SIZE) {
            calculateSpO2();
            hopSamplesFilled = 0;
        }

        return;
    }

    // Collect the next 25 samples before shifting the window.
    nextRed[hopSamplesFilled] = redValue;
    nextIR[hopSamplesFilled] = irValue;

    hopSamplesFilled++;

    if (hopSamplesFilled >= SPO2_HOP_SIZE) {
        // Shift the previous window left by 25 samples.
        for (int32_t i = 0;
             i < SPO2_BUFFER_SIZE - SPO2_HOP_SIZE;
             i++) {
            redBuffer[i] = redBuffer[i + SPO2_HOP_SIZE];
            irBuffer[i] = irBuffer[i + SPO2_HOP_SIZE];
        }

        // Append the new 25 samples.
        for (int32_t i = 0; i < SPO2_HOP_SIZE; i++) {
            redBuffer[SPO2_BUFFER_SIZE - SPO2_HOP_SIZE + i]
                = nextRed[i];

            irBuffer[SPO2_BUFFER_SIZE - SPO2_HOP_SIZE + i]
                = nextIR[i];
        }

        hopSamplesFilled = 0;
        calculateSpO2();
    }
}

// ============================================================
// GYROSCOPE CALIBRATION
// ============================================================

void calibrateGyroscope() {
    constexpr int SAMPLE_COUNT = 500;

    double sumX = 0.0;
    double sumY = 0.0;
    double sumZ = 0.0;

    Serial.println("# Calibrating gyroscope: keep sensor still");

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        sensors_event_t accel;
        sensors_event_t gyro;
        sensors_event_t temp;

        mpu.getEvent(&accel, &gyro, &temp);

        sumX += gyro.gyro.x * RAD_TO_DEG;
        sumY += gyro.gyro.y * RAD_TO_DEG;
        sumZ += gyro.gyro.z * RAD_TO_DEG;

        delay(5);
    }

    gyroBiasX = sumX / SAMPLE_COUNT;
    gyroBiasY = sumY / SAMPLE_COUNT;
    gyroBiasZ = sumZ / SAMPLE_COUNT;

    Serial.println("# Gyroscope calibration complete");
}

// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    // Initialize MPU6050.
    if (!mpu.begin(0x68, &Wire)) {
        Serial.println("# ERROR: MPU6050 not detected");

        while (true) {
            delay(1000);
        }
    }

    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);

    delay(300);
    calibrateGyroscope();

    // Initialize Kalman angles from the accelerometer.
    sensors_event_t accel;
    sensors_event_t gyro;
    sensors_event_t temp;

    mpu.getEvent(&accel, &gyro, &temp);

    ax = accel.acceleration.x;
    ay = accel.acceleration.y;
    az = accel.acceleration.z;

    float initialRoll = atan2f(ay, az) * RAD_TO_DEG;

    float initialPitch =
        atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;

    kalmanRoll.setAngle(initialRoll);
    kalmanPitch.setAngle(initialPitch);

    filteredRoll = initialRoll;
    filteredPitch = initialPitch;

    // Initialize MAX30102.
    if (!maxSensor.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println("# ERROR: MAX30102 not detected");

        while (true) {
            delay(1000);
        }
    }

    // Brightness, averaging, LED mode, sample rate,
    // pulse width, ADC range.
    maxSensor.setup(0x1F, 4, 2, 100, 411, 4096);

    previousMPUSampleUs = micros();
    lastMPUSampleUs = previousMPUSampleUs;

    // CSV header: 15 columns.
    Serial.println(
        "Timestamp_ms,"
        "Accel_X,Accel_Y,Accel_Z,"
        "Gyro_X,Gyro_Y,Gyro_Z,"
        "Roll,Pitch,"
        "IR,RED,"
        "SpO2,SpO2_Valid,"
        "HR_Algorithm,HR_Valid"
    );
}

// ============================================================
// READ ALL AVAILABLE PPG SAMPLES
// ============================================================

void serviceMAX30102() {
    maxSensor.check();

    while (maxSensor.available()) {
        latestRed = maxSensor.getRed();
        latestIR = maxSensor.getIR();

        havePPG = true;

        // Use the MAXIM SpO2/heart-rate algorithm.
        // No separate custom BPM detector.
        processSpO2Sample(latestRed, latestIR);

        maxSensor.nextSample();
    }
}

// ============================================================
// MPU6050 UPDATE AND CSV OUTPUT
// ============================================================

void serviceMPU6050() {
    uint32_t nowUs = micros();

    if ((uint32_t)(nowUs - lastMPUSampleUs) < MPU_PERIOD_US) {
        return;
    }

    lastMPUSampleUs += MPU_PERIOD_US;

    float dt =
        (uint32_t)(nowUs - previousMPUSampleUs) * 1.0e-6f;

    previousMPUSampleUs = nowUs;

    if (dt <= 0.0f || dt > 0.1f) {
        return;
    }

    sensors_event_t accel;
    sensors_event_t gyro;
    sensors_event_t temp;

    mpu.getEvent(&accel, &gyro, &temp);

    ax = accel.acceleration.x;
    ay = accel.acceleration.y;
    az = accel.acceleration.z;

    gx = gyro.gyro.x * RAD_TO_DEG - gyroBiasX;
    gy = gyro.gyro.y * RAD_TO_DEG - gyroBiasY;
    gz = gyro.gyro.z * RAD_TO_DEG - gyroBiasZ;

    float rollAcc = atan2f(ay, az) * RAD_TO_DEG;

    float pitchAcc =
        atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;

    filteredRoll = kalmanRoll.update(rollAcc, gx, dt);
    filteredPitch = kalmanPitch.update(pitchAcc, gy, dt);

    // Timestamp is milliseconds since ESP32 startup.
    // Each row has the same 15 columns as the CSV header.
    Serial.printf(
        "%lu,"
        "%.3f,%.3f,%.3f,"
        "%.3f,%.3f,%.3f,"
        "%.2f,%.2f,"
        "%lu,%lu,"
        "%ld,%d,"
        "%ld,%d\n",
        (unsigned long)millis(),
        ax, ay, az,
        gx, gy, gz,
        filteredRoll, filteredPitch,
        (unsigned long)(havePPG ? latestIR : 0),
        (unsigned long)(havePPG ? latestRed : 0),
        (long)algorithmSpO2,
        (int)(spo2WindowReady && algorithmSpO2Valid),
        (long)algorithmBPM,
        (int)(spo2WindowReady && algorithmHRValid)
    );
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
    serviceMAX30102();
    serviceMPU6050();
}