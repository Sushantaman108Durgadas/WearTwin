#include "CardialState.h"


CardialState::CardialState(
    int ecgPin
)
{
    this->ecgPin = ecgPin;

    sampleIndex = 0;

    lastSampleTime = 0;

    processing = false;


    // -------------------------------------------------
    // R-peak / RR / BPM state
    // -------------------------------------------------

    rPeakCount = 0;

    rrCount = 0;

    averageRR = 0.0f;

    averageBPM = 0.0f;


    // -------------------------------------------------
    // IIR filter state
    // -------------------------------------------------

    lp_x1 = 0.0f;
    lp_x2 = 0.0f;

    lp_y1 = 0.0f;
    lp_y2 = 0.0f;
}


void CardialState::begin()
{
    pinMode(
        ecgPin,
        INPUT
    );


    lastSampleTime = micros();


    Serial.println(
        "CardialState initialized."
    );
}


void CardialState::update()
{
    unsigned long now = micros();


    if (
        (unsigned long)
        (now - lastSampleTime)
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

    if (
        sampleIndex >= BUFFER_SIZE
    )
    {
        processBuffer();

        sampleIndex = 0;
    }
}


bool CardialState::bufferFull() const
{
    return sampleIndex == 0;
}


// =====================================================
// 35 Hz 2nd-order Butterworth IIR Low-Pass Filter
// =====================================================
//
// Fs = 250 Hz
// Fc = 35 Hz
//
// y[n] = b0*x[n]
//      + b1*x[n-1]
//      + b2*x[n-2]
//      - a1*y[n-1]
//      - a2*y[n-2]
//
// =====================================================

float CardialState::lowPassFilter(float x)
{
    float y =
        b0_lp * x
        + b1_lp * lp_x1
        + b2_lp * lp_x2
        - a1_lp * lp_y1
        - a2_lp * lp_y2;

    // Shift input history
    lp_x2 = lp_x1;
    lp_x1 = x;

    // Shift output history
    lp_y2 = lp_y1;
    lp_y1 = y;

    return y;
}


// =====================================================
// Process 2000-sample ECG block
// =====================================================

void CardialState::processBuffer()
{
    if (processing)
        return;


    processing = true;


    Serial.println();
    Serial.println(
        "=============================="
    );

    Serial.println(
        "Processing 2000 ECG samples"
    );

    Serial.println(
        "=============================="
    );


    // -------------------------------------------------
    // Reset filter state for this batch
    // -------------------------------------------------

    lp_x1 = 0.0f;
    lp_x2 = 0.0f;

    lp_y1 = 0.0f;
    lp_y2 = 0.0f;


    // -------------------------------------------------
    // Filter all 2000 samples
    // -------------------------------------------------

    for (
        uint16_t i = 0;
        i < BUFFER_SIZE;
        i++
    )
    {
        filteredSamples[i] =
            lowPassFilter(
                (float)samples[i]
            );
    }


    // -------------------------------------------------
    // Detect R peaks
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

    Serial.print(
        "R Peaks detected: "
    );

    Serial.println(
        rPeakCount
    );


    Serial.print(
        "RR intervals: "
    );

    Serial.println(
        rrCount
    );


    for (
        uint8_t i = 0;
        i < rrCount;
        i++
    )
    {
        Serial.print(
            "RR["
        );

        Serial.print(i);

        Serial.print(
            "] = "
        );

        Serial.print(
            rrIntervals[i],
            4
        );

        Serial.print(
            " s | BPM = "
        );

        Serial.println(
            bpmValues[i],
            2
        );
    }


    Serial.print(
        "Average RR: "
    );

    Serial.print(
        averageRR,
        4
    );

    Serial.println(
        " s"
    );


    Serial.print(
        "Average BPM: "
    );

    Serial.println(
        averageBPM,
        2
    );


    Serial.println(
        "=============================="
    );


    processing = false;
}


// =====================================================
// R-peak detection
// =====================================================

void CardialState::detectRPeaks()
{
    rPeakCount = 0;


    // -------------------------------------------------
    // Find min/max of FILTERED signal
    // -------------------------------------------------

    float signalMin =
        filteredSamples[0];

    float signalMax =
        filteredSamples[0];


    for (
        uint16_t i = 1;
        i < BUFFER_SIZE;
        i++
    )
    {
        if (
            filteredSamples[i]
            < signalMin
        )
        {
            signalMin =
                filteredSamples[i];
        }


        if (
            filteredSamples[i]
            > signalMax
        )
        {
            signalMax =
                filteredSamples[i];
        }
    }


    // -------------------------------------------------
    // Adaptive threshold
    // -------------------------------------------------

    float threshold =
        signalMin
        + 0.80f
        * (
            signalMax
            - signalMin
        );


    Serial.print(
        "Filtered Min: "
    );

    Serial.println(
        signalMin,
        2
    );


    Serial.print(
        "Filtered Max: "
    );

    Serial.println(
        signalMax,
        2
    );


    Serial.print(
        "Threshold: "
    );

    Serial.println(
        threshold,
        2
    );


    // -------------------------------------------------
    // Minimum distance between R peaks
    //
    // 250 ms
    // -------------------------------------------------

    const uint16_t refractorySamples =
    SAMPLE_RATE * 0.35f;


    int32_t lastPeak =
        -refractorySamples;


    // -------------------------------------------------
    // Find local maxima
    // -------------------------------------------------

    for (
        uint16_t i = 1;
        i < BUFFER_SIZE - 1;
        i++
    )
    {
        bool isLocalMaximum =
            filteredSamples[i]
            > filteredSamples[i - 1]
            &&
            filteredSamples[i]
            >= filteredSamples[i + 1];


        bool aboveThreshold =
            filteredSamples[i]
            > threshold;


        bool enoughDistance =
            (
                (int32_t)i
                - lastPeak
            )
            >= refractorySamples;


        if (
            isLocalMaximum
            &&
            aboveThreshold
            &&
            enoughDistance
        )
        {
            if (
                rPeakCount
                < MAX_R_PEAKS
            )
            {
                rPeaks[rPeakCount] =
                    i;


                rPeakCount++;


                lastPeak =
                    i;


                Serial.print(
                    "R Peak at sample: "
                );

                Serial.println(
                    i
                );
            }
        }
    }
}


// =====================================================
// Calculate RR intervals and BPM
// =====================================================

void CardialState::calculateRRAndBPM()
{
    rrCount = 0;

    averageRR = 0.0f;

    averageBPM = 0.0f;


    if (
        rPeakCount < 2
    )
    {
        Serial.println(
            "Not enough R peaks for RR/BPM."
        );

        return;
    }


    float rrSum = 0.0f;

    float bpmSum = 0.0f;


    // -------------------------------------------------
    // Calculate every consecutive RR interval
    // -------------------------------------------------

    for (
        uint8_t i = 1;
        i < rPeakCount;
        i++
    )
    {
        uint16_t sampleDifference =
            rPeaks[i]
            - rPeaks[i - 1];


        float rr =
            (float)sampleDifference
            / SAMPLE_RATE;


        float currentBPM =
            rr > 0.0f
            ? 60.0f / rr
            : 0.0f;


        rrIntervals[rrCount] =
            rr;


        bpmValues[rrCount] =
            currentBPM;


        rrCount++;


        rrSum += rr;

        bpmSum += currentBPM;
    }


    // -------------------------------------------------
    // Calculate averages
    // -------------------------------------------------

    if (
        rrCount > 0
    )
    {
        averageRR =
            rrSum
            / rrCount;


        averageBPM =
            bpmSum
            / rrCount;
    }
}


// =====================================================
// Getters
// =====================================================

uint16_t CardialState::getSample(
    uint16_t index
) const
{
    if (
        index >= BUFFER_SIZE
    )
    {
        return 0;
    }


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
    if (
        index >= rPeakCount
    )
    {
        return 0;
    }


    return rPeaks[index];
}


float CardialState::getRR(
    uint8_t index
) const
{
    if (
        index >= rrCount
    )
    {
        return 0.0f;
    }


    return rrIntervals[index];
}


float CardialState::getBPMValue(
    uint8_t index
) const
{
    if (
        index >= rrCount
    )
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