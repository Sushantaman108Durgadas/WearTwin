#ifndef CARDIAL_STATE_H
#define CARDIAL_STATE_H

#include <Arduino.h>

class CardialState
{
private:

    // =================================================
    // ECG configuration
    // =================================================

    static constexpr uint16_t BUFFER_SIZE = 2000;
    static constexpr uint16_t SAMPLE_RATE = 250;
    static constexpr uint32_t SAMPLE_PERIOD_US =
        1000000UL / SAMPLE_RATE;


    int ecgPin;

    uint16_t samples[BUFFER_SIZE];

    // Filtered ECG
    float filteredSamples[BUFFER_SIZE];

    uint16_t sampleIndex;

    unsigned long lastSampleTime;

    bool processing;


    // =================================================
    // 35 Hz 2nd-order Butterworth Low-Pass Filter
    //
    // Fs = 250 Hz
    // Fc = 35 Hz
    //
    // y[n] = b0*x[n]
    //      + b1*x[n-1]
    //      + b2*x[n-2]
    //      - a1*y[n-1]
    //      - a2*y[n-2]
    // =================================================

    static constexpr float b0_lp = 0.1174f;
    static constexpr float b1_lp = 0.2347f;
    static constexpr float b2_lp = 0.1174f;

    static constexpr float a1_lp = -0.8252f;
    static constexpr float a2_lp = 0.2946f;


    // IIR filter state

    float lp_x1;
    float lp_x2;

    float lp_y1;
    float lp_y2;


    // =================================================
    // R-peak detection
    // =================================================

    static constexpr uint8_t MAX_R_PEAKS = 20;

    uint16_t rPeaks[MAX_R_PEAKS];

    uint8_t rPeakCount;


    // =================================================
    // RR / BPM
    // =================================================

    float rrIntervals[MAX_R_PEAKS - 1];

    float bpmValues[MAX_R_PEAKS - 1];

    uint8_t rrCount;

    float averageRR;

    float averageBPM;


    // =================================================
    // Functions
    // =================================================

    float lowPassFilter(float x);

    void processBuffer();

    void detectRPeaks();

    void calculateRRAndBPM();


public:

    CardialState(int ecgPin);

    void begin();

    void update();


    // =================================================
    // Getters
    // =================================================

    bool bufferFull() const;

    uint16_t getSample(uint16_t index) const;

    uint8_t getRPeakCount() const;

    uint16_t getRPeak(uint8_t index) const;

    float getRR(uint8_t index) const;

    float getBPMValue(uint8_t index) const;

    float getAverageRR() const;

    float getAverageBPM() const;
};

#endif