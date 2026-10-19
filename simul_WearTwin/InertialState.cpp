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

    // Initialize microsecond timing.
    previousTime = micros();

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
    currentTime = micros();

    // Unsigned subtraction handles micros() rollover.
    uint32_t elapsedUs = currentTime - previousTime;

    dt = elapsedUs / 1000000.0f;

    previousTime = currentTime;
}

float InertialState::getAccelerometerAngle()
{
    /*
       Accelerometer angle for the existing orientation:

           angle = atan2(Ax, Az)
    */

    float angle = atan2(
        (float)ax,
        (float)az
    );

    return angle * 180.0f / PI;
}

float InertialState::getGyroRate()
{
    /*
       MPU6050 sensitivity at the default ±250 deg/s range:

           131 LSB/(deg/s)
    */

    return (float)gx / 131.0f;
}

float InertialState::update()
{
    // Read the latest sensor measurements.
    readSensor();

    // Calculate the actual elapsed time.
    updateTime();

    float accelAngle = getAccelerometerAngle();
    float gyroRate = getGyroRate();

    // Initialize the estimated angle on the first reading.
    if (firstRun)
    {
        kalman.setAngle(accelAngle);
        firstRun = false;
    }

    // Predict angle using the gyro.
    kalman.predict(
        gyroRate,
        dt
    );

    // Correct the prediction using the accelerometer.
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