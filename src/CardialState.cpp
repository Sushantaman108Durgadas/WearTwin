#include "CardialState.h"


CardialState::CardialState(
    int ecgPin
)
{
    this->ecgPin = ecgPin;

    sampleIndex = 0;

    lastSampleTime = 0;

    rPeakCount = 0;

    rrInterval = 0.0f;

    bpm = 0.0f;
}


void CardialState::begin()
{
    // -------------------------------------------------
    // ECG input
    // -------------------------------------------------

    pinMode(
        ecgPin,
        INPUT
    );


    lastSampleTime = micros();
}


void CardialState::update()
{
    unsigned long now = micros();

    if (
        now - lastSampleTime
        < SAMPLE_PERIOD_US
    )
    {
        return;
    }

    lastSampleTime += SAMPLE_PERIOD_US;


    // -------------------------------------------------
    // Take one ECG sample
    // -------------------------------------------------

    samples[sampleIndex] =
        analogRead(ecgPin);


    sampleIndex++;


    // -------------------------------------------------
    // 2000 samples collected
    // -------------------------------------------------

    if (sampleIndex >= BUFFER_SIZE)
    {
        processBuffer();

        sampleIndex = 0;
    }
}


bool CardialState::bufferFull() const
{
    return sampleIndex >= BUFFER_SIZE;
}


void CardialState::processBuffer()
{
    detectRPeaks();

    calculateBPM();
}


void CardialState::detectRPeaks()
{
    rPeakCount = 0;


    // -------------------------------------------------
    // Find minimum and maximum ECG values
    // -------------------------------------------------

    uint16_t minValue = 4095;

    uint16_t maxValue = 0;


    for (
        uint16_t i = 0;
        i < BUFFER_SIZE;
        i++
    )
    {
        if (samples[i] < minValue)
            minValue = samples[i];

        if (samples[i] > maxValue)
            maxValue = samples[i];
    }


    // -------------------------------------------------
    // Adaptive threshold
    //
    // threshold =
    // min + 60% of signal range
    // -------------------------------------------------

    float threshold =
        minValue
        + 0.60f * (maxValue - minValue);


    // -------------------------------------------------
    // Minimum distance between R peaks
    //
    // 250 ms = 0.25 sec
    //
    // Maximum theoretical BPM:
    //
    // 60 / 0.25 = 240 BPM
    // -------------------------------------------------

    const uint16_t refractorySamples =
        SAMPLE_RATE * 0.25f;


    int lastPeak =
        -refractorySamples;


    // -------------------------------------------------
    // Find local maxima above threshold
    // -------------------------------------------------

    for (
        uint16_t i = 1;
        i < BUFFER_SIZE - 1;
        i++
    )
    {
        bool isLocalMaximum =
            samples[i] > samples[i - 1] &&
            samples[i] >= samples[i + 1];


        bool aboveThreshold =
            samples[i] > threshold;


        bool enoughDistance =
            (i - lastPeak) >= refractorySamples;


        if (
            isLocalMaximum &&
            aboveThreshold &&
            enoughDistance
        )
        {
            if (rPeakCount < 20)
            {
                rPeaks[rPeakCount] = i;

                rPeakCount++;

                lastPeak = i;
            }
        }
    }
}


void CardialState::calculateBPM()
{
    if (rPeakCount < 2)
    {
        rrInterval = 0.0f;

        bpm = 0.0f;

        return;
    }


    // -------------------------------------------------
    // Use the last two R peaks
    // -------------------------------------------------

    uint16_t r1 =
        rPeaks[rPeakCount - 2];

    uint16_t r2 =
        rPeaks[rPeakCount - 1];


    // -------------------------------------------------
    // Difference in samples
    // -------------------------------------------------

    uint16_t difference =
        r2 - r1;


    // -------------------------------------------------
    // Convert samples to seconds
    // -------------------------------------------------

    rrInterval =
        (float)difference
        / SAMPLE_RATE;


    // -------------------------------------------------
    // Calculate BPM
    //
    // BPM = 60 / RR interval
    // -------------------------------------------------

    bpm =
        60.0f / rrInterval;
}


uint16_t CardialState::getSample(
    uint16_t index
) const
{
    if (index >= BUFFER_SIZE)
        return 0;

    return samples[index];
}


uint8_t CardialState::getRPeakCount() const
{
    return rPeakCount;
}


uint16_t CardialState::getRPeak(
    uint8_t index
) const
{
    if (index >= rPeakCount)
        return 0;

    return rPeaks[index];
}


float CardialState::getRRInterval() const
{
    return rrInterval;
}


float CardialState::getBPM() const
{
    return bpm;
}