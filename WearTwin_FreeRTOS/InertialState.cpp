#include "InertialState.h"
#include <Wire.h>
#include <math.h>

InertialState::InertialState()
    : ax(0), ay(0), az(0), gx(0), gy(0), gz(0),
      currentTime(0), previousTime(0), dt(0.0f),
      firstRun(true), taskHandle(nullptr), stateMutex(nullptr),
      angle(0.0f), bias(0.0f)
{
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

    stateMutex = xSemaphoreCreateMutex();
    if (!stateMutex)
    {
        Serial.println("InertialState: mutex creation failed.");
        return false;
    }

    previousTime = millis();
    return true;
}

bool InertialState::startTask()
{
    BaseType_t ok = xTaskCreatePinnedToCore(
        taskEntry,
        "IMU_Task",
        4096,
        this,
        2,
        &taskHandle,
        1);

    if (ok != pdPASS)
    {
        Serial.println("InertialState: task creation failed.");
        return false;
    }

    return true;
}

void InertialState::taskEntry(void *arg)
{
    static_cast<InertialState *>(arg)->taskLoop();
    vTaskDelete(nullptr);
}

void InertialState::taskLoop()
{
    TickType_t lastWake = xTaskGetTickCount();

    while (true)
    {
        // 100 Hz IMU update. This is independent of ECG acquisition.
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
        update();
    }
}

void InertialState::readSensor()
{
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
}

void InertialState::updateTime()
{
    currentTime = millis();
    dt = (currentTime - previousTime) / 1000.0f;
    previousTime = currentTime;

    // Guard against a pathological scheduling gap.
    if (dt <= 0.0f || dt > 0.2f)
        dt = 0.01f;
}

float InertialState::getAccelerometerAngle()
{
    float a = atan2((float)ax, (float)az);
    return a * 180.0f / PI;
}

float InertialState::getGyroRate()
{
    return (float)gx / 131.0f;
}

float InertialState::update()
{
    readSensor();
    updateTime();

    float accelAngle = getAccelerometerAngle();
    float gyroRate = getGyroRate();

    if (firstRun)
    {
        kalman.setAngle(accelAngle);
        firstRun = false;
    }

    kalman.predict(gyroRate, dt);
    float newAngle = kalman.update(accelAngle);

    if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE)
    {
        angle = newAngle;
        bias = kalman.getBias();
        xSemaphoreGive(stateMutex);
    }

    return newAngle;
}

float InertialState::getAngle() const { return angle; }
float InertialState::getBias() const { return bias; }
float InertialState::getDt() const { return dt; }
