"""Shared definitions for the WearTwin AI demo.

Everything that must stay identical between training, evaluation, the replay demo and
(later) the live ESP32 pipeline lives here: the input contract, state names, and the
rules that turn latent physiology into labels.
"""
import numpy as np

# ---------------------------------------------------------------- input contract
# One row per 30 s window. This is what the ESP32 side will eventually send.
INPUT_COLS = ["hr", "rmssd", "spo2", "rr", "skin_t", "amb_t", "hum", "imu"]
INPUT_UNITS = {
    "hr": "bpm (heart rate from ECG/PPG)",
    "rmssd": "ms (HRV, RMSSD over the window)",
    "spo2": "% (PPG SpO2)",
    "rr": "breaths/min (respiration rate)",
    "skin_t": "deg C (skin temperature)",
    "amb_t": "deg C (ambient temperature)",
    "hum": "% (relative humidity)",
    "imu": "g (mean absolute body acceleration, gravity removed)",
}
WINDOW_SECONDS = 30
DT_MIN = WINDOW_SECONDS / 60.0
# The first 20 windows (10 min) of a session are treated as the resting calibration
# period from which each soldier's personal baseline is taken.
CALIBRATION_WINDOWS = 20

# ---------------------------------------------------------------- outputs
STATES = ["Normal", "Fatigue", "Thermal strain", "Cardio-resp stress"]
READY_GREEN = 75.0   # readiness >= 75  -> Ready
READY_AMBER = 50.0   # 50..75 -> Caution ; < 50 -> Rest / rotate


def readiness_band(score):
    if score >= READY_GREEN:
        return "Ready"
    if score >= READY_AMBER:
        return "Caution"
    return "Rest/rotate"


# ---------------------------------------------------------------- environment
def apparent_temp(t_c, rh):
    """Apparent temperature (Steadman-type, no wind): T + 0.33*e - 4, e = vapour pressure (hPa)."""
    e = rh / 100.0 * 6.105 * np.exp(17.27 * t_c / (237.7 + t_c))
    return t_c + 0.33 * e - 4.0


# ---------------------------------------------------------------- label rules
def scale(x, lo, thr, hi):
    """Piecewise-linear map: lo -> 0, thr -> 0.5, hi -> 1 (clipped). Works when lo > hi."""
    xp, fp = [lo, thr, hi], [0.0, 0.5, 1.0]
    if lo > hi:
        xp, fp = xp[::-1], fp[::-1]
    return np.interp(np.asarray(x, dtype=float), xp, fp)


def severity_indices(core_temp, fatigue, hrr, spo2, rr):
    """Three strain indices in [0, 1]; 0.5 is the 'alert' threshold for each.

    thermal : core temperature 37.5 -> 0, 38.3 -> 0.5, 39.2 -> 1
    fatigue : latent fatigue level 0.15 -> 0, 0.5 -> 0.5, 0.85 -> 1
    cardio  : worst of heart-rate reserve used (0.60/0.80/0.95), SpO2 (96/90/84), resp rate (25/35/45)
    """
    thermal = scale(core_temp, 37.5, 38.3, 39.2)
    fat = scale(fatigue, 0.15, 0.50, 0.85)
    cardio = np.maximum.reduce([
        scale(hrr, 0.60, 0.80, 0.95),
        scale(spo2, 96.0, 90.0, 84.0),
        scale(rr, 25.0, 35.0, 45.0),
    ])
    return thermal, fat, cardio


def label_state(thermal, fat, cardio, alert=0.5):
    """State id (index into STATES): the largest index if it reaches the alert level, else Normal."""
    idx = np.stack([fat, thermal, cardio], axis=1)  # order = STATES[1:]
    return np.where(idx.max(axis=1) >= alert, idx.argmax(axis=1) + 1, 0)


def readiness_score(thermal, fat, cardio):
    """0-100. 100 = no strain. Worst index dominates; the mean adds a smaller penalty."""
    idx = np.stack([fat, thermal, cardio], axis=1)
    strain = 0.7 * idx.max(axis=1) + 0.3 * idx.mean(axis=1)
    return 100.0 * (1.0 - strain)
