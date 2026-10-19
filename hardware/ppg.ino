
#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;

constexpr int32_t BUFFER_SIZE = 100;
constexpr int32_t HOP_SIZE = 25;

MAX30105 maxSensor;

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
uint32_t nextIR[HOP_SIZE];
uint32_t nextRed[HOP_SIZE];

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
        BUFFER_SIZE,
        redBuffer,
        &spo2,
        &spo2Valid,
        &bpm,
        &bpmValid
    );

    algorithmReady = true;
}

void processSample(uint32_t red, uint32_t ir) {
    if (samplesFilled < BUFFER_SIZE) {
        redBuffer[samplesFilled] = red;
        irBuffer[samplesFilled] = ir;
        samplesFilled++;

        if (samplesFilled == BUFFER_SIZE) {
            calculateSpO2();
            hopFilled = 0;
        }
        return;
    }

    nextRed[hopFilled] = red;
    nextIR[hopFilled] = ir;
    hopFilled++;

    if (hopFilled >= HOP_SIZE) {
        for (int i = 0; i < BUFFER_SIZE - HOP_SIZE; i++) {
            redBuffer[i] = redBuffer[i + HOP_SIZE];
            irBuffer[i] = irBuffer[i + HOP_SIZE];
        }

        for (int i = 0; i < HOP_SIZE; i++) {
            redBuffer[BUFFER_SIZE - HOP_SIZE + i] = nextRed[i];
            irBuffer[BUFFER_SIZE - HOP_SIZE + i] = nextIR[i];
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
        while (true) delay(1000);
    }

    // Same configuration as your combined firmware:
    // brightness, averaging, LED mode, sample rate,
    // pulse width and ADC range.
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

        // Timestamp each actual PPG sample as it is read.
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
