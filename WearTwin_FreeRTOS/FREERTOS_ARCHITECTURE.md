# WearTwin FreeRTOS architecture

The ESP32 firmware now uses FreeRTOS tasks so sensor acquisition is independent
of the slower windowed ECG analysis.

```text
                         ESP32
                           |
          +----------------+----------------+
          |                |                |
          v                v                v
     ECG_Acquire        IMU_Task        Telemetry
       250 Hz            100 Hz           1 Hz
          |                |                |
          v                v                v
     triple buffer       Kalman        latest state
          |
          v
     ECG_Process
     low priority
          |
          +--> 2000-sample DSP
          +--> R peaks
          +--> RR / BPM
          |
          v
    timestamped window
```

## Why the ECG uses multiple buffers

The ECG acquisition task must not process a 2000-sample window itself. A
completed window is placed in a FreeRTOS queue and its buffer is returned to a
pool only after the processing task finishes with it.

Four buffers are used so that, during normal operation, acquisition can own
one buffer while processing owns another and completed buffers wait in the
queue.

## Timing

- ECG acquisition: 250 Hz, 4 ms period
- ECG analysis: one 2000-sample window = 8 s of physiological data
- IMU task: 100 Hz
- Telemetry: 1 Hz

The 8-second ECG window is a data interval, not an 8-second CPU block. The
other tasks continue running while the ECG processing task analyses the window.

Each completed ECG window carries `startUs` and `endUs`, allowing later
multimodal temporal alignment instead of treating the result as a timeless
number.

## Task priorities

- `ECG_Acquire`: priority 3, core 1
- `IMU_Task`: priority 2, core 1
- `ECG_Process`: priority 1, core 0
- `Telemetry`: priority 1, core 0

The exact priorities are engineering choices for this prototype, not clinical
or production-certified scheduling values.
