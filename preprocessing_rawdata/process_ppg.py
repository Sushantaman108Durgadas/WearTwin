"""
WearTwin - PPG Preprocessing Pipeline

Features:
    1. Read IR and RED PPG signals from an input CSV.
    2. Validate timestamps and signal columns.
    3. Resample irregularly sampled data onto a uniform time grid.
    4. Apply a Butterworth bandpass filter.
    5. Estimate heart rate using FFT.
    6. Estimate SpO2 using a ratio-of-ratios approximation.
    7. Save processed signals and metrics to an output CSV.

Usage:
    python preprocessing_rawdata/process_ppg.py INPUT.csv OUTPUT.csv

Optional arguments:
    --window 10
    --low 0.5
    --high 2.5
    --target-fs 10
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.signal import butter, sosfiltfilt


# ============================================================
# ARGUMENT PARSING
# ============================================================

def parse_args():
    parser = argparse.ArgumentParser(
        description="WearTwin PPG preprocessing and HR/SpO2 estimation."
    )

    parser.add_argument(
        "input_csv",
        type=Path,
        help="Path to the raw PPG CSV file."
    )

    parser.add_argument(
        "output_csv",
        type=Path,
        help="Path where the processed CSV will be saved."
    )

    parser.add_argument(
        "--window",
        type=float,
        default=10.0,
        help="Analysis window duration in seconds (default: 10)."
    )

    parser.add_argument(
        "--low",
        type=float,
        default=0.5,
        help="Bandpass lower cutoff frequency in Hz (default: 0.5)."
    )

    parser.add_argument(
        "--high",
        type=float,
        default=2.5,
        help="Bandpass upper cutoff frequency in Hz (default: 2.5)."
    )

    parser.add_argument(
        "--target-fs",
        type=float,
        default=10.0,
        help="Uniform resampling frequency in Hz (default: 10)."
    )

    return parser.parse_args()


# ============================================================
# INPUT VALIDATION
# ============================================================

def load_ppg_data(input_csv: Path) -> pd.DataFrame:
    """Load and validate timestamp, IR, and RED columns."""

    if not input_csv.is_file():
        raise FileNotFoundError(
            f"Input CSV does not exist: {input_csv}"
        )

    df = pd.read_csv(input_csv)

    required_columns = ["Timestamp_ms", "IR", "RED"]

    missing = [
        column
        for column in required_columns
        if column not in df.columns
    ]

    if missing:
        raise ValueError(
            f"Missing required columns: {missing}"
        )

    df = df[required_columns].copy()

    for column in required_columns:
        df[column] = pd.to_numeric(
            df[column],
            errors="coerce"
        )

    df = df.dropna(subset=required_columns)

    if len(df) < 10:
        raise ValueError(
            "Insufficient valid samples in the input CSV."
        )

    if not np.isfinite(df[required_columns].to_numpy()).all():
        raise ValueError(
            "Input contains non-finite values."
        )

    # Sort by timestamp and collapse duplicate timestamps.
    df = df.sort_values("Timestamp_ms")

    df = (
        df.groupby("Timestamp_ms", as_index=False)
        .agg({"IR": "mean", "RED": "mean"})
    )

    timestamps = df["Timestamp_ms"].to_numpy(dtype=float)

    if len(timestamps) < 10:
        raise ValueError(
            "Insufficient unique timestamps after cleanup."
        )

    if np.any(np.diff(timestamps) <= 0):
        raise ValueError(
            "Timestamps must be strictly increasing."
        )

    return df


# ============================================================
# UNIFORM RESAMPLING
# ============================================================

def resample_signals(
    df: pd.DataFrame,
    target_fs: float
):
    """
    Interpolate signals onto a uniform time grid.

    Filtering and FFT analysis assume uniform sampling.
    The original irregular timestamps are not used directly
    for those operations.
    """

    timestamps_ms = df["Timestamp_ms"].to_numpy(dtype=float)

    ir = df["IR"].to_numpy(dtype=float)
    red = df["RED"].to_numpy(dtype=float)

    time_s = (timestamps_ms - timestamps_ms[0]) / 1000.0

    duration_s = time_s[-1]

    if duration_s <= 0:
        raise ValueError("Recording duration must be positive.")

    # Uniform grid at the selected sampling rate.
    uniform_time = np.arange(
        0.0,
        duration_s,
        1.0 / target_fs
    )

    if len(uniform_time) < 10:
        raise ValueError(
            "Recording is too short for resampling."
        )

    ir_uniform = np.interp(
        uniform_time,
        time_s,
        ir
    )

    red_uniform = np.interp(
        uniform_time,
        time_s,
        red
    )

    return uniform_time, ir_uniform, red_uniform


# ============================================================
# BUTTERWORTH BANDPASS FILTER
# ============================================================

def bandpass_filter(
    signal: np.ndarray,
    fs: float,
    low_cutoff: float,
    high_cutoff: float
) -> np.ndarray:
    """Apply a zero-phase, second-order Butterworth bandpass."""

    nyquist = fs / 2.0

    if not (
        0.0 < low_cutoff < high_cutoff < nyquist
    ):
        raise ValueError(
            "Cutoffs must satisfy "
            "0 < low < high < sampling_rate / 2. "
            f"Received low={low_cutoff}, high={high_cutoff}, "
            f"Nyquist={nyquist}."
        )

    if len(signal) < 20:
        raise ValueError(
            "Signal is too short for reliable filtering."
        )

    sos = butter(
        2,
        [low_cutoff, high_cutoff],
        btype="bandpass",
        fs=fs,
        output="sos"
    )

    return sosfiltfilt(sos, signal)


# ============================================================
# HEART RATE ESTIMATION
# ============================================================

def estimate_heart_rate(
    ir_window: np.ndarray,
    fs: float,
    low_cutoff: float = 0.8,
    high_cutoff: float = 2.5
) -> float:
    """
    Estimate heart rate from the dominant IR frequency.

    Returns NaN if the window does not contain enough samples
    or no frequency lies in the configured heart-rate band.
    """

    if len(ir_window) < 4:
        return np.nan

    # Remove DC offset.
    signal = ir_window - np.mean(ir_window)

    # Reduce spectral leakage.
    window = np.hanning(len(signal))
    signal_windowed = signal * window

    spectrum = np.abs(
        np.fft.rfft(signal_windowed)
    )

    frequencies = np.fft.rfftfreq(
        len(signal_windowed),
        d=1.0 / fs
    )

    valid = (
        (frequencies >= low_cutoff)
        & (frequencies <= min(high_cutoff, fs / 2.0))
    )

    if not np.any(valid):
        return np.nan

    valid_indices = np.flatnonzero(valid)

    peak_index = valid_indices[
        np.argmax(spectrum[valid])
    ]

    peak_frequency = frequencies[peak_index]

    return float(peak_frequency * 60.0)


# ============================================================
# SPO2 ESTIMATION
# ============================================================

def estimate_spo2(
    ir_window: np.ndarray,
    red_window: np.ndarray
) -> float:
    """
    Illustrative ratio-of-ratios SpO2 estimate.

    IMPORTANT:
    The equation used here is NOT calibrated for medical use.
    It is only an experimental signal-processing feature.
    """

    ir_dc = float(np.mean(ir_window))
    red_dc = float(np.mean(red_window))

    ir_ac = float(np.std(ir_window))
    red_ac = float(np.std(red_window))

    if (
        abs(ir_dc) < 1e-9
        or abs(red_dc) < 1e-9
        or ir_ac < 1e-9
        or red_ac < 1e-9
    ):
        return np.nan

    ratio = (red_ac / red_dc) / (ir_ac / ir_dc)

    # Illustrative formula only; requires sensor-specific
    # calibration and comparison with a validated reference.
    spo2 = 110.0 - 25.0 * ratio

    return float(np.clip(spo2, 70.0, 100.0))


# ============================================================
# WINDOWED METRIC CALCULATION
# ============================================================

def calculate_metrics(
    time_s: np.ndarray,
    ir_filtered: np.ndarray,
    red_filtered: np.ndarray,
    ir_raw: np.ndarray,
    red_raw: np.ndarray,
    fs: float,
    window_seconds: float
) -> pd.DataFrame:
    """
    Calculate windowed HR and SpO2 estimates.

    Metrics are emitted at each sample once a complete analysis
    window is available. Earlier rows remain NaN rather than
    being backfilled using future data.
    """

    window_samples = int(round(window_seconds * fs))

    if window_samples < 4:
        raise ValueError(
            "Analysis window must contain at least 4 samples."
        )

    count = len(time_s)

    heart_rate = np.full(count, np.nan)
    spo2 = np.full(count, np.nan)

    for end in range(window_samples - 1, count):
        start = end - window_samples + 1

        ir_segment = ir_filtered[start:end + 1]
        red_segment = red_filtered[start:end + 1]

        heart_rate[end] = estimate_heart_rate(
            ir_segment,
            fs
        )

        # Use raw DC levels and AC amplitudes from the
        # bandpass-filtered signals.
        spo2[end] = estimate_spo2(
            ir_raw[start:end + 1],
            red_raw[start:end + 1]
        )

    return pd.DataFrame({
        "Timestamp_s": time_s,
        "IR_Raw": ir_raw,
        "RED_Raw": red_raw,
        "IR_Filtered": ir_filtered,
        "RED_Filtered": red_filtered,
        "HR_BPM_Estimate": heart_rate,
        "SpO2_Percent_Estimate": spo2
    })


# ============================================================
# MAIN PIPELINE
# ============================================================

def main():
    args = parse_args()

    input_csv = args.input_csv.resolve()
    output_csv = args.output_csv.resolve()

    if args.window <= 0:
        raise ValueError(
            "--window must be greater than zero."
        )

    if args.target_fs <= 0:
        raise ValueError(
            "--target-fs must be greater than zero."
        )

    print("=" * 60)
    print("WearTwin PPG Preprocessing")
    print("=" * 60)

    print(f"Input CSV : {input_csv}")
    print(f"Output CSV: {output_csv}")

    # 1. Load and validate.
    df = load_ppg_data(input_csv)

    original_timestamps = df["Timestamp_ms"].to_numpy(
        dtype=float
    )

    original_dt = np.diff(original_timestamps) / 1000.0

    print(f"\nOriginal samples : {len(df)}")
    print(
        f"Original duration: "
        f"{(original_timestamps[-1] - original_timestamps[0]) / 1000.0:.2f} s"
    )
    print(
        f"Mean original dt : {np.mean(original_dt) * 1000.0:.2f} ms"
    )
    print(
        f"Median original dt: {np.median(original_dt) * 1000.0:.2f} ms"
    )

    # 2. Resample to uniform sampling.
    time_s, ir_raw, red_raw = resample_signals(
        df,
        args.target_fs
    )

    fs = args.target_fs

    print(f"\nUniform sampling rate: {fs:.3f} Hz")
    print(f"Resampled samples    : {len(time_s)}")
    print(f"Window duration      : {args.window:.2f} s")
    print(
        f"Window samples       : {int(round(args.window * fs))}"
    )
    print(
        f"Bandpass range       : {args.low:.2f}–{args.high:.2f} Hz"
    )

    # 3. Filter both PPG channels.
    ir_filtered = bandpass_filter(
        ir_raw,
        fs,
        args.low,
        args.high
    )

    red_filtered = bandpass_filter(
        red_raw,
        fs,
        args.low,
        args.high
    )

    # 4. Calculate windowed metrics.
    result = calculate_metrics(
        time_s=time_s,
        ir_filtered=ir_filtered,
        red_filtered=red_filtered,
        ir_raw=ir_raw,
        red_raw=red_raw,
        fs=fs,
        window_seconds=args.window
    )

    # 5. Save output.
    output_csv.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    result.to_csv(
        output_csv,
        index=False,
        float_format="%.6f"
    )

    print("\nProcessing complete.")
    print(f"Saved output: {output_csv}")
    print(f"Output rows : {len(result)}")

    valid_hr = result["HR_BPM_Estimate"].dropna()
    valid_spo2 = result["SpO2_Percent_Estimate"].dropna()

    if not valid_hr.empty:
        print(
            f"Mean estimated HR  : {valid_hr.mean():.2f} BPM"
        )
    else:
        print("Mean estimated HR  : unavailable")

    if not valid_spo2.empty:
        print(
            f"Mean estimated SpO2: {valid_spo2.mean():.2f}%"
        )
    else:
        print("Mean estimated SpO2: unavailable")

    print(
        "\nReminder: these HR and SpO2 outputs are experimental "
        "estimates, not validated medical measurements."
    )


if __name__ == "__main__":
    try:
        main()
    except (ValueError, FileNotFoundError, OSError) as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
        sys.exit(1)