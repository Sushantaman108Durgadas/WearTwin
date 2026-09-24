#include <Arduino.h>
#include <Wire.h>
#include "InertialState.h"
#include "CardialState.h"

InertialState inertial;
CardialState cardiac(34);

void setup()
{
    Serial.begin(115200);

    Wire.begin(21, 22);

    if (!inertial.begin())
    {
        while (true)
            delay(1000);
    }

    cardiac.begin();

    Serial.println("System initialized.");
}

void loop()
{
    cardiac.update();

    float angle = inertial.update();

    static unsigned long lastPrint = 0;

    if (millis() - lastPrint >= 1000)
    {
        lastPrint = millis();

        Serial.print("Angle: ");
        Serial.print(angle);

        Serial.print(" | Bias: ");
        Serial.print(inertial.getBias());

        Serial.print(" | Avg BPM: ");
        Serial.println(cardiac.getAverageBPM());
    }
}