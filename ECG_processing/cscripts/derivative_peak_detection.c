#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <math.h>

#define SAMPLES 2000
#define VOLT 5


typedef struct
{
    uint16_t val[SAMPLES];

    int Fs;

    int16_t derivative[SAMPLES];

    uint16_t threshold;

    int16_t max_derivative;
    int16_t min_derivative;

    uint16_t max_value;
    uint16_t min_value;

    float avg_BPM;
    float avg_rr_interval;

} ECG_samples;


int main()
{
    srand(time(NULL));

    ECG_samples ecg;

    ecg.Fs = 250;

    /* -------------------------------
       Generate ECG samples
       ------------------------------- */

    for (int i = 0; i < SAMPLES; i++)
    {
        ecg.val[i] = rand() % 4032;
    }


    /* -------------------------------
       Find MAX and MIN sample
       ------------------------------- */

    int max_sample = INT_MIN;
    int min_sample = INT_MAX;

    for (int i = 0; i < SAMPLES; i++)
    {
        if (ecg.val[i] < min_sample)
        {
            min_sample = ecg.val[i];
        }

        if (ecg.val[i] > max_sample)
        {
            max_sample = ecg.val[i];
        }
    }

    ecg.max_value = (uint16_t)max_sample;
    ecg.min_value = (uint16_t)min_sample;


    printf("Maximum sample = %u\n", ecg.max_value);
    printf("Minimum sample = %u\n", ecg.min_value);


    /* -------------------------------
       Calculate threshold
       ------------------------------- */

    ecg.threshold =
        (uint16_t)(0.8f * (max_sample - min_sample));

    printf("Threshold = %u\n", ecg.threshold);


    /* -------------------------------
       Calculate derivative
       ------------------------------- */

    ecg.derivative[0] = 0;

    ecg.max_derivative = INT16_MIN;
    ecg.min_derivative = INT16_MAX;

    for (int i = 1; i < SAMPLES; i++)
    {
        /*
         * Cast before subtraction so that
         * negative derivatives are preserved.
         */

        ecg.derivative[i] =
            (int16_t)ecg.val[i] -
            (int16_t)ecg.val[i - 1];


        /* Find derivative maximum */

        if (ecg.derivative[i] > ecg.max_derivative)
        {
            ecg.max_derivative =
                ecg.derivative[i];
        }


        /* Find derivative minimum */

        if (ecg.derivative[i] < ecg.min_derivative)
        {
            ecg.min_derivative =
                ecg.derivative[i];
        }
    }


    printf("Maximum derivative = %d\n",
           ecg.max_derivative);

    printf("Minimum derivative = %d\n",
           ecg.min_derivative);


    /* -------------------------------
       Detect high derivative regions
       ------------------------------- */

    int count = 0;

    float last_peak_time = 0.0f;
    float current_peak_time = 0.0f;

    float rr_sum = 0.0f;
    int rr_count = 0;


    for (int i = 1; i < SAMPLES; i++)
    {
        /*
         * Take absolute value because
         * both rising and falling edges
         * produce large derivatives.
         */

        int derivative_magnitude =
            abs(ecg.derivative[i]);


        if (derivative_magnitude >
            ecg.threshold)
        {
            count++;

            current_peak_time =
                (float)i / ecg.Fs;


            /*
             * First detected derivative event
             */

            if (count == 1)
            {
                last_peak_time =
                    current_peak_time;

                printf(
                    "Candidate detected at sample %d, "
                    "time = %.4f s\n",
                    i,
                    current_peak_time
                );
            }


            /*
             * Subsequent derivative events
             */

            else
            {
                float rr_interval =
                    current_peak_time -
                    last_peak_time;


                rr_sum += rr_interval;
                rr_count++;


                ecg.avg_rr_interval =
                    rr_sum / rr_count;


                ecg.avg_BPM =
                    60.0f /
                    ecg.avg_rr_interval;


                printf(
                    "Candidate detected at sample %d, "
                    "time = %.4f s, "
                    "RR = %.4f s\n",
                    i,
                    current_peak_time,
                    rr_interval
                );

                printf(
                    "Average RR = %.4f s\n",
                    ecg.avg_rr_interval
                );

                printf(
                    "Average BPM = %.2f\n",
                    ecg.avg_BPM
                );


                last_peak_time =
                    current_peak_time;
            }
        }
    }


    /* -------------------------------
       Final result
       ------------------------------- */

    printf("\n============================\n");
    printf("FINAL RESULT\n");
    printf("============================\n");

    printf("Samples          : %d\n",
           SAMPLES);

    printf("Sampling Rate    : %d Hz\n",
           ecg.Fs);

    printf("Min ECG value    : %u\n",
           ecg.min_value);

    printf("Max ECG value    : %u\n",
           ecg.max_value);

    printf("Derivative Min   : %d\n",
           ecg.min_derivative);

    printf("Derivative Max   : %d\n",
           ecg.max_derivative);

    printf("Threshold        : %u\n",
           ecg.threshold);

    printf("Derivative hits  : %d\n",
           count);


    if (rr_count > 0)
    {
        printf("Average RR       : %.4f s\n",
               ecg.avg_rr_interval);

        printf("Average BPM      : %.2f\n",
               ecg.avg_BPM);
    }
    else
    {
        printf("No RR interval calculated.\n");
    }


    return 0;
}