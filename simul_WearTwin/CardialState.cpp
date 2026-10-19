#include "CardialState.h"

CardialState::CardialState(int ecgPin)
{
    this->ecgPin = ecgPin;

    sampleIndex = 0;
    lastSampleTime = 0;

    processing = false;
    hasProcessedBuffer = false;

    rPeakCount = 0;
    rrCount = 0;

    averageRR = 0.0f;
    averageBPM = 0.0f;

    lp_x1 = 0.0f;
    lp_x2 = 0.0f;

    lp_y1 = 0.0f;
    lp_y2 = 0.0f;
}

// =====================================================
// Initialization
// =====================================================

void CardialState::begin()
{
    pinMode(ecgPin, INPUT);

    lastSampleTime = micros();

    Serial.println("CardialState initialized.");
    Serial.println("ECG sample rate: 250 Hz");
    Serial.println("ECG buffer: 2000 samples");
    Serial.println("Acquisition window: 8 seconds");
}

// =====================================================
// ECG acquisition
// =====================================================

void CardialState::update()
{
    if (processing)
    {
        return;
    }

    const unsigned long now = micros();

    // Wait until the next 4 ms sample interval.
    if ((unsigned long)(now - lastSampleTime) <
        SAMPLE_PERIOD_US)
    {
        return;
    }

    // Set the timestamp to the actual sampling time.
    // This avoids repeatedly attempting to catch up
    // after a delayed task iteration.
    lastSampleTime = now;

    // -------------------------------------------------
    // Acquire one ADC sample
    // -------------------------------------------------

    samples[sampleIndex] = analogRead(ecgPin);

    sampleIndex++;

    // -------------------------------------------------
    // Process only after collecting 2000 samples
    // -------------------------------------------------

    if (sampleIndex >= BUFFER_SIZE)
    {
        processing = true;

        processBuffer();

        sampleIndex = 0;

        hasProcessedBuffer = true;

        processing = false;
    }
}

// =====================================================
// Buffer status
// =====================================================

bool CardialState::bufferFull() const
{
    return sampleIndex >= BUFFER_SIZE;
}

bool CardialState::hasProcessed() const
{
    return hasProcessedBuffer;
}

// =====================================================
// 35 Hz Butterworth low-pass filter
// Coefficients retained from your original file.
// =====================================================

float CardialState::lowPassFilter(float x)
{
    const float y =
        b0_lp * x
        + b1_lp * lp_x1
        + b2_lp * lp_x2
        - a1_lp * lp_y1
        - a2_lp * lp_y2;

    // Shift input history.
    lp_x2 = lp_x1;
    lp_x1 = x;

    // Shift output history.
    lp_y2 = lp_y1;
    lp_y1 = y;

    return y;
}

// =====================================================
// Process one complete 2000-sample ECG window
// =====================================================

void CardialState::processBuffer()
{
    Serial.println();
    Serial.println("==============================");
    Serial.println("Processing 2000 ECG samples");
    Serial.println("==============================");

    // -------------------------------------------------
    // Reset filter state at the beginning of this block
    // -------------------------------------------------

    lp_x1 = 0.0f;
    lp_x2 = 0.0f;

    lp_y1 = 0.0f;
    lp_y2 = 0.0f;

    // -------------------------------------------------
    // Filter the complete buffer
    // -------------------------------------------------

    for (uint16_t i = 0; i < BUFFER_SIZE; i++)
    {
        filteredSamples[i] =
            lowPassFilter((float)samples[i]);
    }

    // -------------------------------------------------
    // Detect R-peaks
    // -------------------------------------------------

    detectRPeaks();

    // -------------------------------------------------
    // Calculate RR intervals and BPM
    // -------------------------------------------------

    calculateRRAndBPM();

    // -------------------------------------------------
    // Print results
    // -------------------------------------------------

    Serial.println();

    Serial.print("R Peaks detected: ");
    Serial.println(rPeakCount);

    Serial.print("RR intervals: ");
    Serial.println(rrCount);

    for (uint8_t i = 0; i < rrCount; i++)
    {
        Serial.print("RR[");
        Serial.print(i);
        Serial.print("] = ");

        Serial.print(rrIntervals[i], 4);

        Serial.print(" s | BPM = ");

        Serial.println(bpmValues[i], 2);
    }

    Serial.print("Average RR: ");
    Serial.print(averageRR, 4);
    Serial.println(" s");

    Serial.print("Average BPM: ");
    Serial.println(averageBPM, 2);

    Serial.println("==============================");
}

