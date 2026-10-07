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

#define SAMPLE_RATE 250
#define SAMPLE_PERIOD_US (1000000 / SAMPLE_RATE)


// =====================================================
// Heart-rate variability configuration
// =====================================================

/*
 * Maximum beat-to-beat RR variation.
 *
 * 0.03 = ±3%
 *
 * Example at 75 BPM:
 *
 * Nominal RR = 0.800 s
 *
 * Possible RR:
 *
 * 0.776 s -> ~77.3 BPM
 * 0.824 s -> ~72.8 BPM
 */
#define RR_VARIATION 0.03f


/*
 * How strongly the new RR value follows
 * the previous RR value.
 *
 * Higher value = smoother physiological variation.
 *
 * 0.85 means:
 *
 * new RR = 85% previous RR
 *        + 15% new random target
 */
#define RR_SMOOTHING 0.85f


// =====================================================
// ECG simulator state
// =====================================================

typedef struct
{
    // -------------------------------------------------
    // Mode input pins
    // -------------------------------------------------

    pin_t mode0;
    pin_t mode1;
    pin_t mode2;


    // -------------------------------------------------
    // ECG analog output
    // -------------------------------------------------

    pin_t ecg_out;


    // -------------------------------------------------
    // Timer
    // -------------------------------------------------

    timer_t timer;


    // -------------------------------------------------
    // Fourier coefficients
    //
    // real[k] + j*imag[k]
    // -------------------------------------------------

    float real[TEMPLATE_SIZE];
    float imag[TEMPLATE_SIZE];


    // -------------------------------------------------
    // Current generated sample
    // -------------------------------------------------

    uint32_t sample;


    // -------------------------------------------------
    // Nominal heart rate selected by mode
    // -------------------------------------------------

    float bpm;


    // -------------------------------------------------
    // Beat-to-beat timing
    // -------------------------------------------------

    /*
     * Current heartbeat interval in samples.
     *
     * Example:
     *
     * 75 BPM -> approximately 200 samples
     */
    float beat_interval_samples;


    /*
     * Position inside the current heartbeat.
     */
    float beat_position;


    /*
     * Current RR interval in seconds.
     */
    float current_rr;


    /*
     * Target RR interval generated for
     * the next heartbeat.
     */
    float target_rr;


    // -------------------------------------------------
    // Beat-to-beat morphology variation
    // -------------------------------------------------

    /*
     * Small amplitude variation applied to
     * each heartbeat.
     *
     * Normally close to 1.0.
     */
    float beat_amplitude;


} ecg_state_t;


// =====================================================
// Gaussian function
//
// Used to construct P, Q, R, S and T waves.
// =====================================================

static float gaussian(
    float x,
    float center,
    float width,
    float amplitude
)
{
    float d =
        x - center;

    return amplitude *
           expf(
               -(d * d) /
               (2.0f * width * width)
           );
}


// =====================================================
// Idealized ECG template
//
// phase:
// 0.0 -> beginning of heartbeat
// 1.0 -> end of heartbeat
//
// P -> Q -> R -> S -> T
// =====================================================

