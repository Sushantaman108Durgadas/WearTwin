#include <iostream>
#include <vector>
#include <cmath>
#include <cstdint>

using namespace std;

static constexpr uint16_t BUFFER_SIZE = 2000;
static constexpr float PI = 3.14159265358979323846f;


class IIR_secondorder_lpf_filtering
{
private:

    // Filter parameters
    float wd;
    float wa;
    float Fs;
    float fc;
    float T;

    const int order = 2;

    // Butterworth parameters
    float Q;
    float K;
    float denom;

    // Filter coefficients
    float b0;
    float b1;
    float b2;

    float a0;
    float a1;
    float a2;

    // Input and output buffers
    float (&x)[BUFFER_SIZE];
    float (&y)[BUFFER_SIZE];

    // Previous samples
    float x1 = 0.0f;
    float x2 = 0.0f;

    float y1 = 0.0f;
    float y2 = 0.0f;


public:

    IIR_secondorder_lpf_filtering(
        float Fs,
        float fc,
        float (&samples)[BUFFER_SIZE],
        float (&output)[BUFFER_SIZE]
    )
        : x(samples),
          y(output)
    {
        // -----------------------------
        // Initialize filter parameters
        // -----------------------------

        this->Fs = Fs;
        this->fc = fc;

        T = 1.0f / Fs;

        // Digital angular frequency
        wd = 2.0f * PI * fc;

        // Pre-warped analog angular frequency
        wa = (2.0f / T) * tan(wd * T / 2.0f);

        // Butterworth quality factor
        Q = 1.0f / sqrt(2.0f);

        // Bilinear-transform scaling factor
        K = wa * T / 2.0f;

        // Denominator
        denom =
            1.0f
            + (K / Q)
            + (K * K);

        // Feed-forward coefficients
        b0 = (K * K) / denom;
        b1 = (2.0f * K * K) / denom;
        b2 = (K * K) / denom;

        // Feedback coefficients
        a0 = 1.0f;

        a1 =
            (2.0f * (K * K - 1.0f))
            / denom;

        a2 =
            (1.0f - (K / Q) + (K * K))
            / denom;
    }


    void process()
    {
        for (uint16_t n = 0; n < BUFFER_SIZE; n++)
        {
            float currentInput = x[n];

            float currentOutput =
                b0 * currentInput
                + b1 * x1
                + b2 * x2
                - a1 * y1
                - a2 * y2;

            // Store output
            y[n] = currentOutput;

            // Shift delay elements
            x2 = x1;
            x1 = currentInput;

            y2 = y1;
            y1 = currentOutput;
        }
    }


    float getB0() const
    {
        return b0;
    }

    float getB1() const
    {
        return b1;
    }

    float getB2() const
    {
        return b2;
    }

    float getA1() const
    {
        return a1;
    }

    float getA2() const
    {
        return a2;
    }
};


int main()
{
    float Fs = 250.0f;
    float fc = 35.0f;

    float samples[BUFFER_SIZE] = {};
    float filtered[BUFFER_SIZE] = {};

    // Example input
    for (uint16_t i = 0; i < BUFFER_SIZE; i++)
    {
        samples[i] = sin(
            2.0f * PI * 5.0f * i / Fs
        );
    }

    IIR_secondorder_lpf_filtering filter(
        Fs,
        fc,
        samples,
        filtered
    );

    filter.process();

    cout << "First few filtered samples:\n";

    for (int i = 0; i < 10; i++)
    {
        cout << filtered[i] << '\n';
    }

    return 0;
}