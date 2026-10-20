import os
import numpy as np
import pandas as pd
from pathlib import Path
from scipy.signal import butter, filtfilt

# =========================================================================
# PATH RESOLUTION (Relative to WearTwin project root)
# =========================================================================
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent

HARDWARE_DIR = PROJECT_ROOT / "hardware"
OUTPUT_DIR = HARDWARE_DIR / "output"

OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

INPUT_CSV = HARDWARE_DIR / "rawdata_leftthumb_stationary.csv"
OUTPUT_CSV = OUTPUT_DIR / "processed_ppg_metrics_stationary.csv"

print(f"Loading data from: {INPUT_CSV}")
print(f"Output will be saved to: {OUTPUT_CSV}")

# =========================================================================
# 1. LOAD DATASET
# =========================================================================
df = pd.read_csv(INPUT_CSV)

ts = df['Timestamp_ms'].values
ir = df['IR'].values
red = df['RED'].values

N = len(df)
dt = np.mean(np.diff(ts)) / 1000.0  # Sampling period (~0.0996s => ~10.04 Hz)
Fs = 1.0 / dt                        # Sampling frequency (~10.04 Hz)
t = (ts - ts[0]) / 1000.0            # Time vector in seconds

# =========================================================================
# 2. STAGE 1: 2nd-Order IIR Bandpass Filtering (0.5 Hz - 2.5 Hz)
# =========================================================================
# f_low = 0.5 Hz (30 BPM) -> High-pass for DC offset
# f_high = 2.5 Hz (150 BPM) -> Low-pass safe relative to 5.0 Hz Nyquist
f_low = 0.5
f_high = 2.5
Wn = [f_low, f_high] / (Fs / 2.0)

b_bp, a_bp = butter(2, Wn, btype='bandpass')

# Zero-phase filtering to prevent phase distortion
ir_ac = filtfilt(b_bp, a_bp, ir)
red_ac = filtfilt(b_bp, a_bp, red)

df['IR_AC_Filtered'] = ir_ac
df['RED_AC_Filtered'] = red_ac

# =========================================================================
# 3. STAGE 2: Real-Time Sliding Window Metrics Engine (10s Window)
# =========================================================================
win_size = 100  # 10 seconds at 10 Hz
calc_hr = np.full(N, np.nan)
calc_spo2 = np.full(N, np.nan)

for i in range(win_size, N):
    win_ir_ac = ir_ac[i - win_size : i]
    win_red_ac = red_ac[i - win_size : i]
    win_ir_raw = ir[i - win_size : i]
    win_red_raw = red[i - win_size : i]
    
    # --- Heart Rate via Peak Frequency in FFT Spectrum ---
    freqs = np.fft.rfftfreq(win_size, d=dt)
    fft_mag = np.abs(np.fft.rfft(win_ir_ac))
    
    # Physiological HR band: 0.8 Hz to 2.5 Hz (48 BPM to 150 BPM)
    valid_mask = (freqs >= 0.8) & (freqs <= 2.5)
    
    if np.any(valid_mask) and np.max(fft_mag[valid_mask]) > 0:
        peak_freq = freqs[valid_mask][np.argmax(fft_mag[valid_mask])]
        bpm = peak_freq * 60.0
    else:
        bpm = np.nan
    calc_hr[i] = bpm
    
    # --- SpO2 via Ratio-of-Ratios ---
    dc_ir = np.mean(win_ir_raw)
    dc_red = np.mean(win_red_raw)
    
    # Standard deviation / RMS of AC filtered components
    ac_ir_rms = np.std(win_ir_ac)
    ac_red_rms = np.std(win_red_ac)  # FIXED: uses win_red_ac instead of win_red_raw
    
    if dc_ir > 0 and dc_red > 0 and ac_ir_rms > 0:
        R = (ac_red_rms / dc_red) / (ac_ir_rms / dc_ir)
        spo2 = 110.0 - (25.0 * R)
        spo2 = np.clip(spo2, 70.0, 100.0)
    else:
        spo2 = np.nan
    calc_spo2[i] = spo2

df['Calculated_HR'] = calc_hr
df['Calculated_SpO2'] = calc_spo2

# Back-fill initial warmup window
df['Calculated_HR'] = df['Calculated_HR'].bfill()
df['Calculated_SpO2'] = df['Calculated_SpO2'].bfill()

# =========================================================================
# 4. SAVE PROCESSED DATA TO hardware/output/
# =========================================================================
df.to_csv(OUTPUT_CSV, index=False)
print(f"Success! Processed dataset with {len(df)} samples saved to '{OUTPUT_CSV}'.")