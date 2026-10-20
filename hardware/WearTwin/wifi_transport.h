#ifndef WEARTWIN_WIFI_TRANSPORT_H
#define WEARTWIN_WIFI_TRANSPORT_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "weartwin_types.h"

class WiFiTransport {
private:
    uint32_t lastAttemptMs = 0;

public:
    void begin() {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        lastAttemptMs = millis();
        Serial.print("# Connecting to Wi-Fi");
    }

    void maintain() {
        if (WiFi.status() == WL_CONNECTED) return;
        if ((uint32_t)(millis() - lastAttemptMs) >= WIFI_RETRY_MS) {
            lastAttemptMs = millis();
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            Serial.println("# Retrying Wi-Fi connection");
        }
    }

    bool connected() const { return WiFi.status() == WL_CONNECTED; }

    // Sends a batch rather than one HTTP request per sensor sample.
    bool sendBatch(const WearTwinReading* readings, size_t count) {
        if (!connected() || readings == nullptr || count == 0) return false;

        WiFiClient client;
        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);
        if (!http.begin(client, BACKEND_BATCH_URL)) return false;
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (size_t i = 0; i < count; ++i) {
            const WearTwinReading& r = readings[i];
            JsonObject o = arr.add<JsonObject>();
            o["timestamp_ms"] = r.timestampMs;
            o["accel_x"] = r.ax; o["accel_y"] = r.ay; o["accel_z"] = r.az;
            o["gyro_x"] = r.gx; o["gyro_y"] = r.gy; o["gyro_z"] = r.gz;
            o["roll"] = r.roll; o["pitch"] = r.pitch;
            o["ir"] = r.ir; o["red"] = r.red;
            if (r.heartRateValid) o["heart_rate_bpm"] = r.heartRate;
            else o["heart_rate_bpm"].set(nullptr);
            o["heart_rate_valid"] = r.heartRateValid;
            if (r.spo2Valid) o["spo2_percent"] = r.spo2;
            else o["spo2_percent"].set(nullptr);
            o["spo2_valid"] = r.spo2Valid;
        }
        String body;
        serializeJson(doc, body);
        const int status = http.POST(body);
        http.end();
        Serial.printf("# Backend HTTP status: %d\n", status);
        return status >= 200 && status < 300;
    }
};

#endif
