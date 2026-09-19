# ECG Signal Processing

This project implements a basic ECG signal-processing pipeline using GNU Octave.

The objective is to analyze a synthetic ECG signal in both the time and
frequency domains and remove 50 Hz power-line interference using a
window-based FIR band-reject filter.

## Pipeline

Synthetic ECG
     ↓
Time-domain visualization
     ↓
FFT
     ↓
Frequency-domain analysis
     ↓
50 Hz interference identification
     ↓
Ideal band-reject filter design
     ↓
Rectangular window
     ↓
FIR filter
     ↓
Convolution with ECG
     ↓
Filtered ECG

## Directory Structure

```text
ECG_processing/
│
├── octavescripts/
│   ├── signal_acquistion_freqtransform.m
│   ├── band_reject_impulse.m
│   ├── FIR_filtering.m
│   └── Filtered_ECG.m
│
├── outputs/
│   ├── original_ecg.png
│   ├── frequency_spectrum.png
│   ├── ideal_band_reject.png
│   ├── fir_filter.png
│   └── filtered_ecg.png
│
└── README.md
