#ifndef I2C_BUS_LOCK_H
#define I2C_BUS_LOCK_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Shared by the MPU6050 and MAX30102-style simulator.
// Both sensors communicate over the ESP32 Wire I2C bus.
extern SemaphoreHandle_t i2cBusMutex;

#endif // I2C_BUS_LOCK_H