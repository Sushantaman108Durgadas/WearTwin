#ifndef CARDIAL_STATE_H
#define CARDIAL_STATE_H

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>


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


    // =================================================
    // RTOS buffer configuration
    // =================================================

    /*
     * Two acquisition buffers are used so that:
     *
     * Buffer 0 -> can be processed
     * Buffer 1 -> can be acquired
     *
     * This does NOT change the ECG processing algorithm.
     */
    static constexpr uint8_t BUFFER_COUNT = 2;


    int ecgPin;


    // =================================================
    // Original ECG processing buffer
    // =================================================

    /*
     * This remains the same logical buffer used by
     * the original processBuffer() implementation.
     */
    uint16_t samples[BUFFER_SIZE];


    // =================================================
    // RTOS acquisition buffers
    // =================================================

    /*
     * These buffers belong to the acquisition system.
     *
     * Once a buffer contains 2000 samples, it is passed
     * to the processing task.
     */
    uint16_t ecgBuffers[BUFFER_COUNT][BUFFER_SIZE];


    // Filtered ECG

    float filteredSamples[BUFFER_SIZE];


    uint16_t sampleIndex;

    unsigned long lastSampleTime;

    bool processing;


    // =================================================
    // RTOS
    // =================================================

    struct ECGBlock
    {
        uint16_t *buffer;

        uint32_t startUs;
        uint32_t endUs;
    };


    /*
     * Queue containing completed ECG blocks waiting
     * for processing.
     */
    QueueHandle_t readyQueue;


    /*
     * Queue containing buffers that are available
     * for ECG acquisition.
     */
    QueueHandle_t freeQueue;


    /*
     * Protects shared result data.
     */
    SemaphoreHandle_t resultMutex;


    /*
     * FreeRTOS task handles.
     */
    TaskHandle_t acquisitionTaskHandle;
    TaskHandle_t processingTaskHandle;


    /*
     * Buffer currently being filled by the
     * acquisition task.
     */
    uint16_t *activeBuffer;


    /*
     * Timestamp of the beginning of the current
     * 2000-sample ECG window.
     */
    uint32_t activeStartUs;


    /*
     * Timestamp information for the most recently
     * processed ECG window.
     */
    uint32_t lastWindowStartUs;
    uint32_t lastWindowEndUs;


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
    // FreeRTOS task functions
    // =================================================

    static void acquisitionTaskEntry(void *arg);

    static void processingTaskEntry(void *arg);

    void acquisitionTask();

    void processingTask();


    // =================================================
    // Original ECG processing functions
    // =================================================

    float lowPassFilter(float x);

    void processBuffer();

    void detectRPeaks();

    void calculateRRAndBPM();


    // =================================================
    // Additional helper functions
    // =================================================

    void resetFilterState();

    void printResults();


public:

    // =================================================
    // Constructor
    // =================================================

    CardialState(int ecgPin);


    // =================================================
    // Initialization
    // =================================================

    bool begin();

    bool startTasks();


    // =================================================
    // Original API
    //
    // Kept so the class structure remains familiar.
    // update() will no longer be responsible for the
    // RTOS acquisition loop.
    // =================================================

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


    // =================================================
    // ECG window timing
    // =================================================

    uint32_t getLastWindowStartUs() const;

    uint32_t getLastWindowEndUs() const;
};

#endif