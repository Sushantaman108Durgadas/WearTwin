#include "InertialState.h"

#include <Wire.h>
#include <math.h>


InertialState::InertialState()
{
    ax = ay = az = 0;
    gx = gy = gz = 0;

    currentTime = 0;
    previousTime = 0;

    dt = 0.0f;

    firstRun = true;
}


bool InertialState::begin()
{
    mpu.initialize();

    Serial.println("Establishing MPU6050 connection...");

    if (!mpu.testConnection())
    {
        Serial.println("MPU6050 connection failed!");

        return false;
    }

    Serial.println("MPU6050 connected!");

    previousTime = millis();

    return true;
}


void InertialState::readSensor()
{
    mpu.getMotion6(
        &ax,
        &ay,
        &az,
        &gx,
        &gy,
        &gz
    );
}


void InertialState::updateTime()
{
    currentTime = millis();

    dt =
        (currentTime - previousTime)
        / 1000.0f;

    previousTime = currentTime;
}


float InertialState::getAccelerometerAngle()
{
    /*
       Depending on your physical orientation,
       you may use a different equation.

       For this configuration:

           angle = atan2(Ax, Az)
    */

    float angle =
        atan2(
            (float)ax,
            (float)az
        );

    return angle * 180.0f / PI;
}


float InertialState::getGyroRate()
{
    /*
       MPU6050 default:

       ±250 deg/s

       Sensitivity:
       131 LSB/(deg/s)
    */

    return (float)gx / 131.0f;
}


float InertialState::update()
{
    readSensor();

    updateTime();

    float accelAngle =
        getAccelerometerAngle();

    float gyroRate =
        getGyroRate();


    // First measurement gives us
    // an initial angle.
    if (firstRun)
    {
        kalman.setAngle(accelAngle);

        firstRun = false;
    }


    kalman.predict(
        gyroRate,
        dt
    );


    return kalman.update(
        accelAngle
    );
}


float InertialState::getAngle() const
{
    return kalman.getAngle();
}


float InertialState::getBias() const
{
    return kalman.getBias();
}


float InertialState::getDt() const
{
    return dt;
}