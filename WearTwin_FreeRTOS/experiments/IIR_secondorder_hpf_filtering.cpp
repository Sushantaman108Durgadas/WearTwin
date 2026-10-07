#include <iostream>
#include <cmath>
#include <cstdint>

using namespace std;

static constexpr uint16_t BUFFER_SIZE = 2000;
static constexpr float PI = 3.14159265358979323846f;


class IIR_secondorder_hpf_filtering
{
private:

    float Fs;
    float fc;
    float T;

    float wd;
    float wa;

    float Q;
    float K;
    float denom;

    float b0;
    float b1;
    float b2;

    float a1;
    float a2;

    float (&x)[BUFFER_SIZE];
    float (&y)[BUFFER_SIZE];

    // Previous input/output states
    float x1;
    float x2;

    float y1;
    float y2;


public:

    IIR_secondorder_hpf_filtering(
        float Fs,
        float fc,
        float (&samples)[BUFFER_SIZE],
        float (&output)[BUFFER_SIZE]
    )
        : x(samples),
          y(output)
    {
        this->Fs = Fs;
        this->fc = fc;

        T = 1.0f / Fs;

        // Digital angular frequency
        wd = 2.0f * PI * fc;

        // Pre-warped analog angular frequency
        wa =
            (2.0f / T)
            * tan(wd * T / 2.0f);

        // Butterworth Q
        Q = 1.0f / sqrt(2.0f);

        // Bilinear transform factor
        K = wa * T / 2.0f;

        denom =
            1.0f
            + (K / Q)
            + (K * K);

        // -------------------------
        // High-pass coefficients
        // -------------------------

        b0 = 1.0f / denom;

        b1 = -2.0f / denom;

        b2 = 1.0f / denom;

        a1 =
            (2.0f * (K * K - 1.0f))
            / denom;

        a2 =
            (1.0f - (K / Q) + (K * K))
            / denom;

        // Initial filter states
        x1 = 0.0f;
        x2 = 0.0f;

        y1 = 0.0f;
        y2 = 0.0f;
    }


    void process()
    {
        // --------------------------------
        // Calculate initial DC value
        // Equivalent to:
        //
        // dc_init = mean(y_lp(1:10))
        // --------------------------------

        float dc_init = 0.0f;

        const uint16_t warmupSamples = 10;

        for (uint16_t i = 0; i < warmupSamples; i++)
        {
            dc_init += x[i];
        }

        dc_init /= warmupSamples;


        // --------------------------------
        // Initialize previous input states
        // --------------------------------

        x1 = dc_init;
        x2 = dc_init;

        y1 = 0.0f;
        y2 = 0.0f;


        // --------------------------------
        // Filtering
        // --------------------------------

        for (uint16_t n = 0; n < BUFFER_SIZE; n++)
        {
            float currentInput = x[n];

            float currentOutput =
                b0 * currentInput
                + b1 * x1
                + b2 * x2
                - a1 * y1
                - a2 * y2;

            y[n] = currentOutput;


            // Shift states

            x2 = x1;
            x1 = currentInput;

            y2 = y1;
            y1 = currentOutput;
        }
    }
};


int main()
{
    float Fs = 250.0f;
    float fc = 0.5f;

    float samples[BUFFER_SIZE] = {};
    float filtered[BUFFER_SIZE] = {};

    // Example input
    for (uint16_t i = 0; i < BUFFER_SIZE; i++)
    {
        samples[i] = sin(
            2.0f * PI * 5.0f * i / Fs
        );
    }

    IIR_secondorder_hpf_filtering highPass(
        Fs,
        fc,
        y_lp,
        y_clean
    );

    highPass.process();

    return 0;
}