// =====================================================
// R-peak detection
// =====================================================

void CardialState::detectRPeaks()
{
    rPeakCount = 0;

    // -------------------------------------------------
    // Find the filtered signal minimum and maximum
    // -------------------------------------------------

    float signalMin = filteredSamples[0];
    float signalMax = filteredSamples[0];

    for (uint16_t i = 1; i < BUFFER_SIZE; i++)
    {
        if (filteredSamples[i] < signalMin)
        {
            signalMin = filteredSamples[i];
        }

        if (filteredSamples[i] > signalMax)
        {
            signalMax = filteredSamples[i];
        }
    }

    // -------------------------------------------------
    // Adaptive threshold
    // -------------------------------------------------

    const float threshold =
        signalMin + 0.80f * (signalMax - signalMin);

    Serial.print("Filtered Min: ");
    Serial.println(signalMin, 2);

    Serial.print("Filtered Max: ");
    Serial.println(signalMax, 2);

    Serial.print("Threshold: ");
    Serial.println(threshold, 2);

    // -------------------------------------------------
    // Refractory period: 350 ms
    // -------------------------------------------------

    const uint16_t refractorySamples =
        (SAMPLE_RATE * 35U) / 100U;

    int32_t lastPeak = -(int32_t)refractorySamples;

    // -------------------------------------------------
    // Find local maxima above the threshold
    // -------------------------------------------------

    for (uint16_t i = 1; i < BUFFER_SIZE - 1; i++)
    {
        const bool isLocalMaximum =
            filteredSamples[i] > filteredSamples[i - 1]
            &&
            filteredSamples[i] >= filteredSamples[i + 1];

        const bool aboveThreshold =
            filteredSamples[i] > threshold;

        const bool enoughDistance =
            ((int32_t)i - lastPeak) >= refractorySamples;

        if (isLocalMaximum &&
            aboveThreshold &&
            enoughDistance)
        {
            if (rPeakCount < MAX_R_PEAKS)
            {
                rPeaks[rPeakCount] = i;

                rPeakCount++;
                lastPeak = i;

                Serial.print("R Peak at sample: ");
                Serial.println(i);
            }
        }
    }
}

// =====================================================
// RR interval and BPM calculation
// =====================================================

void CardialState::calculateRRAndBPM()
{
    rrCount = 0;

    averageRR = 0.0f;
    averageBPM = 0.0f;

    if (rPeakCount < 2)
    {
        Serial.println(
            "Not enough R peaks for RR/BPM."
        );

        return;
    }

    float rrSum = 0.0f;
    float bpmSum = 0.0f;

    // -------------------------------------------------
    // Calculate each consecutive RR interval
    // -------------------------------------------------

    for (uint8_t i = 1; i < rPeakCount; i++)
    {
        const uint16_t sampleDifference =
            rPeaks[i] - rPeaks[i - 1];

        const float rr =
            (float)sampleDifference / SAMPLE_RATE;

        const float currentBPM =
            rr > 0.0f ? 60.0f / rr : 0.0f;

        rrIntervals[rrCount] = rr;
        bpmValues[rrCount] = currentBPM;

        rrCount++;

        rrSum += rr;
        bpmSum += currentBPM;
    }

    // -------------------------------------------------
    // Calculate averages
    // -------------------------------------------------

    if (rrCount > 0)
    {
        averageRR = rrSum / rrCount;
        averageBPM = bpmSum / rrCount;
    }
}

// =====================================================
// Getters
// =====================================================

uint16_t CardialState::getSample(uint16_t index) const
{
    if (index >= BUFFER_SIZE)
    {
        return 0;
    }

    return samples[index];
}

uint8_t CardialState::getRPeakCount() const
{
    return rPeakCount;
}

uint16_t CardialState::getRPeak(uint8_t index) const
{
    if (index >= rPeakCount)
    {
        return 0;
    }

    return rPeaks[index];
}

float CardialState::getRR(uint8_t index) const
{
    if (index >= rrCount)
    {
        return 0.0f;
    }

    return rrIntervals[index];
}

float CardialState::getBPMValue(uint8_t index) const
{
    if (index >= rrCount)
    {
        return 0.0f;
    }

    return bpmValues[index];
}

float CardialState::getAverageRR() const
{
    return averageRR;
}

float CardialState::getAverageBPM() const
{
    return averageBPM;
}