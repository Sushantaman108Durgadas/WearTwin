#include <iostream>
#include <cmath>
#include <cstdint>

using namespace std;

static constexpr float PI = 3.14159265358979323846f;


class FIRBandReject
{
private:

    float Fs;
    float fc1;
    float fc2;

    uint16_t M;
    float shift;

    float* h;


public:

    FIRBandReject(
        float Fs,
        float fc1,
        float fc2,
        uint16_t M
    )
        : Fs(Fs),
          fc1(fc1),
          fc2(fc2),
          M(M)
    {
        shift = (M - 1) / 2.0f;

        h = new float[M];

        designFilter();
    }


    ~FIRBandReject()
    {
        delete[] h;
    }


private:

    void designFilter()
    {
        // Convert cutoff frequencies from Hz
        // to digital angular frequencies

        float wc1 =
            2.0f * PI * fc1 / Fs;

        float wc2 =
            2.0f * PI * fc2 / Fs;


        for (uint16_t n = 0; n < M; n++)
        {
            float k = n - shift;

            if (k != 0.0f)
            {
                h[n] =
                    sin(wc1 * k) / (PI * k)
                    -
                    sin(wc2 * k) / (PI * k);
            }
            else
            {
                h[n] =
                    1.0f
                    -
                    (wc2 - wc1) / PI;
            }
        }
    }


public:

    float processSample(float sample)
    {
        // This function is not enough by itself yet.
        // We need a delay line for real-time FIR processing.

        return sample;
    }


    float getCoefficient(uint16_t index) const
    {
        if (index >= M)
            return 0.0f;

        return h[index];
    }


    uint16_t getLength() const
    {
        return M;
    }
};