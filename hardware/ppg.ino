
#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;

constexpr int32_t PPG_BUFFER_SIZE = 100;
constexpr int32_t PPG_HOP_SIZE = 25;

MAX30105 maxSensor;

uint32_t irBuffer[PPG_BUFFER_SIZE];
uint32_t redBuffer[PPG_BUFFER_SIZE];

uint32_t nextIR[PPG_HOP_SIZE];
uint32_t nextRed[PPG_HOP_SIZE];

int32_t samplesFilled = 0;
int32_t hopFilled = 0;

int32_t bpm = 0;
int32_t spo2 = 0;

int8_t bpmValid = 0;
int8_t spo2Valid = 0;

bool algorithmReady = false;

void calculateSpO2() {
    maxim_heart_rate_and_oxygen_saturation(
        irBuffer,
        PPG_BUFFER_SIZE,
        redBuffer,
        &spo2,
        &spo2Valid,
        &bpm,
        &bpmValid
    );

    algorithmReady = true;
}

void processSample(uint32_t red, uint32_t ir) {
    // Fill the initial 100-sample window.
    if (samplesFilled < PPG_BUFFER_SIZE) {
        redBuffer[samplesFilled] = red;
        irBuffer[samplesFilled] = ir;

        samplesFilled++;

        if (samplesFilled == PPG_BUFFER_SIZE) {
            calculateSpO2();
            hopFilled = 0;
        }

        return;
    }

    // Collect the next 25 samples.
    nextRed[hopFilled] = red;
    nextIR[hopFilled] = ir;

    hopFilled++;

    if (hopFilled >= PPG_HOP_SIZE) {
        // Shift the existing window left by 25 samples.
        for (int32_t i = 0;
             i < PPG_BUFFER_SIZE - PPG_HOP_SIZE;
             i++) {
            redBuffer[i] = redBuffer[i + PPG_HOP_SIZE];
            irBuffer[i] = irBuffer[i + PPG_HOP_SIZE];
        }

        // Append the latest 25 samples.
        for (int32_t i = 0; i < PPG_HOP_SIZE; i++) {
            redBuffer[PPG_BUFFER_SIZE - PPG_HOP_SIZE + i]
                = nextRed[i];

            irBuffer[PPG_BUFFER_SIZE - PPG_HOP_SIZE + i]
                = nextIR[i];
        }

        hopFilled = 0;

        calculateSpO2();
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    if (!maxSensor.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println("# ERROR: MAX30102 not detected");

        while (true) {
            delay(1000);
        }
    }

    // brightness, averaging, LED mode,
    // sample rate, pulse width, ADC range
    maxSensor.setup(0x1F, 4, 2, 100, 411, 4096);

    Serial.println(
        "Timestamp_ms,IR,RED,SpO2,SpO2_Valid,"
        "HR_Algorithm,HR_Valid"
    );
}

void loop() {
    maxSensor.check();

    while (maxSensor.available()) {
        uint32_t red = maxSensor.getRed();
        uint32_t ir = maxSensor.getIR();

        processSample(red, ir);

        Serial.printf(
            "%lu,%lu,%lu,%ld,%d,%ld,%d\n",
            (unsigned long)millis(),
            (unsigned long)ir,
            (unsigned long)red,
            (long)spo2,
            (int)(algorithmReady && spo2Valid),
            (long)bpm,
            (int)(algorithmReady && bpmValid)
        );

        maxSensor.nextSample();
    }
}
