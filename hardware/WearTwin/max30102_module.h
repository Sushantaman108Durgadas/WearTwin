#ifndef WEARTWIN_MAX30102_MODULE_H
#define WEARTWIN_MAX30102_MODULE_H

#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "weartwin_types.h"

class MAX30102Module {
private:
    MAX30105 sensor;
    static constexpr int32_t WINDOW_SIZE = 100;
    static constexpr int32_t HOP_SIZE = 25;
    uint32_t irBuffer[WINDOW_SIZE] = {};
    uint32_t redBuffer[WINDOW_SIZE] = {};
    uint32_t nextIR[HOP_SIZE] = {};
    uint32_t nextRed[HOP_SIZE] = {};
    int32_t samplesFilled = 0;
    int32_t hopFilled = 0;
    uint32_t latestIR = 0, latestRed = 0;
    int32_t bpm = 0, spo2 = 0;
    int8_t bpmValid = 0, spo2Valid = 0;
    bool haveSample = false, windowReady = false;

    void calculate() {
        int32_t nextSpO2 = 0, nextBpm = 0;
        int8_t nextSpO2Valid = 0, nextBpmValid = 0;
        maxim_heart_rate_and_oxygen_saturation(
            irBuffer, WINDOW_SIZE, redBuffer,
            &nextSpO2, &nextSpO2Valid, &nextBpm, &nextBpmValid);
        spo2 = nextSpO2;
        spo2Valid = nextSpO2Valid;
        bpm = nextBpm;
        bpmValid = nextBpmValid;
        windowReady = true;
    }

    void process(uint32_t red, uint32_t ir) {
        if (samplesFilled < WINDOW_SIZE) {
            redBuffer[samplesFilled] = red;
            irBuffer[samplesFilled] = ir;
            ++samplesFilled;
            if (samplesFilled == WINDOW_SIZE) {
                calculate();
                hopFilled = 0;
            }
            return;
        }
        nextRed[hopFilled] = red;
        nextIR[hopFilled] = ir;
        ++hopFilled;
        if (hopFilled >= HOP_SIZE) {
            for (int32_t i = 0; i < WINDOW_SIZE - HOP_SIZE; ++i) {
                redBuffer[i] = redBuffer[i + HOP_SIZE];
                irBuffer[i] = irBuffer[i + HOP_SIZE];
            }
            for (int32_t i = 0; i < HOP_SIZE; ++i) {
                redBuffer[WINDOW_SIZE - HOP_SIZE + i] = nextRed[i];
                irBuffer[WINDOW_SIZE - HOP_SIZE + i] = nextIR[i];
            }
            hopFilled = 0;
            calculate();
        }
    }

public:
    bool begin(TwoWire& bus = Wire) {
        if (!sensor.begin(bus, I2C_SPEED_FAST)) return false;
        sensor.setup(0x1F, 4, 2, 100, 411, 4096);
        return true;
    }

    // Drain the sensor FIFO frequently. Call this before the MPU update.
    void service() {
        sensor.check();
        while (sensor.available()) {
            latestRed = sensor.getRed();
            latestIR = sensor.getIR();
            haveSample = true;
            process(latestRed, latestIR);
            sensor.nextSample();
        }
    }

    void copyTo(WearTwinReading& out) const {
        out.ir = haveSample ? latestIR : 0;
        out.red = haveSample ? latestRed : 0;
        out.spo2 = spo2;
        out.spo2Valid = windowReady && spo2Valid;
        out.heartRate = bpm;
        out.heartRateValid = windowReady && bpmValid;
    }
};

#endif
