#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "I2CBusLock.h"
#include "InertialState.h"
#include "CardialState.h"
#include "PpgState.h"

// Define the global I2C mutex declared as 'extern' in I2CBusLock.h
SemaphoreHandle_t i2cBusMutex = nullptr;

InertialState inertial;
CardialState cardiac(34);
PpgState ppg;

static const char *qualityText(PpgState::SignalQuality quality)
{
    switch (quality)
    {
        case PpgState::GOOD:     return "GOOD";
        case PpgState::DEGRADED: return "DEGRADED";
        case PpgState::POOR:     return "POOR";
        case PpgState::NO_DATA:  return "NO_DATA";
        default:                 return "WARMING_UP";
    }
}

void telemetryTask(void *parameter)
{
    (void)parameter;

    while (true)
    {
        Serial.printf(
            "Angle: %.2f | Bias: %.2f | Avg RR: %.4f | ECG BPM: %.2f\n",
            inertial.getAngle(),
            inertial.getBias(),
            cardiac.getAverageRR(),
            cardiac.getAverageBPM()
        );

        Serial.printf(
            "PPG RED: %lu | IR: %lu | PI: %.2f%% | Motion artifact score: %.1f/100 | Quality: %s | SpO2 estimate: ",
            (unsigned long)ppg.getRedRaw(),
            (unsigned long)ppg.getIrRaw(),
            ppg.getPerfusionIndex(),
            ppg.getMotionScore(),
            qualityText(ppg.getSignalQuality())
        );

        if (ppg.isEstimateValid())
            Serial.printf("%.1f%%\n", ppg.getEstimatedSpO2());
        else
            Serial.println("UNAVAILABLE (signal quality too poor)");

        Serial.println("--------------------------------------------------");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void stopOnFailure(const char *message)
{
    Serial.println(message);
    while (true) delay(1000);
}

void setup()
{
    Serial.begin(115200);
    Wire.begin(21, 22);
    Wire.setClock(100000);

    // Create shared I2C bus mutex required by PpgState and InertialState
    i2cBusMutex = xSemaphoreCreateMutex();
    if (i2cBusMutex == nullptr)
        stopOnFailure("I2C mutex creation failed.");

    if (!inertial.begin())
        stopOnFailure("IMU initialization failed.");

    if (!cardiac.begin())
        stopOnFailure("ECG initialization failed.");

    if (!ppg.begin())
        stopOnFailure("PPG initialization failed; check chip registration and I2C wiring.");

    if (!inertial.startTask())
        stopOnFailure("Failed to start IMU task.");

    if (!cardiac.startTasks())
        stopOnFailure("Failed to start ECG tasks.");

    if (!ppg.startTask())
        stopOnFailure("Failed to start PPG task.");

    if (xTaskCreatePinnedToCore(
            telemetryTask, "Telemetry", 4096, nullptr, 1, nullptr, 0) != pdPASS)
        stopOnFailure("Failed to start telemetry task.");

    Serial.println("WearTwin FreeRTOS system initialized.");
    Serial.println("PPG estimate is experimental simulation output, not a medical measurement.");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}