import pandas as pd
import numpy as np
from pathlib import Path

STATIONARY = Path("hardware/rawdata_leftthumb_stationary.csv")
MOVING = Path("hardware/rawdata_leftthumb_motion.csv")
OUT = Path("hardware/rawdata_leftthumb_motion_clean.csv")

def read_csv(path):
    # Keep original timestamp text so corrupted timestamp strings can be identified.
    df = pd.read_csv(path, dtype={"Timestamp_ms": "string"})
    for c in ["IR", "RED", "SpO2", "SpO2_Valid", "HR_Algorithm", "HR_Valid"]:
        if c in df:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    df["_timestamp_numeric"] = pd.to_numeric(df["Timestamp_ms"], errors="coerce")
    return df

stationary = read_csv(STATIONARY)
moving = read_csv(MOVING)

print("=== INPUT SUMMARY ===")
print(f"Stationary rows: {len(stationary)}")
print(f"Moving rows:     {len(moving)}")

# Find timestamp reset(s): a valid timestamp lower than the preceding valid timestamp.
t = moving["_timestamp_numeric"]
reset_positions = np.flatnonzero((t.diff() < 0).fillna(False).to_numpy())

if len(reset_positions):
    # This dataset has a clear reset. Keep the final segment after the last reset.
    start = int(reset_positions[-1])
    print(f"\nDetected {len(reset_positions)} timestamp reset(s).")
    print(f"Keeping the final recording segment starting at CSV row {start + 1} (1-based).")
    candidate = moving.iloc[start:].copy()
else:
    print("\nNo timestamp reset detected; considering the entire moving file.")
    candidate = moving.copy()

# Report if candidate segment's IR/RED values match stationary values row-by-row.
shared = min(len(stationary), len(candidate))
if shared:
    same_ir = candidate["IR"].iloc[:shared].reset_index(drop=True).equals(
        stationary["IR"].iloc[:shared].reset_index(drop=True)
    )
    same_red = candidate["RED"].iloc[:shared].reset_index(drop=True).equals(
        stationary["RED"].iloc[:shared].reset_index(drop=True)
    )
    print(f"Candidate segment first {shared} rows IR exactly match stationary prefix: {same_ir}")
    print(f"Candidate segment first {shared} rows RED exactly match stationary prefix: {same_red}")

before = len(candidate)
invalid_ts = candidate["_timestamp_numeric"].isna()
print(f"\nRows in chosen segment before timestamp cleaning: {before}")
print(f"Rows with invalid/corrupted timestamps: {int(invalid_ts.sum())}")
if invalid_ts.any():
    print("Invalid timestamp rows (original row numbers are 1-based):")
    for idx, value in candidate.loc[invalid_ts, "Timestamp_ms"].items():
        print(f"  row {idx + 1}: {value!r}")

# Drop rows whose timestamp cannot be parsed.
clean = candidate.loc[~invalid_ts].copy()

# Sort chronologically by timestamp; do not blindly remove same-time rows yet.
clean = clean.sort_values("_timestamp_numeric", kind="stable")

# Diagnose repeated timestamps and exact repeated measurements.
dup_ts = clean["_timestamp_numeric"].duplicated(keep=False)
print(f"\nRows sharing a timestamp with another row: {int(dup_ts.sum())}")
print(f"Excess rows with repeated timestamps: {int(clean['_timestamp_numeric'].duplicated().sum())}")
if dup_ts.any():
    print("Repeated-timestamp rows (inspect before deciding whether to remove):")
    print(clean.loc[dup_ts, ["Timestamp_ms", "IR", "RED", "SpO2", "HR_Algorithm"]].to_string(index=True))

measurement_cols = [c for c in ["IR", "RED"] if c in clean.columns]
if measurement_cols:
    exact_adjacent = clean[measurement_cols].eq(clean[measurement_cols].shift()).all(axis=1)
    print(f"Rows with IR/RED identical to immediately previous row after sorting: {int(exact_adjacent.sum())}")

# Remove exact duplicate records only; do NOT drop timestamp duplicates automatically.
data_cols = ["Timestamp_ms", "IR", "RED", "SpO2", "SpO2_Valid", "HR_Algorithm", "HR_Valid"]
data_cols = [c for c in data_cols if c in clean.columns]
exact_dups = clean.duplicated(subset=data_cols, keep="first")
print(f"Exact duplicate data rows to remove: {int(exact_dups.sum())}")
clean = clean.loc[~exact_dups].copy()

# Save original seven columns, not helper column.
clean[data_cols].to_csv(OUT, index=False)

# Report rate using positive intervals and endpoint rate.
ts = clean["_timestamp_numeric"].to_numpy(dtype=float)
dt = np.diff(ts) / 1000.0
positive = dt[dt > 0]
duration = (ts[-1] - ts[0]) / 1000.0 if len(ts) >= 2 else np.nan
print("\n=== CLEANED MOVING DATA SUMMARY ===")
print(f"Output rows: {len(clean)}")
print(f"First timestamp: {ts[0] if len(ts) else 'N/A'} ms")
print(f"Last timestamp:  {ts[-1] if len(ts) else 'N/A'} ms")
print(f"Duration: {duration:.3f} s" if np.isfinite(duration) else "Duration: N/A")
if len(positive):
    med = float(np.median(positive))
    mean = float(np.mean(positive))
    print(f"Median positive interval: {med * 1000:.3f} ms")
    print(f"Mean positive interval:   {mean * 1000:.3f} ms")
    print(f"Rate from median interval: {1 / med:.3f} Hz")
    print(f"Rate from mean interval:   {1 / mean:.3f} Hz")
    print(f"Intervals > 1.5x median: {int((positive > 1.5 * med).sum())}")
print(f"\nSaved cleaned candidate to: {OUT}")
print("Important: repeated timestamps were reported, not automatically removed. Inspect them before deciding.")
