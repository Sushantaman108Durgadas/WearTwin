"""Check that the streaming engine (sees only the past, one window at a time) gives exactly the same
raw predictions as batch scoring of the finished CSV. Run: python3 verify_engine.py
"""
import numpy as np
import pandas as pd

from engine import SoldierTwin, load_bundle
from features import build_features
from schema import CALIBRATION_WINDOWS, STATES

b = load_bundle()
test = pd.read_csv("data/test.csv")
# one session per scenario, from held-out soldiers
sessions = [g.session_id.iloc[0] for _, g in test.groupby("scenario")]
X = build_features(test)[b["feature_cols"]]
max_ready_diff, state_mismatch, idx_diff, n_checked = 0.0, 0, 0.0, 0
for sid in sessions:
    mask = (test.session_id == sid).to_numpy()
    d = test[mask].reset_index(drop=True)
    Xs = X[mask]
    batch_state = b["clf"].predict(Xs)
    batch_ready = np.clip(b["reg"].predict(Xs), 0, 100)
    batch_idx = {c: np.clip(m.predict(Xs), 0, 1) for c, m in b["idx_regs"].items()}
    twin = SoldierTwin(sid, b)
    for i, row in d.iterrows():
        out = twin.update(row)
        if i < CALIBRATION_WINDOWS:
            assert out["status"] == "Calibrating"
            continue
        n_checked += 1
        state_mismatch += int(out["raw_state"] != STATES[batch_state[i]])
        max_ready_diff = max(max_ready_diff, abs(out["readiness_raw"] - batch_ready[i]))
        idx_diff = max(idx_diff, max(abs(out[c] - batch_idx[c][i]) for c in batch_idx))
print(f"sessions: {len(sessions)} | windows compared: {n_checked}")
print(f"raw-state mismatches: {state_mismatch} | max |readiness diff|: {max_ready_diff:.2e} | max |index diff|: {idx_diff:.2e}")
assert state_mismatch == 0 and max_ready_diff < 1e-6 and idx_diff < 1e-6
print("PASS: streaming == batch")
