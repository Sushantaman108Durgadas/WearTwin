"""Causal feature extraction: every feature at window t uses only windows 0..t of the same session.

This is what makes the model usable on a live feed: the same function runs on a finished CSV
and on a stream that has only seen the past.
"""
import numpy as np
import pandas as pd

from schema import CALIBRATION_WINDOWS, DT_MIN, INPUT_COLS, apparent_temp

BASE_COLS = ["hr", "rmssd", "spo2", "rr", "skin_t"]
ACTIVE_IMU = 0.12  # g: above this we count the window as "active"


def _baseline(x, cal=CALIBRATION_WINDOWS):
    """Causal personal baseline: running median over the first `cal` windows, then frozen."""
    b = x.expanding(min_periods=1).median()
    if len(b) > cal:
        b.iloc[cal:] = b.iloc[cal - 1]
    return b.ffill()


def session_features(g):
    """g: one session, rows in time order, containing INPUT_COLS. Returns a feature DataFrame (same index)."""
    raw = g[INPUT_COLS].astype(float).reset_index(drop=True)
    n = len(raw)
    f = pd.DataFrame(index=raw.index)

    # raw channels the model may use directly
    for c in ["hr", "spo2", "rr", "skin_t", "amb_t", "hum", "imu"]:
        f[c] = raw[c]
    f["apparent_temp"] = apparent_temp(raw["amb_t"], raw["hum"])

    # personal-baseline deviations (the 'digital twin' normalisation)
    base = {c: _baseline(raw[c]) for c in BASE_COLS}
    f["hr_dev"] = raw["hr"] - base["hr"]
    f["rmssd_lr"] = np.log(raw["rmssd"].clip(lower=1.0) / base["rmssd"].clip(lower=1.0))
    f["spo2_dev"] = raw["spo2"] - base["spo2"]
    f["rr_dev"] = raw["rr"] - base["rr"]
    f["skin_dev"] = raw["skin_t"] - base["skin_t"]

    # short (3 min) and long (10 min) trailing summaries; median for HR to shrug off cadence-lock spikes
    for name, w in [("3m", 6), ("10m", 20)]:
        f[f"hr_dev_med_{name}"] = f["hr_dev"].rolling(w, min_periods=1).median()
        for c in ["rmssd_lr", "spo2_dev", "rr_dev", "skin_dev"]:
            f[f"{c}_mean_{name}"] = f[c].rolling(w, min_periods=1).mean()
        f[f"imu_mean_{name}"] = f["imu"].rolling(w, min_periods=1).mean()

    # trends: change in the 5-min mean over the last 5 minutes, per minute
    for c in ["hr_dev", "rmssd_lr", "rr_dev", "skin_dev"]:
        m10 = f[c].rolling(10, min_periods=1).mean()
        f[f"{c}_slope"] = (m10 - m10.shift(10)) / (10 * DT_MIN)

    # cardiac cost of movement: heart-rate rise per unit of activity (cardiac drift shows up here)
    f["hr_per_imu"] = f["hr_dev_med_10m"] / (f["imu_mean_10m"] + 0.1)

    # exposure memory
    # If a sensor was never seen so far the counter is unknown (NaN), not zero: "no data" must not look like "no exertion".
    active = (raw["imu"] > ACTIVE_IMU).astype(float)
    imu_seen = raw["imu"].notna().cumsum() > 0
    ap_seen = f["apparent_temp"].notna().cumsum() > 0
    f["active_min"] = (active.cumsum() * DT_MIN).where(imu_seen)
    f["heat_exposure_min"] = ((active * (f["apparent_temp"] > 30).astype(float)).cumsum() * DT_MIN).where(imu_seen & ap_seen)

    f["n_missing"] = raw.isna().sum(axis=1)
    f["calibrating"] = (np.arange(n) < CALIBRATION_WINDOWS).astype(int)
    f.index = g.index
    return f


def build_features(df):
    """Apply session_features per session_id; returns features aligned to df's index."""
    parts = [session_features(g) for _, g in df.groupby("session_id", sort=False)]
    return pd.concat(parts).loc[df.index]


FEATURE_COLS = list(session_features(
    pd.DataFrame({c: np.zeros(30) for c in INPUT_COLS})).columns)

# Direction in which each feature should move the readiness score (-1 = more of it lowers readiness).
# Used only for the optional monotonic-constrained readiness model.
READINESS_MONOTONIC = {
    "hr": -1, "hr_dev": -1, "hr_dev_med_3m": -1, "hr_dev_med_10m": -1, "hr_dev_slope": -1,
    "rr": -1, "rr_dev": -1, "rr_dev_mean_3m": -1, "rr_dev_mean_10m": -1, "rr_dev_slope": -1,
    "skin_t": -1, "skin_dev": -1, "skin_dev_mean_3m": -1, "skin_dev_mean_10m": -1, "skin_dev_slope": -1,
    "spo2": 1, "spo2_dev": 1, "spo2_dev_mean_3m": 1, "spo2_dev_mean_10m": 1,
    "rmssd_lr": 1, "rmssd_lr_mean_3m": 1, "rmssd_lr_mean_10m": 1, "rmssd_lr_slope": 1,
    "apparent_temp": -1, "hr_per_imu": -1, "active_min": -1, "heat_exposure_min": -1,
}
