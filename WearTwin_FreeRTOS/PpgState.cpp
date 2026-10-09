#include "PpgState.h"
#include "I2CBusLock.h"

#include <math.h>

namespace
{
constexpr uint8_t REG_FIFO_WR_PTR = 0x04;
constexpr uint8_t REG_FIFO_OVF    = 0x05;
constexpr uint8_t REG_FIFO_RD_PTR = 0x06;
constexpr uint8_t REG_FIFO_DATA   = 0x07;
constexpr uint8_t REG_MODE_CONFIG = 0x09;
constexpr uint8_t REG_SPO2_CONFIG = 0x0A;
constexpr uint8_t REG_PART_ID     = 0xFF;
constexpr uint32_t MAX_18BIT      = 0x3FFFFUL;
}

PpgState::PpgState()
    : writeIndex(0), sampleCount(0), samplesSinceEstimate(0),
      latestRed(0), latestIr(0), estimatedSpO2(0.0f),
      perfusionIndex(0.0f), motionScore(0.0f),
      estimateValid(false), quality(WARMING_UP),
      stateMutex(nullptr), taskHandle(nullptr)
{
    for (uint16_t i = 0; i < WINDOW_SIZE; ++i)
    {
        redSamples[i] = irSamples[i] = 0;
        redChronological[i] = irChronological[i] = sortedScratch[i] = 0;
    }
    for (uint8_t i = 0; i < 31; ++i)
        redBatch[i] = irBatch[i] = 0;
}

