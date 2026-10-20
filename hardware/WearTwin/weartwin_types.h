#ifndef WEARTWIN_TYPES_H
#define WEARTWIN_TYPES_H

#include <Arduino.h>

struct WearTwinReading {
    uint32_t timestampMs;
    float ax, ay, az;
    float gx, gy, gz;
    float roll, pitch;
    uint32_t ir, red;
    int32_t spo2;
    bool spo2Valid;
    int32_t heartRate;
    bool heartRateValid;
};

#endif
