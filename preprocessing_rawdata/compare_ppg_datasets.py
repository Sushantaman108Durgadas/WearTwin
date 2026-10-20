#!/usr/bin/env python3
"""Compare stationary and moving PPG CSV files.
Usage: python compare_ppg_datasets.py stationary.csv moving.csv [--timestamp Timestamp_ms] [--unit ms]
Install: pip install pandas numpy
"""
import argparse
import sys
from pathlib import Path
import numpy as np
import pandas as pd

TIME_NAMES = ["timestamp_ms", "timestamp", "time_ms", "time", "time_s",
              "elapsed_ms", "elapsed_time_ms", "millis", "timestamp_us",
              "timestamp_ns", "datetime", "date_time"]

def norm(s):
    return str(s).strip().lower().replace(" ", "_").replace("-", "_")

def find_time_col(df, requested=None):
    if requested:
        for c in df.columns:
            if norm(c) == norm(requested):
                return c
        raise ValueError(f"Timestamp column {requested!r} not found. Columns: {list(df.columns)}")
    by_norm = {norm(c): c for c in df.columns}
    for name in TIME_NAMES:
        if name in by_norm:
            return by_norm[name]
    for c in df.columns:
        n = norm(c)
        if "timestamp" in n or n.startswith("time_") or n in ("time", "millis"):
            return c
    return None

def unit_for(col, override):
    if override != "auto":
        return override
    n = norm(col or "")
    if n.endswith("_ns"): return "ns"
    if n.endswith("_us"): return "us"
    if n.endswith("_ms") or "millis" in n or "elapsed_ms" in n: return "ms"
    # Generic "time"/"timestamp" is ambiguous; assume seconds and print this.
    return "s"

def numeric_or_datetime(series):
    x = pd.to_numeric(series, errors="coerce")
    if x.notna().mean() >= 0.8:
        return x.astype(float), "numeric"
    d = pd.to_datetime(series, errors="coerce", utc=True)
    if d.notna().mean() >= 0.8:
        seconds = d.astype("int64").astype(float) / 1e9
        seconds[d.isna()] = np.nan
        return seconds, "datetime"
    return x.astype(float), "numeric"

def analyze(filename, label, timestamp=None, unit="auto"):
    path = Path(filename)
    if not path.exists():
        raise FileNotFoundError(f"File not found: {path}")
    df = pd.read_csv(path)
    if df.empty:
        raise ValueError(f"No rows in {path}")
    df.columns = [str(c).strip() for c in df.columns]

    print("\n" + "=" * 70)
    print(f"{label}: {path}")
    print("=" * 70)
    print(f"Rows: {len(df)}")
    print(f"Columns: {list(df.columns)}")
    print(f"Missing cells: {int(df.isna().sum().sum())}")

    result = {"label": label, "rows": len(df), "duration_s": np.nan, "rate_hz": np.nan}
    tc = find_time_col(df, timestamp)
    if tc is None:
        print("\nNo timestamp column detected; cannot infer duration/rate from row count alone.")
    else:
        print(f"\nTimestamp column: {tc}")
        t, kind = numeric_or_datetime(df[tc])
        if kind == "datetime":
            scale = 1.0
            print("Timestamp type: datetime strings (parsed as UTC)")
        else:
            u = unit_for(tc, unit)
            scale = {"s": 1, "ms": 1e-3, "us": 1e-6, "ns": 1e-9}[u]
            print(f"Timestamp type: numeric; interpreted as {u} (verify this assumption)")
        valid = t.notna()
        print(f"Valid timestamps: {int(valid.sum())}; invalid: {int((~valid).sum())}")
        vals = t[valid].to_numpy(dtype=float)
        if len(vals) >= 2:
            dt = np.diff(vals) * scale
            pos = dt[dt > 0]
            duration = (vals[-1] - vals[0]) * scale
            duplicates = int((dt == 0).sum())
            backwards = int((dt < 0).sum())
            print(f"First timestamp: {vals[0]}")
            print(f"Last timestamp:  {vals[-1]}")
            print(f"Elapsed duration in file order: {duration:.6f} s")
            print(f"Adjacent duplicate timestamps: {duplicates}")
            print(f"Backwards timestamp steps: {backwards}")
            print(f"Unique valid timestamps: {len(np.unique(vals))}")
            if len(pos):
                med = float(np.median(pos))
                mean = float(np.mean(pos))
                print(f"Positive intervals: {len(pos)}")
                print(f"Mean positive interval: {mean*1000:.6f} ms")
                print(f"Median positive interval: {med*1000:.6f} ms")
                print(f"Min positive interval: {pos.min()*1000:.6f} ms")
                print(f"Max positive interval: {pos.max()*1000:.6f} ms")
                print(f"Rate from median interval: {1/med:.3f} Hz")
                print(f"Rate from mean interval:   {1/mean:.3f} Hz")
                if duration > 0:
                    print(f"Endpoint rate (rows-1)/duration: {(len(vals)-1)/duration:.3f} Hz")
                gaps = pos[pos > 1.5 * med]
                print(f"Intervals > 1.5 × median interval: {len(gaps)}")
                if len(gaps):
                    print(f"Largest positive interval: {pos.max()*1000:.3f} ms")
                result["duration_s"] = duration
                result["rate_hz"] = 1 / med
            else:
                print("No positive intervals; rate cannot be estimated.")
        else:
            print("Need at least two valid timestamps to estimate rate.")

    # Numeric signal diagnostics, especially IR and RED.
    print("\nNumeric-column diagnostics:")
    num = df.apply(pd.to_numeric, errors="coerce")
    shown = False
    for c in df.columns:
        n = norm(c)
        if n in ("ir", "red", "infrared", "red_led") or n.startswith(("ir_", "red_")):
            x = num[c].dropna()
            if len(x):
                shown = True
                print(f"  {c}: valid={len(x)}, min={x.min():.3f}, max={x.max():.3f}, "
                      f"mean={x.mean():.3f}, std={x.std():.3f}, zeros={(x == 0).sum()}")
    if not shown:
        print("  No recognizable IR/RED columns found.")
    numeric_cols = [c for c in num.columns if num[c].notna().any()]
    if numeric_cols:
        zero_rows = num[numeric_cols].fillna(0).eq(0).all(axis=1).sum()
        print(f"Rows with all numeric columns zero (NaNs treated as zero): {int(zero_rows)}")
    return result