bool PpgState::writeRegisterUnlocked(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool PpgState::readRegisterUnlocked(uint8_t reg, uint8_t &value)
{
    Wire.beginTransmission(ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom(ADDRESS, (uint8_t)1) != 1)
        return false;
    int v = Wire.read();
    if (v < 0)
        return false;
    value = (uint8_t)v;
    return true;
}

bool PpgState::readFifoSampleUnlocked(uint32_t &red, uint32_t &ir)
{
    Wire.beginTransmission(ADDRESS);
    Wire.write(REG_FIFO_DATA);
    if (Wire.endTransmission(false) != 0)
        return false;

    if (Wire.requestFrom(ADDRESS, (uint8_t)6) != 6)
        return false;

    uint8_t b[6];
    for (uint8_t n = 0; n < 6; ++n)
    {
        int v = Wire.read();
        if (v < 0)
            return false;
        b[n] = (uint8_t)v;
    }

    red = ((((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2]) & MAX_18BIT);
    ir  = ((((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5]) & MAX_18BIT);
    return true;
}

bool PpgState::begin()
{
    if (stateMutex == nullptr)
        stateMutex = xSemaphoreCreateMutex();

    if (stateMutex == nullptr || i2cBusMutex == nullptr)
    {
        Serial.println("PpgState: mutex creation failed.");
        return false;
    }

    if (xSemaphoreTake(i2cBusMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        Serial.println("PpgState: I2C bus busy.");
        return false;
    }

    uint8_t partId = 0;
    const bool found = readRegisterUnlocked(REG_PART_ID, partId);
    bool configured = false;

    if (found && partId == 0x15)
    {
        configured =
            writeRegisterUnlocked(REG_FIFO_WR_PTR, 0x00) &&
            writeRegisterUnlocked(REG_FIFO_OVF, 0x00) &&
            writeRegisterUnlocked(REG_FIFO_RD_PTR, 0x00) &&
            writeRegisterUnlocked(REG_MODE_CONFIG, 0x03) &&
            writeRegisterUnlocked(REG_SPO2_CONFIG, 0x27);
    }

    xSemaphoreGive(i2cBusMutex);

    if (!found || partId != 0x15 || !configured)
    {
        Serial.println("PpgState: device initialization failed at 0x57.");
        return false;
    }

    Serial.printf("PpgState initialized; PART_ID=0x%02X\n", partId);
    Serial.println("PPG mode: RED + IR, 100 Hz.");
    return true;
}

bool PpgState::startTask()
{
    if (taskHandle != nullptr)
        return true;

    BaseType_t result = xTaskCreatePinnedToCore(
        taskEntry, "PPG_Task", 8192, this, 2, &taskHandle, 0);

    if (result != pdPASS)
    {
        taskHandle = nullptr;
        Serial.println("PpgState: task creation failed.");
        return false;
    }
    return true;
}

void PpgState::taskEntry(void *arg)
{
    static_cast<PpgState *>(arg)->taskLoop();
    vTaskDelete(nullptr);
}

void PpgState::taskLoop()
{
    TickType_t lastWake = xTaskGetTickCount();
    while (true)
    {
        pollFifo();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
    }
}

bool PpgState::pollFifo()
{
    if (i2cBusMutex == nullptr ||
        xSemaphoreTake(i2cBusMutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return false;

    uint8_t wr = 0, rd = 0, overflow = 0;
    if (!readRegisterUnlocked(REG_FIFO_WR_PTR, wr) ||
        !readRegisterUnlocked(REG_FIFO_RD_PTR, rd) ||
        !readRegisterUnlocked(REG_FIFO_OVF, overflow))
    {
        xSemaphoreGive(i2cBusMutex);
        return false;
    }

    wr &= 0x1F;
    rd &= 0x1F;
    // Pointer subtraction cannot distinguish empty from a full 32-entry FIFO.
    // The simulator's overflow counter flags lost samples; cap each read to 31.
    uint8_t available = (uint8_t)((wr - rd) & 0x1F);
    if (available > 31) available = 31;

    uint8_t received = 0;
    for (uint8_t n = 0; n < available; ++n)
    {
        uint32_t red = 0, ir = 0;
        if (!readFifoSampleUnlocked(red, ir))
            break;

        redBatch[received] = red;
        irBatch[received] = ir;
        ++received;
    }
    xSemaphoreGive(i2cBusMutex);

    for (uint8_t n = 0; n < received; ++n)
        addSample(redBatch[n], irBatch[n]);

    (void)overflow;
    return received > 0 || available == 0;
}

void PpgState::addSample(uint32_t red, uint32_t ir)
{
    if (stateMutex != nullptr &&
        xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE)
    {
        latestRed = red;
        latestIr = ir;
        xSemaphoreGive(stateMutex);
    }

    redSamples[writeIndex] = red;
    irSamples[writeIndex] = ir;
    writeIndex = (uint16_t)((writeIndex + 1U) % WINDOW_SIZE);

    if (sampleCount < WINDOW_SIZE)
    {
        ++sampleCount;
        if (sampleCount == WINDOW_SIZE)
        {
            samplesSinceEstimate = 0;
            estimateWindow();
        }
        return;
    }

    if (++samplesSinceEstimate >= SAMPLE_RATE_HZ)
    {
        samplesSinceEstimate = 0;
        estimateWindow();
    }
}

void PpgState::insertionSort(uint32_t *values, uint16_t count)
{
    for (uint16_t i = 1; i < count; ++i)
    {
        uint32_t key = values[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && values[j] > key)
        {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = key;
    }
}

uint32_t PpgState::percentileRange(
    const uint32_t *values, uint16_t count, uint32_t &meanOut)
{
    if (count == 0 || count > WINDOW_SIZE)
    {
        meanOut = 0;
        return 0;
    }

    uint64_t sum = 0;
    for (uint16_t i = 0; i < count; ++i)
    {
        sortedScratch[i] = values[i];
        sum += values[i];
    }
    meanOut = (uint32_t)(sum / count);

    insertionSort(sortedScratch, count);
    if (count < 10)
        return 0;

    uint16_t lowIndex = (uint16_t)((count - 1U) * 10U / 100U);
    uint16_t highIndex = (uint16_t)((count - 1U) * 90U / 100U);
    return sortedScratch[highIndex] - sortedScratch[lowIndex];
}

float PpgState::clampFloat(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

void PpgState::estimateWindow()
{
    // writeIndex points at the oldest sample once the circular buffer is full.
    for (uint16_t i = 0; i < WINDOW_SIZE; ++i)
    {
        uint16_t idx = (uint16_t)((writeIndex + i) % WINDOW_SIZE);
        redChronological[i] = redSamples[idx];
        irChronological[i] = irSamples[idx];
    }

    uint32_t redDC = 0, irDC = 0;
    uint32_t redAC = percentileRange(redChronological, WINDOW_SIZE, redDC);
    uint32_t irAC = percentileRange(irChronological, WINDOW_SIZE, irDC);

    const float redNorm = redDC ? (float)redAC / (float)redDC : 0.0f;
    const float irNorm  = irDC  ? (float)irAC  / (float)irDC  : 0.0f;
    const float ratio = irNorm > 0.000001f ? redNorm / irNorm : 0.0f;
    const float pi = irDC ? 100.0f * (float)irAC / (float)irDC : 0.0f;

    // Synthetic mapping used only by this simulator. The target is not read by firmware;\n    // it is inferred from the RED/IR ratio of the generated raw signal. Not clinical calibration.
    const float spo2 = clampFloat(110.0f - 25.0f * ratio, 70.0f, 100.0f);

    double residualSum = 0.0, residualSquareSum = 0.0;
    for (uint16_t i = 1; i < WINDOW_SIZE - 1; ++i)
    {
        float smooth = ((float)irChronological[i - 1] +
                        (float)irChronological[i] +
                        (float)irChronological[i + 1]) / 3.0f;
        double residual = (double)irChronological[i] - smooth;
        residualSum += residual;
        residualSquareSum += residual * residual;
    }

    const double residualMean = residualSum / (WINDOW_SIZE - 2);
    double residualVariance =
        residualSquareSum / (WINDOW_SIZE - 2) - residualMean * residualMean;
    if (residualVariance < 0.0) residualVariance = 0.0;

    const float residualRms = sqrtf((float)residualVariance);
    const float disturbanceRatio = residualRms / (float)((irAC > 100) ? irAC : 100);
    const float motion = clampFloat((disturbanceRatio - 0.10f) * 200.0f, 0.0f, 100.0f);

    SignalQuality q = GOOD;
    bool valid = true;

    if (redDC < 1000 || irDC < 1000 || redAC < 10 || irAC < 10 ||
        !isfinite(ratio) || ratio < 0.20f || ratio > 2.00f)
    {
        q = POOR;
        valid = false;
    }
    else if (pi < 0.20f || motion >= 70.0f)
    {
        q = POOR;
        valid = false;
    }
    else if (pi < 0.50f || motion >= 35.0f)
    {
        q = DEGRADED;
    }

    Serial.printf(
        "[PPG DEBUG] RED DC=%lu AC=%lu | IR DC=%lu AC=%lu | R=%.4f | PI=%.2f%% | Motion=%.1f | Quality=%s\n",
        (unsigned long)redDC, (unsigned long)redAC,
        (unsigned long)irDC, (unsigned long)irAC,
        ratio, pi, motion,
        q == GOOD ? "GOOD" : q == DEGRADED ? "DEGRADED" : "POOR"
    );

    if (valid)
        Serial.printf("[PPG DEBUG] Candidate SpO2 = %.2f%%\n", spo2);
    else
        Serial.println("[PPG DEBUG] Estimate unavailable: signal rejected.");

    if (stateMutex != nullptr &&
        xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE)
    {
        perfusionIndex = pi;
        motionScore = motion;
        estimatedSpO2 = spo2;
        estimateValid = valid;
        quality = q;
        xSemaphoreGive(stateMutex);
    }
}

uint32_t PpgState::getRedRaw() const
{
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE)
        return 0;
    uint32_t value = latestRed;
    xSemaphoreGive(stateMutex);
    return value;
}

uint32_t PpgState::getIrRaw() const
{
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE)
        return 0;
    uint32_t value = latestIr;
    xSemaphoreGive(stateMutex);
    return value;
}

float PpgState::getEstimatedSpO2() const
{
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE)
        return 0.0f;
    float value = estimatedSpO2;
    xSemaphoreGive(stateMutex);
    return value;
}

float PpgState::getPerfusionIndex() const
{
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE)
        return 0.0f;
    float value = perfusionIndex;
    xSemaphoreGive(stateMutex);
    return value;
}

float PpgState::getMotionScore() const
{
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE)
        return 100.0f;
    float value = motionScore;
    xSemaphoreGive(stateMutex);
    return value;
}

bool PpgState::isEstimateValid() const
{
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE)
        return false;
    bool value = estimateValid;
    xSemaphoreGive(stateMutex);
    return value;
}

PpgState::SignalQuality PpgState::getSignalQuality() const
{
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE)
        return NO_DATA;
    SignalQuality value = quality;
    xSemaphoreGive(stateMutex);
    return value;
}
