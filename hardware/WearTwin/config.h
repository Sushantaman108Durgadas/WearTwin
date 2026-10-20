#ifndef WEARTWIN_CONFIG_H
#define WEARTWIN_CONFIG_H

#include <Arduino.h>

// Fill these in for your own Wi-Fi and computer running the backend.
static const char* WIFI_SSID = "motorola edge 50 fusion";
static const char* WIFI_PASSWORD = "192837465";

// Use your computer's LAN IPv4 address, NOT localhost/127.0.0.1.
// Example: http://192.168.1.10:8000/api/readings/batch
static const char* BACKEND_BATCH_URL = "http://192.168.1.10:8000/api/readings/batch";

constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint32_t MPU_PERIOD_US = 10000UL; // 100 Hz target
constexpr uint8_t BATCH_CAPACITY = 20;       // ~200 ms at 100 Hz
constexpr uint32_t WIFI_RETRY_MS = 10000UL;
constexpr uint32_t HTTP_TIMEOUT_MS = 1200UL;

#endif
