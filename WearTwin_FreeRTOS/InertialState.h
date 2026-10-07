#ifndef INERTIAL_STATE_H
#define INERTIAL_STATE_H

#include <Arduino.h>
#include <MPU6050.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "KalmanFilter.h"

class InertialState
{
private:
    MPU6050 mpu;

    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    unsigned long currentTime;
    unsigned long previousTime;
    float dt;

    KalmanFilter kalman;
    bool firstRun;

    TaskHandle_t taskHandle;
    SemaphoreHandle_t stateMutex;

    static void taskEntry(void *arg);
    void taskLoop();

    float angle;
    float bias;

public:
    InertialState();

    bool begin();
    bool startTask();

    // Kept for compatibility/testing. The RTOS task normally calls these.
    void readSensor();
    void updateTime();
    float getAccelerometerAngle();
    float getGyroRate();
    float update();

    float getAngle() const;
    float getBias() const;
    float getDt() const;
};

#endif
