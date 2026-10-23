"""Streaming 'digital twin' engine: feed it one 30 s window at a time, get the soldier's current state.

The same engine is what the live ESP32 -> FastAPI path will call later; only the source of windows changes.
The first CALIBRATION_WINDOWS windows (10 min) of a feed must be the soldier at rest: they define the
personal baseline. Until then the status is 'Calibrating'.
"""
from pathlib import Path

import joblib
import numpy as np
import pandas as pd

from features import session_features
from schema import CALIBRATION_WINDOWS, DT_MIN, INPUT_COLS, STATES, readiness_band

MODEL_PATH = Path(__file__).parent / "models" / "weartwin_model.joblib"
IDX = ["thermal_idx", "fatigue_idx", "cardio_idx"]


def load_bundle(path=MODEL_PATH):
    return joblib.load(path)


class SoldierTwin:
    """Per-soldier state. `confirm_k` = consecutive identical raw predictions needed to change the confirmed state."""

    def __init__(self, soldier_id, bundle, confirm_k=3, ema=0.35):
        self.soldier_id, self.b = soldier_id, bundle
        self.rows, self.confirm_k, self.ema = [], confirm_k, ema
        self.state_id, self._cand, self._cand_n, self._ready = 0, 0, 0, None

    def update(self, window):
        """window: dict/Series with INPUT_COLS (missing or None -> NaN). Returns the twin's current state."""
        self.rows.append({c: (np.nan if window.get(c) is None else float(window.get(c))) for c in INPUT_COLS})
        n = len(self.rows)
        t_min = (n - 1) * DT_MIN
        if n <= CALIBRATION_WINDOWS:
            return dict(soldier_id=self.soldier_id, t_min=t_min, status="Calibrating", state="Calibrating",
                        raw_state=None, readiness=None, readiness_raw=None, band=None, alert=False,
                        **{c: None for c in IDX})
        df = pd.DataFrame(self.rows)
        df["session_id"] = self.soldier_id
        X = session_features(df).iloc[[-1]][self.b["feature_cols"]]
        proba = self.b["clf"].predict_proba(X)[0]
        raw_state = int(np.argmax(proba))
        ready_raw = float(np.clip(self.b["reg"].predict(X)[0], 0, 100))
        idx = {c: float(np.clip(m.predict(X)[0], 0, 1)) for c, m in self.b["idx_regs"].items()}

        self._ready = ready_raw if self._ready is None else self.ema * ready_raw + (1 - self.ema) * self._ready
        # debounce: the confirmed state only changes after confirm_k identical raw predictions in a row
        self._cand_n = self._cand_n + 1 if raw_state == self._cand else 1
        self._cand = raw_state
        if self._cand_n >= self.confirm_k:
            self.state_id = raw_state
        return dict(soldier_id=self.soldier_id, t_min=t_min, status="Live", state=STATES[self.state_id],
                    raw_state=STATES[raw_state], readiness=self._ready, readiness_raw=ready_raw,
                    band=readiness_band(self._ready), alert=self.state_id != 0, **idx,
                    **{f"p_{STATES[i]}": float(p) for i, p in enumerate(proba)})
