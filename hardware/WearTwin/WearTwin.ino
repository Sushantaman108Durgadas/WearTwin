#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "weartwin_types.h"
#include "mpu6050_module.h"
#include "max30102_module.h"
#include "wifi_transport.h"

MPU6050Module mpuModule;
MAX30102Module ppgModule;
WiFiTransport wifiTransport;

WearTwinReading batch[BATCH_CAPACITY];
size_t batchCount = 0;
uint32_t lastBatchAttemptMs = 0;

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    if (!mpuModule.begin(Wire)) {
        Serial.println("# ERROR: MPU6050 not detected; check wiring/address 0x68");
        while (true) delay(1000);
    }
    if (!ppgModule.begin(Wire)) {
        Serial.println("# ERROR: MAX30102 not detected; check wiring/address 0x57");
        while (true) delay(1000);
    }

    wifiTransport.begin();
    Serial.println("# WearTwin acquisition started");
}

void loop() {
    // Service the optical sensor often so its FIFO does not overflow.
    ppgModule.service();
    wifiTransport.maintain();

    WearTwinReading reading{};
    if (mpuModule.update(reading)) {
        ppgModule.copyTo(reading);
        if (batchCount < BATCH_CAPACITY) {
            batch[batchCount++] = reading;
        }

        // Local serial output remains useful for debugging.
        Serial.printf("%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%lu,%lu,%ld,%d,%ld,%d\n",
            (unsigned long)reading.timestampMs,
            reading.ax, reading.ay, reading.az,
            reading.gx, reading.gy, reading.gz,
            reading.roll, reading.pitch,
            (unsigned long)reading.ir, (unsigned long)reading.red,
            (long)reading.spo2, reading.spo2Valid ? 1 : 0,
            (long)reading.heartRate, reading.heartRateValid ? 1 : 0);
    }

    // Send a batch when full. Avoid blocking on HTTP for every 100 Hz tick.
    if (batchCount >= BATCH_CAPACITY && wifiTransport.connected() &&
        (uint32_t)(millis() - lastBatchAttemptMs) >= 200UL) {
        lastBatchAttemptMs = millis();
        if (wifiTransport.sendBatch(batch, batchCount)) {
            batchCount = 0;
        }
        // On failure, keep the batch for retry. If full, don't overwrite it.
    }

    // If Wi-Fi is down the batch is held in RAM. This small demo intentionally
    // does not implement an unbounded offline queue or flash persistence.
}
