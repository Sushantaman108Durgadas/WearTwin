#ifndef CARDIAL_STATE_H
#define CARDIAL_STATE_H

#include <Arduino.h>


class CardialState {

private:

    // -------------------------------------------------
    // ECG analog input
    // -------------------------------------------------

    int ecgPin;


    // -------------------------------------------------
    // Sampling
    // -------------------------------------------------

    static constexpr uint16_t BUFFER_SIZE = 2000;

    static constexpr uint16_t SAMPLE_RATE = 250;

    static constexpr uint32_t SAMPLE_PERIOD_US =
        1000000UL / SAMPLE_RATE;


    // -------------------------------------------------
    // ECG buffer
    // -------------------------------------------------

    uint16_t samples[BUFFER_SIZE];

    uint16_t sampleIndex;


    // -------------------------------------------------
    // Timing
    // -------------------------------------------------

    unsigned long lastSampleTime;


    // -------------------------------------------------
    // R-peak information
    // -------------------------------------------------

    uint16_t rPeaks[20];

    uint8_t rPeakCount;


    // -------------------------------------------------
    // Heart-rate information
    // -------------------------------------------------

    float rrInterval;

    float bpm;


    // -------------------------------------------------
    // Internal functions
    // -------------------------------------------------

    void detectRPeaks();

    void calculateBPM();


public:

    // -------------------------------------------------
    // Constructor
    // -------------------------------------------------

    CardialState(
        int ecgPin
    );


    // -------------------------------------------------
    // Initialization
    // -------------------------------------------------

    void begin();


    // -------------------------------------------------
    // ECG sampling
    // -------------------------------------------------

    void update();


    // -------------------------------------------------
    // Buffer status
    // -------------------------------------------------

    bool bufferFull() const;


    // -------------------------------------------------
    // ECG processing
    // -------------------------------------------------

    void processBuffer();


    // -------------------------------------------------
    // Sample access
    // -------------------------------------------------

    uint16_t getSample(
        uint16_t index
    ) const;


    // -------------------------------------------------
    // R-peak access
    // -------------------------------------------------

    uint8_t getRPeakCount() const;

    uint16_t getRPeak(
        uint8_t index
    ) const;


    // -------------------------------------------------
    // Heart-rate information
    // -------------------------------------------------

    float getRRInterval() const;

    float getBPM() const;
};

#endif