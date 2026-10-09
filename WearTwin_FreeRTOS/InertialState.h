#ifndef INERTIAL_STATE_H
#define INERTIAL_STATE_H

#include <Arduino.h>
#include <Wire.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <MPU6050.h>
#include "KalmanFilter.h"

class InertialState
{
private:
    MPU6050 mpu;
    KalmanFilter kalman;

    // Raw MPU6050 measurements
    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    // Timing
    unsigned long currentTime;
    unsigned long previousTime;
    float dt;
    bool firstRun;

    // FreeRTOS
    TaskHandle_t taskHandle;
    SemaphoreHandle_t stateMutex;

    // Estimated state
    float angle;
    float bias;

    // Internal methods
    void readSensor();
    void updateTime();

    float getAccelerometerAngle();
    float getGyroRate();

    static void taskEntry(void *arg);
    void taskLoop();

public:
    InertialState();

    bool begin();
    bool startTask();

    float update();

    float getAngle() const;
    float getBias() const;
    float getDt() const;
};

#endif