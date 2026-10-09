#ifndef PPG_STATE_H
#define PPG_STATE_H

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

class PpgState
{
public:
    enum SignalQuality
    {
        WARMING_UP,
        GOOD,
        DEGRADED,
        POOR,
        NO_DATA
    };

    static constexpr uint8_t ADDRESS = 0x57;
    static constexpr uint16_t WINDOW_SIZE = 500;
    static constexpr uint16_t SAMPLE_RATE_HZ = 100;

    PpgState();
    ~PpgState() = default;

    // Initialization & Task Management
    bool begin();
    bool startTask();

    // Data Accessors (Thread-safe)
    uint32_t getRedRaw() const;
    uint32_t getIrRaw() const;
    float getEstimatedSpO2() const;
    float getPerfusionIndex() const;
    float getMotionScore() const;
    bool isEstimateValid() const;
    SignalQuality getSignalQuality() const;

private:
    // FreeRTOS Task Entry & Loop
    static void taskEntry(void *arg);
    void taskLoop();
    bool pollFifo();

    // I2C Low-level Unlocked Register Operations
    bool writeRegisterUnlocked(uint8_t reg, uint8_t value);
    bool readRegisterUnlocked(uint8_t reg, uint8_t &value);
    bool readFifoSampleUnlocked(uint32_t &red, uint32_t &ir);

    // Processing & Estimation Helpers
    void addSample(uint32_t red, uint32_t ir);
    void estimateWindow();
    
    static void insertionSort(uint32_t *values, uint16_t count);
    uint32_t percentileRange(const uint32_t *values, uint16_t count, uint32_t &meanOut);
    static float clampFloat(float value, float low, float high);

    // Buffer state
    uint32_t redSamples[WINDOW_SIZE];
    uint32_t irSamples[WINDOW_SIZE];
    uint32_t redChronological[WINDOW_SIZE];
    uint32_t irChronological[WINDOW_SIZE];
    uint32_t sortedScratch[WINDOW_SIZE];

    uint32_t redBatch[31];
    uint32_t irBatch[31];

    uint16_t writeIndex;
    uint16_t sampleCount;
    uint16_t samplesSinceEstimate;

    // Output State (Protected by stateMutex)
    uint32_t latestRed;
    uint32_t latestIr;
    float estimatedSpO2;
    float perfusionIndex;
    float motionScore;
    bool estimateValid;
    SignalQuality quality;

    // Synchronization Handles
    SemaphoreHandle_t stateMutex;
    TaskHandle_t taskHandle;
};

#endif // PPG_STATE_H