#include <Arduino.h>
#include <Wire.h>

#include "InertialState.h"
#include "CardialState.h"


// =====================================================
// MPU6050
// =====================================================

InertialState inertial;


// =====================================================
// ECG
//
// Physical switches on ECG custom chip:
//
// MODE0 → low heart rate
// MODE1 → normal heart rate
// MODE2 → high heart rate
//
// ECG_OUT → ESP32 GPIO34
//
// CardialState measures the ECG signal and calculates
// BPM and RR interval from detected R-peaks.
// =====================================================

CardialState cardiac(
    34
);


void setup()
{
    Serial.begin(115200);


    // -------------------------------------------------
    // MPU6050 I2C
    // -------------------------------------------------

    Wire.begin(21, 22);


    // -------------------------------------------------
    // Inertial subsystem
    // -------------------------------------------------

    if (!inertial.begin())
    {
        while (true)
        {
            delay(1000);
        }
    }


    // -------------------------------------------------
    // Cardiac subsystem
    // -------------------------------------------------

    cardiac.begin();


    Serial.println(
        "System initialized."
    );
}


void loop()
{
    // -------------------------------------------------
    // Update ECG acquisition
    // -------------------------------------------------

    cardiac.update();


    // -------------------------------------------------
    // Update IMU
    // -------------------------------------------------

    float angle =
        inertial.update();


    // -------------------------------------------------
    // Print system information occasionally
    // -------------------------------------------------

    static unsigned long lastPrint = 0;

    if (millis() - lastPrint >= 1000)
    {
        lastPrint = millis();


        Serial.print("Angle: ");
        Serial.print(angle);


        Serial.print(" | Bias: ");
        Serial.print(
            inertial.getBias()
        );


        Serial.print(" | BPM: ");
        Serial.print(
            cardiac.getBPM()
        );


        Serial.print(" | RR: ");
        Serial.println(
            cardiac.getRRInterval()
        );
    }
}