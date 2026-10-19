
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint32_t SAMPLE_PERIOD_US = 10000UL; // 100 Hz

Adafruit_MPU6050 mpu;

float gyroBiasX = 0, gyroBiasY = 0, gyroBiasZ = 0;
uint32_t lastSampleUs = 0;
uint32_t previousSampleUs = 0;

class KalmanFilter {
private:
    float Q_angle = 0.001f;
    float Q_bias = 0.003f;
    float R_measure = 0.03f;

    float angle = 0, bias = 0;
    float P[2][2] = {{0, 0}, {0, 0}};

public:
    void setAngle(float value) {
        angle = value;
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

KalmanFilter kalmanRoll, kalmanPitch;
float roll = 0, pitch = 0;

void calibrateGyroscope() {
    constexpr int N = 500;
    double sx = 0, sy = 0, sz = 0;

    for (int i = 0; i < N; i++) {
        sensors_event_t a, g, t;
        mpu.getEvent(&a, &g, &t);

        sx += g.gyro.x * RAD_TO_DEG;
        sy += g.gyro.y * RAD_TO_DEG;
        sz += g.gyro.z * RAD_TO_DEG;

        delay(5);
    }

    gyroBiasX = sx / N;
    gyroBiasY = sy / N;
    gyroBiasZ = sz / N;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    if (!mpu.begin(0x68, &Wire)) {
        Serial.println("# ERROR: MPU6050 not detected");
        while (true) delay(1000);
    }

    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);

    calibrateGyroscope();

    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);

    float initialRoll = atan2f(a.acceleration.y,
                               a.acceleration.z) * RAD_TO_DEG;

    float initialPitch = atan2f(
        -a.acceleration.x,
        sqrtf(a.acceleration.y * a.acceleration.y +
              a.acceleration.z * a.acceleration.z)
    ) * RAD_TO_DEG;

    kalmanRoll.setAngle(initialRoll);
    kalmanPitch.setAngle(initialPitch);

    previousSampleUs = micros();
    lastSampleUs = previousSampleUs;

    Serial.println(
        "Timestamp_ms,Accel_X,Accel_Y,Accel_Z,"
        "Gyro_X,Gyro_Y,Gyro_Z,Roll,Pitch"
    );
}

void loop() {
    uint32_t nowUs = micros();

    if ((uint32_t)(nowUs - lastSampleUs) < SAMPLE_PERIOD_US)
        return;

    lastSampleUs += SAMPLE_PERIOD_US;

    float dt = (uint32_t)(nowUs - previousSampleUs) * 1.0e-6f;
    previousSampleUs = nowUs;

    if (dt <= 0 || dt > 0.1f) return;

    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);

    float ax = a.acceleration.x;
    float ay = a.acceleration.y;
    float az = a.acceleration.z;

    float gx = g.gyro.x * RAD_TO_DEG - gyroBiasX;
    float gy = g.gyro.y * RAD_TO_DEG - gyroBiasY;
    float gz = g.gyro.z * RAD_TO_DEG - gyroBiasZ;

    float rollAcc = atan2f(ay, az) * RAD_TO_DEG;
    float pitchAcc = atan2f(-ax, sqrtf(ay * ay + az * az))
                     * RAD_TO_DEG;

    roll = kalmanRoll.update(rollAcc, gx, dt);
    pitch = kalmanPitch.update(pitchAcc, gy, dt);

    Serial.printf(
        "%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f\n",
        (unsigned long)millis(),
        ax, ay, az, gx, gy, gz, roll, pitch
    );
}
