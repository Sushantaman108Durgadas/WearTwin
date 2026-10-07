#include "CardialState.h"
#include <string.h>

/* ============================================================
 * Constructor
 * ============================================================ */

CardialState::CardialState(int ecgPin)
    : ecgPin(ecgPin),
      readyQueue(nullptr),
      freeQueue(nullptr),
      resultMutex(nullptr),
      acquisitionTaskHandle(nullptr),
      processingTaskHandle(nullptr),
      activeBuffer(nullptr),
      activeStartUs(0),
      sampleIndex(0),
      lastSampleTime(0),
      processing(false),
      lastWindowStartUs(0),
      lastWindowEndUs(0),
      lp_x1(0.0f),
      lp_x2(0.0f),
      lp_y1(0.0f),
      lp_y2(0.0f),
      rPeakCount(0),
      rrCount(0),
      averageRR(0.0f),
      averageBPM(0.0f)
{
}


/* ============================================================
 * Initialization
 * ============================================================ */

bool CardialState::begin()
{
    pinMode(ecgPin, INPUT);

    /*
     * Queue containing completed ECG blocks waiting
     * for the processing task.
     */
    readyQueue = xQueueCreate(
        2,
        sizeof(ECGBlock)
    );

    /*
     * Queue containing pointers to free ECG buffers.
     */
    freeQueue = xQueueCreate(
        BUFFER_COUNT,
        sizeof(uint16_t *)
    );

    /*
     * Protects results accessed by different tasks.
     */
    resultMutex = xSemaphoreCreateMutex();

    if (!readyQueue || !freeQueue || !resultMutex)
    {
        Serial.println(
            "CardialState: FreeRTOS object creation failed."
        );

        return false;
    }


    /*
     * Put every ECG buffer into the free-buffer queue.
     */
    for (uint8_t i = 0; i < BUFFER_COUNT; ++i)
    {
        uint16_t *buffer = ecgBuffers[i];

        if (xQueueSend(
                freeQueue,
                &buffer,
                0
            ) != pdPASS)
        {
            Serial.println(
                "CardialState: failed to initialize buffer pool."
            );

            return false;
        }
    }


    /*
     * Acquire the first buffer for acquisition.
     */
    if (xQueueReceive(
            freeQueue,
            &activeBuffer,
            0
        ) != pdPASS)
    {
        Serial.println(
            "CardialState: failed to acquire initial ECG buffer."
        );

        return false;
    }


    sampleIndex = 0;
    activeStartUs = micros();
    lastSampleTime = activeStartUs;

    processing = false;

    rPeakCount = 0;
    rrCount = 0;
    averageRR = 0.0f;
    averageBPM = 0.0f;

    resetFilterState();

    Serial.println(
        "CardialState initialized (FreeRTOS)."
    );

    return true;
}


/* ============================================================
 * Start FreeRTOS Tasks
 * ============================================================ */

bool CardialState::startTasks()
{
    BaseType_t acquisitionResult =
        xTaskCreatePinnedToCore(
            acquisitionTaskEntry,
            "ECG_Acquire",
            4096,
            this,
            3,
            &acquisitionTaskHandle,
            1
        );


    BaseType_t processingResult =
        xTaskCreatePinnedToCore(
            processingTaskEntry,
            "ECG_Process",
            8192,
            this,
            1,
            &processingTaskHandle,
            0
        );


    if (acquisitionResult != pdPASS ||
        processingResult != pdPASS)
    {
        Serial.println(
            "CardialState: failed to create ECG tasks."
        );

        return false;
    }

    return true;
}


/* ============================================================
 * Acquisition Task Entry
 * ============================================================ */

void CardialState::acquisitionTaskEntry(void *arg)
{
    CardialState *self =
        static_cast<CardialState *>(arg);

    self->acquisitionTask();

    /*
     * Normally never reached because acquisitionTask()
     * runs continuously.
     */
    vTaskDelete(nullptr);
}


/* ============================================================
 * Processing Task Entry
 * ============================================================ */

void CardialState::processingTaskEntry(void *arg)
{
    CardialState *self =
        static_cast<CardialState *>(arg);

    self->processingTask();

    vTaskDelete(nullptr);
}


/* ============================================================
 * ECG Acquisition Task
 *
 * This task ONLY acquires samples.
 *
 * It does NOT:
 * - filter
 * - detect R peaks
 * - calculate BPM
 *
 * That is deliberately handled by the processing task.
 * ============================================================ */

