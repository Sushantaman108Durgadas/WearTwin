#ifndef WEARTWIN_MPU6050_MODULE_H
#define WEARTWIN_MPU6050_MODULE_H

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "config.h"
#include "weartwin_types.h"

// Header-only module: all definitions are inline to avoid a .cpp file.
class KalmanAngleFilter {
private:
    float qAngle = 0.001f;
    float qBias = 0.003f;
    float rMeasure = 0.03f;
    float angle = 0.0f;
    float bias = 0.0f;
    float P[2][2] = {{0.0f, 0.0f}, {0.0f, 0.0f}};

public:
    void setAngle(float initialAngle) { angle = initialAngle; }

    float update(float measuredAngle, float gyroRate, float dt) {
        const float rate = gyroRate - bias;
        angle += dt * rate;

        P[0][0] += dt * (dt * P[1][1] - P[0][1] - P[1][0] + qAngle);
        P[0][1] -= dt * P[1][1];
        P[1][0] -= dt * P[1][1];
        P[1][1] += qBias * dt;

        const float innovation = measuredAngle - angle;
        const float S = P[0][0] + rMeasure;
        const float K0 = P[0][0] / S;
        const float K1 = P[1][0] / S;
        const float P00 = P[0][0];
        const float P01 = P[0][1];

        angle += K0 * innovation;
        bias += K1 * innovation;
        P[0][0] -= K0 * P00;
        P[0][1] -= K0 * P01;
        P[1][0] -= K1 * P00;
        P[1][1] -= K1 * P01;
        return angle;
    }
};

class MPU6050Module {
private:
    Adafruit_MPU6050 sensor;
    KalmanAngleFilter rollFilter, pitchFilter;
    float biasX = 0, biasY = 0, biasZ = 0;
    float roll = 0, pitch = 0;
    uint32_t lastSampleUs = 0, previousSampleUs = 0;
    bool ready = false;

    bool readEvents(sensors_event_t& accel, sensors_event_t& gyro, sensors_event_t& temp) {
        sensor.getEvent(&accel, &gyro, &temp);
        return true;
    }

    void calibrateGyroscope() {
        constexpr int SAMPLE_COUNT = 300;
        double sx = 0, sy = 0, sz = 0;
        Serial.println("# MPU6050 gyro calibration: keep the sensor still");
        for (int i = 0; i < SAMPLE_COUNT; ++i) {
            sensors_event_t a, g, t;
            readEvents(a, g, t);
            sx += g.gyro.x * RAD_TO_DEG;
            sy += g.gyro.y * RAD_TO_DEG;
            sz += g.gyro.z * RAD_TO_DEG;
            delay(5);
        }
        biasX = sx / SAMPLE_COUNT;
        biasY = sy / SAMPLE_COUNT;
        biasZ = sz / SAMPLE_COUNT;
        Serial.println("# MPU6050 gyro calibration complete");
    }

public:
    bool begin(TwoWire& bus = Wire) {
        if (!sensor.begin(0x68, &bus)) return false;
        sensor.setAccelerometerRange(MPU6050_RANGE_4_G);
        sensor.setGyroRange(MPU6050_RANGE_500_DEG);
        sensor.setFilterBandwidth(MPU6050_BAND_44_HZ);
        delay(250);
        calibrateGyroscope();

        sensors_event_t a, g, t;
        readEvents(a, g, t);
        const float initialRoll = atan2f(a.acceleration.y, a.acceleration.z) * RAD_TO_DEG;
        const float initialPitch = atan2f(-a.acceleration.x,
            sqrtf(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * RAD_TO_DEG;
        rollFilter.setAngle(initialRoll);
        pitchFilter.setAngle(initialPitch);
        roll = initialRoll;
        pitch = initialPitch;
        previousSampleUs = micros();
        lastSampleUs = previousSampleUs;
        ready = true;
        return true;
    }

    bool update(WearTwinReading& out) {
        if (!ready) return false;
        const uint32_t nowUs = micros();
        if ((uint32_t)(nowUs - lastSampleUs) < MPU_PERIOD_US) return false;
        lastSampleUs += MPU_PERIOD_US;
        const float dt = (uint32_t)(nowUs - previousSampleUs) * 1.0e-6f;
        previousSampleUs = nowUs;
        if (dt <= 0.0f || dt > 0.1f) return false;

        sensors_event_t a, g, t;
        readEvents(a, g, t);
        out.ax = a.acceleration.x;
        out.ay = a.acceleration.y;
        out.az = a.acceleration.z;
        out.gx = g.gyro.x * RAD_TO_DEG - biasX;
        out.gy = g.gyro.y * RAD_TO_DEG - biasY;
        out.gz = g.gyro.z * RAD_TO_DEG - biasZ;

        const float rollAcc = atan2f(out.ay, out.az) * RAD_TO_DEG;
        const float pitchAcc = atan2f(-out.ax, sqrtf(out.ay * out.ay + out.az * out.az)) * RAD_TO_DEG;
        roll = rollFilter.update(rollAcc, out.gx, dt);
        pitch = pitchFilter.update(pitchAcc, out.gy, dt);
        out.roll = roll;
        out.pitch = pitch;
        out.timestampMs = millis();
        return true;
    }
};

#endif
