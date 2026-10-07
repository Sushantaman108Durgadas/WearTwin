#include <Arduino.h>
#include <Wire.h>

#include "InertialState.h"
#include "CardialState.h"


/* ============================================================
 * Sensor Objects
 * ============================================================ */

InertialState inertial;

/*
 * ECG input:
 * GPIO34 is connected to the ECG simulator output.
 */
CardialState cardiac(34);


/* ============================================================
 * Setup
 * ============================================================ */

void setup()
{
    Serial.begin(115200);

    /*
     * ESP32 I2C
     *
     * SDA = GPIO21
     * SCL = GPIO22
     */
    Wire.begin(21, 22);


    /* --------------------------------------------------------
     * Initialize IMU
     * -------------------------------------------------------- */

    if (!inertial.begin())
    {
        Serial.println(
            "IMU initialization failed."
        );

        /*
         * Stop here if IMU initialization fails.
         */
        while (true)
        {
            delay(1000);
        }
    }


    /* --------------------------------------------------------
     * Initialize ECG
     * -------------------------------------------------------- */

    if (!cardiac.begin())
    {
        Serial.println(
            "ECG initialization failed."
        );

        while (true)
        {
            delay(1000);
        }
    }


    /*
     * Start ECG FreeRTOS tasks.
     *
     * Acquisition task:
     *      samples ECG at 250 Hz
     *
     * Processing task:
     *      processes complete 2000-sample windows
     */
    if (!cardiac.startTasks())
    {
        Serial.println(
            "ECG RTOS task initialization failed."
        );

        while (true)
        {
            delay(1000);
        }
    }


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "WearTwin system initialized."
    );

    Serial.println(
        "ECG acquisition task started."
    );

    Serial.println(
        "ECG processing task started."
    );

    Serial.println(
        "================================"
    );
}


/* ============================================================
 * Main Loop
 *
 * IMPORTANT:
 *
 * ECG acquisition and ECG processing are NOT performed here.
 *
 * They are handled by FreeRTOS tasks inside CardialState.
 *
 * loop() is therefore free for:
 *
 * - IMU
 * - temperature
 * - PPG
 * - SpO2
 * - communication
 * - gateway transmission
 * - future WearTwin processing
 *
 * ============================================================ */

void loop()
{
    /* --------------------------------------------------------
     * IMU
     * -------------------------------------------------------- */

    float angle = inertial.update();


    /* --------------------------------------------------------
     * Periodic system information
     * -------------------------------------------------------- */

    static unsigned long lastPrint = 0;


    if (
        millis() - lastPrint >= 1000
    )
    {
        lastPrint = millis();


        Serial.print(
            "Angle: "
        );

        Serial.print(
            angle
        );


        Serial.print(
            " | Bias: "
        );

        Serial.print(
            inertial.getBias()
        );


        Serial.print(
            " | Avg BPM: "
        );

        Serial.println(
            cardiac.getAverageBPM()
        );
    }
}