void CardialState::acquisitionTask()
{
    TickType_t lastWake =
        xTaskGetTickCount();


    while (true)
    {
        /*
         * 250 Hz
         *
         * 1 / 250 = 4 ms
         */
        vTaskDelayUntil(
            &lastWake,
            pdMS_TO_TICKS(4)
        );


        /*
         * Start timestamp of a new 2000-sample window.
         */
        if (sampleIndex == 0)
        {
            activeStartUs = micros();
        }


        /*
         * Acquire one ECG ADC sample.
         */
        activeBuffer[sampleIndex] =
            analogRead(ecgPin);

        sampleIndex++;


        /*
         * Window is not complete yet.
         */
        if (sampleIndex < BUFFER_SIZE)
        {
            continue;
        }


        /*
         * We now have exactly 2000 samples.
         */
        ECGBlock block;

        block.buffer = activeBuffer;
        block.startUs = activeStartUs;
        block.endUs = micros();


        /*
         * Send completed block to processing task.
         *
         * Do not wait here because acquisition is the
         * timing-sensitive task.
         */
        if (xQueueSend(
                readyQueue,
                &block,
                0
            ) != pdPASS)
        {
            /*
             * Processing queue was full.
             * Return this buffer to the free pool.
             */
            uint16_t *dropped =
                block.buffer;

            xQueueSend(
                freeQueue,
                &dropped,
                0
            );

            Serial.println(
                "ECG: processing queue full; "
                "dropping window."
            );
        }


        /*
         * The current window is complete.
         */
        sampleIndex = 0;


        /*
         * Obtain another buffer for the next
         * 2000-sample window.
         */
        while (
            xQueueReceive(
                freeQueue,
                &activeBuffer,
                pdMS_TO_TICKS(20)
            ) != pdPASS
        )
        {
            taskYIELD();
        }
    }
}


/* ============================================================
 * ECG Processing Task
 *
 * Receives a complete 2000-sample window and runs
 * the ORIGINAL ECG processing pipeline.
 * ============================================================ */

void CardialState::processingTask()
{
    ECGBlock block;


    while (true)
    {
        /*
         * Sleep until a complete ECG window arrives.
         */
        if (
            xQueueReceive(
                readyQueue,
                &block,
                portMAX_DELAY
            ) == pdPASS
        )
        {
            /*
             * Copy the acquired window into the original
             * samples[] array.
             *
             * This allows the original processing logic
             * to remain unchanged.
             */
            memcpy(
                samples,
                block.buffer,
                sizeof(samples)
            );


            /*
             * ORIGINAL ECG PROCESSING:
             *
             * 2000 samples
             *      ↓
             * low-pass filter
             *      ↓
             * R-peak detection
             *      ↓
             * RR intervals
             *      ↓
             * BPM
             */
            processBuffer();


            /*
             * Store temporal information about
             * this processed window.
             */
            if (
                xSemaphoreTake(
                    resultMutex,
                    portMAX_DELAY
                ) == pdTRUE
            )
            {
                lastWindowStartUs =
                    block.startUs;

                lastWindowEndUs =
                    block.endUs;

                xSemaphoreGive(
                    resultMutex
                );
            }


            /*
             * The processing task is finished with
             * this acquisition buffer.
             *
             * Return it to the free-buffer pool.
             */
            uint16_t *buffer =
                block.buffer;

            xQueueSend(
                freeQueue,
                &buffer,
                portMAX_DELAY
            );
        }
    }
}


/* ============================================================
 * Original update()
 *
 * Kept for API compatibility.
 *
 * Sampling is now performed by acquisitionTask().
 * Processing is performed by processingTask().
 * ============================================================ */

void CardialState::update()
{
    /*
     * Intentionally empty.
     *
     * RTOS tasks now own ECG acquisition and processing.
     */
}


/* ============================================================
 * Original bufferFull()
 * ============================================================ */

bool CardialState::bufferFull() const
{
    return sampleIndex == 0;
}


/* ============================================================
 * Original getSample()
 * ============================================================ */

uint16_t CardialState::getSample(uint16_t index) const
{
    if (index >= BUFFER_SIZE)
        return 0;

    return samples[index];
}


/* ============================================================
 * Original processBuffer()
 *
 * IMPORTANT:
 * The actual ECG algorithm is NOT changed here.
 *
 * ============================================================ */

void CardialState::processBuffer()
{
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


    /*
     * Reset IIR filter state before processing
     * the new 2000-sample window.
     */
    resetFilterState();


    /*
     * ORIGINAL FILTERING LOGIC
     */
    for (uint16_t i = 0;
         i < BUFFER_SIZE;
         ++i)
    {
        filteredSamples[i] =
            lowPassFilter(
                (float)samples[i]
            );
    }


    /*
     * ORIGINAL R-PEAK DETECTION
     */
    detectRPeaks();


    /*
     * ORIGINAL RR/BPM CALCULATION
     */
    calculateRRAndBPM();


    /*
     * ORIGINAL RESULT PRINTING
     */
    printResults();


    processing = false;
}


/* ============================================================
 * Filter State Reset
 * ============================================================ */

void CardialState::resetFilterState()
{
    lp_x1 = 0.0f;
    lp_x2 = 0.0f;

    lp_y1 = 0.0f;
    lp_y2 = 0.0f;
}


/* ============================================================
 * 2nd Order IIR Low-Pass Filter
 *
 * Original difference equation.
 * ============================================================ */

