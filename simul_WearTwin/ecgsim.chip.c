#include "wokwi-api.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define PI 3.14159265358979323846f

// =====================================================
// ECG configuration
// =====================================================

#define TEMPLATE_SIZE 256
#define HARMONICS 32

// Generate one ECG sample every 10 ms.
#define SAMPLE_RATE 250
#define SAMPLE_PERIOD_US (1000000 / SAMPLE_RATE)

// =====================================================
// ECG simulator state
// =====================================================

typedef struct
{
    // Analog ECG output pin
    pin_t ecg_out;

    // Wokwi BPM slider attribute
    uint32_t heart_rate_attr;

    // Periodic sample-generation timer
    timer_t timer;

    // Fourier coefficients: real[k] + j*imag[k]
    float real[TEMPLATE_SIZE];
    float imag[TEMPLATE_SIZE];

    // Number of samples generated
    uint32_t sample;

    // Current simulated heart rate
    float bpm;

} ecg_state_t;

// =====================================================
// Gaussian wave component
// =====================================================

static float gaussian(
    float x,
    float center,
    float width,
    float amplitude
)
{
    float d = x - center;

    return amplitude *
           expf(-(d * d) / (2.0f * width * width));
}

// =====================================================
// Idealized P-Q-R-S-T ECG template
// =====================================================

static float ecg_template(float phase)
{
    float value = 0.0f;

    // P wave
    value += gaussian(
        phase,
        0.18f,
        0.025f,
        0.18f
    );

    // Q wave
    value += gaussian(
        phase,
        0.285f,
        0.010f,
        -0.12f
    );

    // R wave
    value += gaussian(
        phase,
        0.300f,
        0.008f,
        1.00f
    );

    // S wave
    value += gaussian(
        phase,
        0.315f,
        0.010f,
        -0.25f
    );

    // T wave
    value += gaussian(
        phase,
        0.55f,
        0.045f,
        0.30f
    );

    return value;
}

// =====================================================
// Calculate Fourier coefficients
// X[k] = (1/N) sum x[n] e^(-j*2*pi*k*n/N)
// =====================================================

static void calculate_dft(ecg_state_t *chip)
{
    for (uint32_t k = 0; k <= HARMONICS; k++)
    {
        float real = 0.0f;
        float imag = 0.0f;

        for (uint32_t n = 0; n < TEMPLATE_SIZE; n++)
        {
            float phase =
                (float)n / (float)TEMPLATE_SIZE;

            float sample =
                ecg_template(phase);

            float angle =
                2.0f * PI *
                (float)k *
                (float)n /
                (float)TEMPLATE_SIZE;

            real += sample * cosf(angle);
            imag -= sample * sinf(angle);
        }

        chip->real[k] =
            real / (float)TEMPLATE_SIZE;

        chip->imag[k] =
            imag / (float)TEMPLATE_SIZE;
    }
}

// =====================================================
// Reconstruct the real-valued ECG from Fourier terms
// =====================================================

static float inverse_fourier(
    ecg_state_t *chip,
    float phase
)
{
    float result = chip->real[0];

    for (uint32_t k = 1; k <= HARMONICS; k++)
    {
        float angle =
            2.0f * PI * (float)k * phase;

        result += 2.0f *
                  (
                      chip->real[k] * cosf(angle)
                      -
                      chip->imag[k] * sinf(angle)
                  );
    }

    return result;
}

// =====================================================
// Random noise in approximately [-1, +1]
// =====================================================

static float random_noise(void)
{
    return 2.0f *
           (
               (float)rand() /
               (float)RAND_MAX
           )
           - 1.0f;
}

// =====================================================
// Read the current heart-rate slider
// =====================================================

static float get_bpm(ecg_state_t *chip)
{
    uint32_t bpm =
        attr_read(chip->heart_rate_attr);

    // Defensive limits
    if (bpm < 40)
    {
        bpm = 40;
    }
    else if (bpm > 180)
    {
        bpm = 180;
    }

    return (float)bpm;
}

// =====================================================
// Generate one ECG sample
// =====================================================

static void generate_sample(ecg_state_t *chip)
{
    // Read the current slider value.
    chip->bpm = get_bpm(chip);

    // Convert sample index to elapsed time.
    float seconds =
        (float)chip->sample /
        (float)SAMPLE_RATE;

    // Calculate the phase within the heartbeat.
    float beat_phase =
        fmodf(
            seconds * chip->bpm / 60.0f,
            1.0f
        );

    // Reconstruct the ECG waveform.
    float signal =
        inverse_fourier(chip, beat_phase);

    // Measurement noise.
    signal += random_noise() * 0.015f;

    // Baseline wander at 0.33 Hz.
    signal +=
        0.04f *
        sinf(
            2.0f * PI * 0.33f * seconds
        );

    // Map the waveform into a normalized range.
    // The output is limited to the ESP32's 3.3 V range.
    float normalized =
        0.5f + 0.30f * signal;

    if (normalized < 0.05f)
    {
        normalized = 0.05f;
    }

    if (normalized > 0.95f)
    {
        normalized = 0.95f;
    }

    float voltage =
        normalized * 3.3f;

    // Output the analog ECG voltage.
    pin_dac_write(
        chip->ecg_out,
        voltage
    );

    chip->sample++;
}

// =====================================================
// Timer callback: called every 10,000 microseconds
// =====================================================

static void chip_timer_callback(void *user_data)
{
    ecg_state_t *chip =
        (ecg_state_t *)user_data;

    generate_sample(chip);
}

// =====================================================
// Custom chip initialization
// =====================================================

void chip_init(void)
{
    ecg_state_t *chip =
        calloc(1, sizeof(ecg_state_t));

    if (chip == NULL)
    {
        return;
    }

    // Initialize the remaining physical pins.
    pin_init("VCC", INPUT);
    pin_init("GND", INPUT);

    chip->ecg_out =
        pin_init("ECG_OUT", ANALOG);

    // Register the BPM slider.
    // This must be initialized inside chip_init().
    chip->heart_rate_attr =
        attr_init("heartRateBpm", 75);

    chip->sample = 0;
    chip->bpm = 75.0f;

    // Repeatable noise sequence for simulation.
    srand(12345);

    // Calculate the Fourier coefficients once.
    calculate_dft(chip);

    // Configure the periodic timer.
    const timer_config_t timer_config =
    {
        .callback = chip_timer_callback,
        .user_data = chip
    };

    chip->timer =
        timer_init(&timer_config);

    // 100 Hz:
    // T = 1 / 100 = 0.01 seconds = 10,000 us.
    timer_start(
        chip->timer,
        SAMPLE_PERIOD_US,
        true
    );
}