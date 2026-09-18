#ifndef INERTIAL_STATE_H
#define INERTIAL_STATE_H

#include <Arduino.h>
#include <MPU6050.h>

#include "KalmanFilter.h"


class InertialState {

private:

    MPU6050 mpu;

    int16_t ax;
    int16_t ay;
    int16_t az;

    int16_t gx;
    int16_t gy;
    int16_t gz;

    unsigned long currentTime;
    unsigned long previousTime;

    float dt;

    KalmanFilter kalman;

    bool firstRun;


public:

    InertialState();

    bool begin();

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