float CardialState::lowPassFilter(float x)
{
    float y =
        b0_lp * x
        + b1_lp * lp_x1
        + b2_lp * lp_x2
        - a1_lp * lp_y1
        - a2_lp * lp_y2;


    /*
     * Shift previous input samples.
     */
    lp_x2 = lp_x1;
    lp_x1 = x;


    /*
     * Shift previous output samples.
     */
    lp_y2 = lp_y1;
    lp_y1 = y;


    return y;
}


/* ============================================================
 * ORIGINAL R-PEAK DETECTION
 * ============================================================ */

void CardialState::detectRPeaks()
{
    rPeakCount = 0;


    /*
     * Find minimum and maximum of filtered window.
     */
    float signalMin =
        filteredSamples[0];

    float signalMax =
        filteredSamples[0];


    for (uint16_t i = 1;
         i < BUFFER_SIZE;
         ++i)
    {
        if (
            filteredSamples[i] <
            signalMin
        )
        {
            signalMin =
                filteredSamples[i];
        }


        if (
            filteredSamples[i] >
            signalMax
        )
        {
            signalMax =
                filteredSamples[i];
        }
    }


    /*
     * Adaptive threshold.
     *
     * 80% of the min-max range.
     */
    float threshold =
        signalMin +
        0.80f *
        (signalMax - signalMin);


    /*
     * 0.35 second refractory period.
     *
     * At 250 Hz:
     *
     * 250 × 0.35 = 87.5 samples
     */
    const uint16_t refractorySamples =
        (uint16_t)(
            SAMPLE_RATE * 0.35f
        );


    int32_t lastPeak =
        -refractorySamples;


    /*
     * Search for local maxima.
     */
    for (
        uint16_t i = 1;
        i < BUFFER_SIZE - 1;
        ++i
    )
    {
        bool localMaximum =
            filteredSamples[i] >
                filteredSamples[i - 1]
            &&
            filteredSamples[i] >=
                filteredSamples[i + 1];


        bool aboveThreshold =
            filteredSamples[i] >
            threshold;


        bool enoughDistance =
            (
                (int32_t)i -
                lastPeak
            ) >=
            refractorySamples;


        if (
            localMaximum &&
            aboveThreshold &&
            enoughDistance &&
            rPeakCount < MAX_R_PEAKS
        )
        {
            rPeaks[rPeakCount++] =
                i;

            lastPeak = i;
        }
    }
}


/* ============================================================
 * ORIGINAL RR + BPM CALCULATION
 * ============================================================ */

void CardialState::calculateRRAndBPM()
{
    rrCount = 0;

    averageRR = 0.0f;

    averageBPM = 0.0f;


    /*
     * At least two R peaks are required
     * for one RR interval.
     */
    if (rPeakCount < 2)
        return;


    float rrSum = 0.0f;
    float bpmSum = 0.0f;


    for (
        uint8_t i = 1;
        i < rPeakCount;
        ++i
    )
    {
        /*
         * RR interval in seconds.
         */
        float rr =
            (float)(
                rPeaks[i] -
                rPeaks[i - 1]
            )
            /
            SAMPLE_RATE;


        /*
         * BPM = 60 / RR.
         */
        float bpm =
            rr > 0.0f
                ? 60.0f / rr
                : 0.0f;


        rrIntervals[rrCount] =
            rr;

        bpmValues[rrCount] =
            bpm;

        ++rrCount;


        rrSum += rr;

        bpmSum += bpm;
    }


    /*
     * Calculate average RR and BPM.
     */
    if (rrCount)
    {
        averageRR =
            rrSum /
            rrCount;

        averageBPM =
            bpmSum /
            rrCount;
    }
}


/* ============================================================
 * ORIGINAL RESULT PRINTING
 * ============================================================ */

void CardialState::printResults()
{
    Serial.print(
        "R Peaks detected: "
    );

    Serial.println(
        rPeakCount
    );


    for (
        uint8_t i = 0;
        i < rPeakCount;
        ++i
    )
    {
        Serial.print(
            "R Peak at sample: "
        );

        Serial.println(
            rPeaks[i]
        );
    }


    Serial.print(
        "RR intervals: "
    );

    Serial.println(
        rrCount
    );


    for (
        uint8_t i = 0;
        i < rrCount;
        ++i
    )
    {
        Serial.print(
            "RR["
        );

        Serial.print(
            i
        );

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
}


/* ============================================================
 * Getters
 * ============================================================ */

float CardialState::getAverageRR() const
{
    return averageRR;
}


float CardialState::getAverageBPM() const
{
    return averageBPM;
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


float CardialState::getRR(
    uint8_t index
) const
{
    if (index >= rrCount)
        return 0.0f;

    return rrIntervals[index];
}


float CardialState::getBPMValue(
    uint8_t index
) const
{
    if (index >= rrCount)
        return 0.0f;

    return bpmValues[index];
}


uint32_t CardialState::getLastWindowStartUs() const
{
    return lastWindowStartUs;
}


uint32_t CardialState::getLastWindowEndUs() const
{
    return lastWindowEndUs;
}