static float ecg_template(float phase)
{
    float value = 0.0f;


    // -------------------------------------------------
    // P wave
    // -------------------------------------------------

    value += gaussian(
        phase,
        0.18f,
        0.025f,
        0.18f
    );


    // -------------------------------------------------
    // Q wave
    // -------------------------------------------------

    value += gaussian(
        phase,
        0.285f,
        0.010f,
        -0.12f
    );


    // -------------------------------------------------
    // R wave
    // -------------------------------------------------

    value += gaussian(
        phase,
        0.300f,
        0.008f,
        1.00f
    );


    // -------------------------------------------------
    // S wave
    // -------------------------------------------------

    value += gaussian(
        phase,
        0.315f,
        0.010f,
        -0.25f
    );


    // -------------------------------------------------
    // T wave
    // -------------------------------------------------

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
//
// X[k] = (1/N) sum x[n] e^(-j2*pi*k*n/N)
//
// Only the required harmonics are calculated.
// =====================================================

static void calculate_dft(ecg_state_t *chip)
{
    for (
        uint32_t k = 0;
        k <= HARMONICS;
        k++
    )
    {
        float real = 0.0f;
        float imag = 0.0f;


        for (
            uint32_t n = 0;
            n < TEMPLATE_SIZE;
            n++
        )
        {
            // -----------------------------------------
            // Time-domain phase
            // -----------------------------------------

            float phase =
                (float)n /
                (float)TEMPLATE_SIZE;


            float sample =
                ecg_template(phase);


            // -----------------------------------------
            // Fourier angle
            // -----------------------------------------

            float angle =
                2.0f *
                PI *
                (float)k *
                (float)n /
                (float)TEMPLATE_SIZE;


            // -----------------------------------------
            // Real part
            // -----------------------------------------

            real +=
                sample *
                cosf(angle);


            // -----------------------------------------
            // Imaginary part
            //
            // e^(-jtheta)
            // = cos(theta) - j sin(theta)
            // -----------------------------------------

            imag -=
                sample *
                sinf(angle);
        }


        // ---------------------------------------------
        // Normalize by N
        // ---------------------------------------------

        chip->real[k] =
            real /
            (float)TEMPLATE_SIZE;


        chip->imag[k] =
            imag /
            (float)TEMPLATE_SIZE;
    }
}


// =====================================================
// Inverse Fourier reconstruction
//
// ECG is real-valued, therefore:
//
// X[-k] = conjugate(X[k])
//
// Hence:
//
// x(t) = X0
//      + 2 * sum Re{Xk * e^(j2*pi*k*t)}
// =====================================================

static float inverse_fourier(
    ecg_state_t *chip,
    float phase
)
{
    float result =
        chip->real[0];


    for (
        uint32_t k = 1;
        k <= HARMONICS;
        k++
    )
    {
        float angle =
            2.0f *
            PI *
            (float)k *
            phase;


        result +=
            2.0f *
            (
                chip->real[k] *
                cosf(angle)
                -
                chip->imag[k] *
                sinf(angle)
            );
    }


    return result;
}


// =====================================================
// Random noise
//
// Returns approximately:
// -1.0 -> +1.0
// =====================================================

static float random_noise(void)
{
    return
        2.0f *
        (
            (float)rand() /
            (float)RAND_MAX
        )
        - 1.0f;
}


// =====================================================
// Determine ECG mode
//
// MODE0 -> 55 BPM
// MODE1 -> 75 BPM
// MODE2 -> 110 BPM
// =====================================================

static float get_bpm(ecg_state_t *chip)
{
    if (
        pin_read(chip->mode0) ==
        HIGH
    )
    {
        return 55.0f;
    }


    if (
        pin_read(chip->mode1) ==
        HIGH
    )
    {
        return 75.0f;
    }


    if (
        pin_read(chip->mode2) ==
        HIGH
    )
    {
        return 110.0f;
    }


    // Default to normal heart rate
    return 75.0f;
}


// =====================================================
// Generate random RR interval
//
// The mode BPM is the nominal heart rate.
//
// We introduce a small physiological variation around
// the nominal RR interval.
//
// Example:
//
// 75 BPM
// nominal RR = 0.800 s
//
// RR may become:
//
// 0.786 s
// 0.808 s
// 0.794 s
// 0.817 s
//
// but remains close to 75 BPM overall.
// =====================================================

static float generate_target_rr(ecg_state_t *chip)
{
    float nominal_rr =
        60.0f /
        chip->bpm;


    /*
     * Random value:
     *
     * approximately -1.0 -> +1.0
     */
    float variation =
        random_noise();


    /*
     * Apply ±RR_VARIATION.
     */
    float target =
        nominal_rr *
        (
            1.0f +
            variation *
            RR_VARIATION
        );


    return target;
}


// =====================================================
// Start / update heartbeat
//
// Called whenever the current heartbeat finishes.
// =====================================================

static void start_next_beat(ecg_state_t *chip)
{
    /*
     * Read the current mode.
     *
     * This means changing the physical switch
     * changes the nominal heart rate.
     */
    chip->bpm =
        get_bpm(chip);


    /*
     * Generate a new target RR interval.
     */
    chip->target_rr =
        generate_target_rr(chip);


    /*
     * Smooth the transition between RR intervals.
     *
     * This avoids abrupt random jumps.
     */
    chip->current_rr =
        (
            RR_SMOOTHING *
            chip->current_rr
        )
        +
        (
            (1.0f - RR_SMOOTHING) *
            chip->target_rr
        );


    /*
     * Convert RR seconds into samples.
     */
    chip->beat_interval_samples =
        chip->current_rr *
        (float)SAMPLE_RATE;


    /*
     * Reset position inside the new beat.
     */
    chip->beat_position =
        0.0f;


    /*
     * Small beat-to-beat amplitude variation.
     *
     * Approximately ±2%.
     */
    chip->beat_amplitude =
        1.0f +
        random_noise() *
        0.02f;
}


// =====================================================
// Generate one ECG sample
// =====================================================

static void generate_sample(ecg_state_t *chip)
{
    // -------------------------------------------------
    // Check whether current heartbeat is complete
    // -------------------------------------------------

    if (
        chip->beat_position >=
        chip->beat_interval_samples
    )
    {
        start_next_beat(chip);
    }


    // -------------------------------------------------
    // Calculate phase inside current heartbeat
    // -------------------------------------------------

    float beatPhase =
        chip->beat_position /
        chip->beat_interval_samples;


    /*
     * Safety clamp.
     */
    if (beatPhase < 0.0f)
        beatPhase = 0.0f;


    if (beatPhase >= 1.0f)
        beatPhase = 0.999999f;


    // -------------------------------------------------
    // Reconstruct ECG from Fourier coefficients
    // -------------------------------------------------

    float signal =
        inverse_fourier(
            chip,
            beatPhase
        );


    // -------------------------------------------------
    // Beat-to-beat amplitude variation
    // -------------------------------------------------

    signal *=
        chip->beat_amplitude;


    // -------------------------------------------------
    // Random measurement noise
    // -------------------------------------------------

    signal +=
        random_noise() *
        0.020f;


    // -------------------------------------------------
    // Baseline wander
    //
    // 0.33 Hz
    // -------------------------------------------------

    float seconds =
        (float)chip->sample /
        (float)SAMPLE_RATE;


    signal +=
        0.04f *
        sinf(
            2.0f *
            PI *
            0.33f *
            seconds
        );


    // -------------------------------------------------
    // 50 Hz power-line interference
    // -------------------------------------------------

    signal +=
        0.025f *
        sinf(
            2.0f *
            PI *
            50.0f *
            seconds
        );


    // -------------------------------------------------
    // Map signal approximately into 0 -> 1
    // -------------------------------------------------

    float normalized =
        0.5f +
        0.30f *
        signal;


    // -------------------------------------------------
    // Clamp
    // -------------------------------------------------

    if (
        normalized < 0.05f
    )
    {
        normalized = 0.05f;
    }


    if (
        normalized > 0.95f
    )
    {
        normalized = 0.95f;
    }


    // -------------------------------------------------
    // Convert to voltage
    // -------------------------------------------------

    float voltage =
        normalized *
        5.0f;


    // -------------------------------------------------
    // Write analog voltage
    // -------------------------------------------------

    pin_dac_write(
        chip->ecg_out,
        voltage
    );


    // -------------------------------------------------
    // Advance sample and heartbeat position
    // -------------------------------------------------

    chip->sample++;

    chip->beat_position +=
        1.0f;
}


// =====================================================
// Timer callback
// =====================================================

static void chip_timer_callback(
    void *user_data
)
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
    // -------------------------------------------------
    // Allocate chip state
    // -------------------------------------------------

    ecg_state_t *chip =
        calloc(
            1,
            sizeof(ecg_state_t)
        );


    // -------------------------------------------------
    // Initialize mode pins
    // -------------------------------------------------

    chip->mode0 =
        pin_init(
            "MODE0",
            INPUT_PULLDOWN
        );


    chip->mode1 =
        pin_init(
            "MODE1",
            INPUT_PULLDOWN
        );


    chip->mode2 =
        pin_init(
            "MODE2",
            INPUT_PULLDOWN
        );


    // -------------------------------------------------
    // Initialize ECG analog output
    // -------------------------------------------------

    chip->ecg_out =
        pin_init(
            "ECG_OUT",
            ANALOG
        );


    // -------------------------------------------------
    // Initialize state
    // -------------------------------------------------

    chip->sample = 0;

    chip->bpm = 75.0f;


    /*
     * Start with nominal 75 BPM RR.
     *
     * 60 / 75 = 0.8 seconds
     */
    chip->current_rr =
        60.0f /
        75.0f;


    chip->target_rr =
        chip->current_rr;


    chip->beat_interval_samples =
        chip->current_rr *
        (float)SAMPLE_RATE;


    chip->beat_position =
        0.0f;


    chip->beat_amplitude =
        1.0f;


    // -------------------------------------------------
    // Deterministic random seed
    // -------------------------------------------------

    srand(12345);


    // -------------------------------------------------
    // Calculate Fourier coefficients once
    //
    // We do NOT perform the DFT for every sample.
    // -------------------------------------------------

    calculate_dft(chip);


    // -------------------------------------------------
    // Timer configuration
    // -------------------------------------------------

    const timer_config_t timer_config =
    {
        .callback = chip_timer_callback,
        .user_data = chip
    };


    chip->timer =
        timer_init(
            &timer_config
        );


    // -------------------------------------------------
    // Start timer
    //
    // 250 Hz:
    //
    // T = 1 / 250
    //   = 0.004 s
    //   = 4000 us
    // -------------------------------------------------

    timer_start(
        chip->timer,
        SAMPLE_PERIOD_US,
        true
    );
}