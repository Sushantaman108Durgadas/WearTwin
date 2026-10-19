#include <Arduino.h>
#include <Wire.h>

#include "InertialState.h"
#include "CardialState.h"

// =====================================================
// Object instances
// =====================================================

InertialState inertial;
CardialState cardiac(34);

// Latest IMU readings shared with the main loop
volatile float latestAngle = 0.0f;
volatile float latestBias = 0.0f;

// =====================================================
// FreeRTOS task declarations
// =====================================================

void ecgAcquisitionTask(void *parameter);
void imuAcquisitionTask(void *parameter);

// =====================================================
// Setup
// =====================================================

void setup()
{
    Serial.begin(115200);

    Wire.begin(21, 22);

    // Initialize IMU and Kalman filter.
    if (!inertial.begin())
    {
        Serial.println("IMU initialization failed.");

        while (true)
        {
            delay(1000);
        }
    }

    // Initialize ECG acquisition.
    cardiac.begin();

    Serial.println("WearTwin initialized.");
    Serial.println("ECG sampling: 250 Hz");
    Serial.println("IMU angle sampling: 100 Hz");

    // -------------------------------------------------
    // Task 1: ECG acquisition
    //
    // Core 0
    // cardiac.update() maintains its own 4 ms interval.
    // -------------------------------------------------

    BaseType_t ecgCreated = xTaskCreatePinnedToCore(
        ecgAcquisitionTask,
        "ECG_Acquisition",
        4096,
        nullptr,
        2,
        nullptr,
        0
    );

    // -------------------------------------------------
    // Task 2: IMU angle acquisition
    //
    // Core 1
    // One update every 10 ms = 100 Hz.
    // -------------------------------------------------

    BaseType_t imuCreated = xTaskCreatePinnedToCore(
        imuAcquisitionTask,
        "IMU_Acquisition",
        4096,
        nullptr,
        2,
        nullptr,
        1
    );

    if (ecgCreated != pdPASS || imuCreated != pdPASS)
    {
        Serial.println("ERROR: FreeRTOS task creation failed.");

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println("ECG and IMU tasks started.");
}

// =====================================================
// ECG acquisition task
// =====================================================

void ecgAcquisitionTask(void *parameter)
{
    // Wake every millisecond and let CardialState enforce
    // its existing 4 ms ECG sampling interval.
    TickType_t lastWakeTime = xTaskGetTickCount();

    const TickType_t taskPeriod = pdMS_TO_TICKS(1);

    while (true)
    {
        cardiac.update();

        vTaskDelayUntil(
            &lastWakeTime,
            taskPeriod
        );
    }
}

// =====================================================
// IMU angle acquisition task
// =====================================================

void imuAcquisitionTask(void *parameter)
{
    TickType_t lastWakeTime = xTaskGetTickCount();

    const TickType_t taskPeriod = pdMS_TO_TICKS(10);

    while (true)
    {
        // Read MPU6050, update dt, and run Kalman filter.
        latestAngle = inertial.update();
        latestBias = inertial.getBias();

        // 10 ms period = 100 Hz target rate.
        vTaskDelayUntil(
            &lastWakeTime,
            taskPeriod
        );
    }
}

// =====================================================
// Main loop: reporting only
// =====================================================

void loop()
{
    static unsigned long lastPrint = 0;

    if (millis() - lastPrint >= 1000)
    {
        lastPrint = millis();

        Serial.print("Angle: ");
        Serial.print(latestAngle, 2);

        Serial.print(" | Bias: ");
        Serial.print(latestBias, 4);

        Serial.print(" | Avg BPM: ");
        Serial.println(cardiac.getAverageBPM(), 2);
    }

    delay(1);
}