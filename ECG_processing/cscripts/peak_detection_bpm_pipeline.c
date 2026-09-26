#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#define VOLT 5
#define SAMPLES 2000
#define MAX_PEAKS 20

typedef struct
{
    uint16_t val[SAMPLES];

    uint16_t peak_arr[MAX_PEAKS];
    float peak_time[MAX_PEAKS];

    int Fs;

    float time_samples[SAMPLES];

    float avg_bpm;

    int peak_count;

} ECG_samples;


int main()
{
    srand(time(NULL));
    ECG_samples ecg;

    ecg.Fs = 250;
    ecg.avg_bpm = 0.0f;
    ecg.peak_count = 0;


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


    printf("Minimum sample : %d\n", min_sample);
    printf("Maximum sample : %d\n", max_sample);


    /* -------------------------------
       Calculate threshold
       ------------------------------- */

    uint16_t threshold =
        min_sample + 0.8f * (max_sample - min_sample);

    printf("Threshold      : %u\n", threshold);


    /* -------------------------------
       Generate time axis
       ------------------------------- */

    for (int i = 0; i < SAMPLES; i++)
    {
        ecg.time_samples[i] =
            i / (float)ecg.Fs;
    }


    /* -------------------------------
       Detect peaks
       ------------------------------- */

    for (int i = 1; i < SAMPLES - 1; i++)
    {
        int is_local_maximum =
            (ecg.val[i] > ecg.val[i - 1]) &&
            (ecg.val[i] >= ecg.val[i + 1]);

        int above_threshold =
            ecg.val[i] >= threshold;


        if (is_local_maximum && above_threshold)
        {
            if (ecg.peak_count < MAX_PEAKS)
            {
                ecg.peak_arr[ecg.peak_count] =
                    ecg.val[i];

                ecg.peak_time[ecg.peak_count] =
                    ecg.time_samples[i];

                ecg.peak_count++;
            }
        }
    }


    /* -------------------------------
       Print detected peaks
       ------------------------------- */

    printf("\nDetected Peaks: %d\n",
           ecg.peak_count);

    for (int i = 0; i < ecg.peak_count; i++)
    {
        printf(
            "Peak %d : sample = %u, time = %.4f s\n",
            i,
            ecg.peak_arr[i],
            ecg.peak_time[i]
        );
    }


    /* -------------------------------
       Calculate RR intervals and BPM
       ------------------------------- */

    if (ecg.peak_count >= 2)
    {
        float rr_sum = 0.0f;
        int rr_count = 0;

        printf("\nRR intervals and BPM:\n");

        for (int i = 1; i < ecg.peak_count; i++)
        {
            float time_diff =
                ecg.peak_time[i] -
                ecg.peak_time[i - 1];

            float bpm =
                60.0f / time_diff;

            printf(
                "RR[%d] = %.4f s   BPM = %.2f\n",
                i - 1,
                time_diff,
                bpm
            );

            rr_sum += time_diff;
            rr_count++;
        }

        /* -------------------------------
           Average BPM
           ------------------------------- */

        float average_rr =
            rr_sum / rr_count;

        ecg.avg_bpm =
            60.0f / average_rr;

        printf("\nAverage RR  : %.4f s\n",
               average_rr);

        printf("Average BPM : %.2f\n",
               ecg.avg_bpm);
    }
    else
    {
        printf("\nNot enough peaks to calculate BPM.\n");
    }


    return 0;
}