def main():
    p = argparse.ArgumentParser(description="Compare stationary and moving PPG CSV datasets.")
    p.add_argument("stationary_csv")
    p.add_argument("moving_csv")
    p.add_argument("--timestamp", help="Timestamp column name (same name in both files)")
    p.add_argument("--unit", choices=["auto", "s", "ms", "us", "ns"], default="auto",
                   help="Numeric timestamp units. Auto infers from column name; generic names default to seconds.")
    a = p.parse_args()
    try:
        s = analyze(a.stationary_csv, "STATIONARY", a.timestamp, a.unit)
        m = analyze(a.moving_csv, "MOVING", a.timestamp, a.unit)
    except Exception as e:
        print(f"\nERROR: {e}", file=sys.stderr)
        sys.exit(1)

    print("\n" + "=" * 70)
    print("SIDE-BY-SIDE SUMMARY")
    print("=" * 70)
    print(f"Stationary rows: {s['rows']}")
    print(f"Moving rows:     {m['rows']}")
    print(f"Moving/stationary row ratio: {m['rows']/s['rows']:.6f}×")
    if np.isfinite(s["duration_s"]) and np.isfinite(m["duration_s"]):
        print(f"Stationary duration: {s['duration_s']:.6f} s")
        print(f"Moving duration:     {m['duration_s']:.6f} s")
        if s["duration_s"] > 0:
            print(f"Moving/stationary duration ratio: {m['duration_s']/s['duration_s']:.6f}×")
    else:
        print("Duration ratio unavailable.")
    if np.isfinite(s["rate_hz"]) and np.isfinite(m["rate_hz"]):
        print(f"Stationary estimated rate: {s['rate_hz']:.3f} Hz")
        print(f"Moving estimated rate:     {m['rate_hz']:.3f} Hz")
        if s["rate_hz"] > 0:
            print(f"Moving/stationary rate ratio: {m['rate_hz']/s['rate_hz']:.6f}×")
    else:
        print("Rate ratio unavailable.")
    print("\nNotes:")
    print("- ESP32 millis() is elapsed time since boot, not calendar time.")
    print("- Verify units if the timestamp column is named only 'Timestamp' or 'Time'.")
    print("- A timestamp rate is only as reliable as the timestamp source; host-side serial logging can add jitter.")
    print("- Check gaps/duplicates before resampling. Don't infer a sample rate from row counts alone.")

if __name__ == "__main__":
    main()
