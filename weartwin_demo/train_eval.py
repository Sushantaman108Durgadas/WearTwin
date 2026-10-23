"""Generate data, train, cross-validate by soldier, evaluate on held-out soldiers, run the verification
checks, and save models + metrics. Run:  python3 train_eval.py
"""
import json
import time
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from sklearn.inspection import permutation_importance
from sklearn.model_selection import GroupKFold

from features import FEATURE_COLS, build_features
from models import (clf_metrics, make_clf, make_reg, make_ridge, reg_metrics, rule_state)
from schema import CALIBRATION_WINDOWS, DT_MIN, INPUT_COLS, STATES
from simulate import generate_dataset

ROOT = Path(__file__).parent
DATA, MODELS, REPORTS = ROOT / "data", ROOT / "models", ROOT / "reports"
for d in (DATA, MODELS, REPORTS):
    d.mkdir(exist_ok=True)
T0 = time.time()
IDX_COLS = ["thermal_idx", "fatigue_idx", "cardio_idx"]


def log(*a):
    print(f"[{time.time() - T0:6.0f}s]", *a, flush=True)


def summarise(folds):
    keys = [k for k in folds[0] if isinstance(folds[0][k], float)]
    return {k: [float(np.mean([f[k] for f in folds])), float(np.std([f[k] for f in folds]))] for k in keys}


def make_aug(df, rng, frac=0.25):
    """Copies of a random 25% of sessions with ONE whole sensor channel missing (random channel per session).
    Teaches the model that a missing channel is 'unknown', not 'zero'."""
    sessions = df["session_id"].unique()
    chosen = rng.choice(sessions, size=int(len(sessions) * frac), replace=False)
    aug = df[df["session_id"].isin(chosen)].copy()
    for sid, col in zip(chosen, rng.choice(INPUT_COLS, size=len(chosen))):
        aug.loc[aug["session_id"] == sid, col] = np.nan
    aug["session_id"] = aug["session_id"] + "-aug"
    return aug


# ------------------------------------------------------------------ data
log("generating data")
train = generate_dataset(48, seed=101, id_prefix="T")
test = generate_dataset(12, seed=202, id_prefix="H")                  # held-out soldiers, same population
shifted = generate_dataset(12, seed=303, shifted=True, id_prefix="X")  # held-out, older/less fit/noisier sensors
for name, d in [("train", train), ("test", test), ("shifted", shifted)]:
    d.to_csv(DATA / f"{name}.csv", index=False)
assert not (set(train.soldier_id) & set(test.soldier_id)) and not (set(train.soldier_id) & set(shifted.soldier_id))
assert not (set(test.soldier_id) & set(shifted.soldier_id))

train_all = pd.concat([train, make_aug(train, np.random.default_rng(5))], ignore_index=True)
is_aug = train_all["session_id"].str.endswith("-aug").to_numpy()
Xtr = build_features(train_all)[FEATURE_COLS]
Xte, Xsh = (build_features(d)[FEATURE_COLS] for d in (test, shifted))
ytr, yte, ysh = (d["state_id"].to_numpy() for d in (train_all, test, shifted))
rtr, rte, rsh = (d["readiness"].to_numpy() for d in (train_all, test, shifted))
mtr, mte, msh = (X["calibrating"].to_numpy() == 0 for X in (Xtr, Xte, Xsh))   # evaluate post-calibration only
log(f"rows clean-train/aug/test/shifted: {len(train)}/{int(is_aug.sum())}/{len(test)}/{len(shifted)}; features: {len(FEATURE_COLS)}")
R = {"label_distribution": {n: d["state"].value_counts(normalize=True).round(4).to_dict()
                            for n, d in [("train", train), ("test", test), ("shifted", shifted)]},
     "n_rows": {"train": len(train), "train_augmented_extra": int(is_aug.sum()), "test": len(test), "shifted": len(shifted)},
     "n_soldiers": {"train": 48, "test": 12, "shifted": 12}}

# ------------------------------------------------------------------ leave-soldier-out CV (model selection only)
log("cross-validation (GroupKFold by soldier; validation rows are clean sessions of unseen soldiers)")
cv = {k: [] for k in ["rules", "clf", "const", "ridge", "reg_free", "reg_mono"]}
for fold, (tr, va) in enumerate(GroupKFold(n_splits=5).split(Xtr, groups=train_all.soldier_id)):
    va = va[~is_aug[va]]
    ev = va[mtr[va]]
    cv["rules"].append(clf_metrics(ytr[ev], rule_state(Xtr.iloc[ev])))
    clf = make_clf().fit(Xtr.iloc[tr], ytr[tr])
    cv["clf"].append(clf_metrics(ytr[ev], clf.predict(Xtr.iloc[ev])))
    cv["const"].append(reg_metrics(rtr[ev], np.full(len(ev), rtr[tr].mean())))
    cv["ridge"].append(reg_metrics(rtr[ev], make_ridge().fit(Xtr.iloc[tr], rtr[tr]).predict(Xtr.iloc[ev])))
    cv["reg_free"].append(reg_metrics(rtr[ev], make_reg().fit(Xtr.iloc[tr], rtr[tr]).predict(Xtr.iloc[ev])))
    cv["reg_mono"].append(reg_metrics(rtr[ev], make_reg(monotonic=True).fit(Xtr.iloc[tr], rtr[tr]).predict(Xtr.iloc[ev])))
    log(f"  fold {fold + 1}/5 done: clf macro-F1 {cv['clf'][-1]['macro_f1']:.3f}, "
        f"readiness MAE free {cv['reg_free'][-1]['mae']:.2f} / mono {cv['reg_mono'][-1]['mae']:.2f}")
R["cv"] = {k: summarise(v) for k, v in cv.items()}
R["cv_rules_recall"] = np.mean([f["recall"] for f in cv["rules"]], axis=0).tolist()
R["cv_clf_recall"] = np.mean([f["recall"] for f in cv["clf"]], axis=0).tolist()
# selection rule (fixed before looking at the test set): use the monotonic-constrained readiness model
# unless it is more than 10% worse in CV MAE.
use_mono = R["cv"]["reg_mono"]["mae"][0] <= 1.10 * R["cv"]["reg_free"]["mae"][0]
R["readiness_model_choice"] = "monotonic-constrained" if use_mono else "unconstrained"
log("readiness model choice:", R["readiness_model_choice"])

# ------------------------------------------------------------------ final fit + held-out evaluation
log("final fit on all 48 training soldiers (+ sensor-loss augmentation)")
clf = make_clf().fit(Xtr, ytr)
reg_free = make_reg().fit(Xtr, rtr)
reg_mono = make_reg(monotonic=True).fit(Xtr, rtr)
reg = reg_mono if use_mono else reg_free
ridge = make_ridge().fit(Xtr, rtr)
idx_regs = {c: make_reg().fit(Xtr, train_all[c].to_numpy()) for c in IDX_COLS}
joblib.dump(dict(clf=clf, reg=reg, idx_regs=idx_regs, feature_cols=FEATURE_COLS, states=STATES,
                 readiness_model=R["readiness_model_choice"]), MODELS / "weartwin_model.joblib")
joblib.dump(dict(reg_free=reg_free, reg_mono=reg_mono), MODELS / "readiness_variants.joblib")

for name, d, X, y, r, m in [("test", test, Xte, yte, rte, mte), ("shifted", shifted, Xsh, ysh, rsh, msh)]:
    pc, pr = clf.predict(X), reg.predict(X)
    R[name] = dict(clf=clf_metrics(y[m], pc[m]), rules=clf_metrics(y[m], rule_state(X)[m]),
                   reg=reg_metrics(r[m], np.clip(pr[m], 0, 100)),
                   reg_free=reg_metrics(r[m], np.clip(reg_free.predict(X)[m], 0, 100)),
                   reg_mono=reg_metrics(r[m], np.clip(reg_mono.predict(X)[m], 0, 100)),
                   ridge=reg_metrics(r[m], np.clip(ridge.predict(X)[m], 0, 100)),
                   const=reg_metrics(r[m], np.full(m.sum(), rtr.mean())),
                   indices={c: reg_metrics(d[c].to_numpy()[m], np.clip(idx_regs[c].predict(X)[m], 0, 1)) for c in IDX_COLS})
    log(f"{name}: macro-F1 model {R[name]['clf']['macro_f1']:.3f} vs rules {R[name]['rules']['macro_f1']:.3f}; "
        f"readiness MAE {R[name]['reg']['mae']:.2f} (ridge {R[name]['ridge']['mae']:.2f}, const {R[name]['const']['mae']:.2f}); "
        f"index MAE " + ", ".join(f"{c.split('_')[0]} {R[name]['indices'][c]['mae']:.3f}" for c in IDX_COLS))

# held-out predictions for plotting / the demo
proba = clf.predict_proba(Xte)
pred = test[["soldier_id", "session_id", "scenario", "t_min", "state_id", "readiness"] + IDX_COLS].copy()
pred["pred_state_id"] = clf.predict(Xte)
pred["pred_readiness"] = np.clip(reg.predict(Xte), 0, 100)
for c in IDX_COLS:
    pred["pred_" + c] = np.clip(idx_regs[c].predict(Xte), 0, 1)
for i in range(len(STATES)):
    pred[f"p_{i}"] = proba[:, i]
pred.to_csv(REPORTS / "test_predictions.csv", index=False)

# ------------------------------------------------------------------ verification 1: shuffled labels
log("verification: shuffled-label test")
shuf = make_clf().fit(Xtr, np.random.default_rng(0).permutation(ytr))
R["shuffled_label"] = clf_metrics(yte[mte], shuf.predict(Xte[mte]))
log(f"  shuffled-label macro-F1 {R['shuffled_label']['macro_f1']:.3f}, balanced acc {R['shuffled_label']['balanced_accuracy']:.3f} (chance = 0.25)")

# ------------------------------------------------------------------ verification 2: monotonic perturbations
log("verification: perturbation (monotonic sanity) tests")
pos = test.groupby("session_id").cumcount().to_numpy()
after_cal = pos >= CALIBRATION_WINDOWS     # perturb only AFTER calibration, else the personal baseline absorbs it
PERTS = {
    "HR +15 bpm": {"hr": ("add", 15)},
    "Skin temp +1.0 C": {"skin_t": ("add", 1.0)},
    "RMSSD x0.6": {"rmssd": ("mul", 0.6)},
    "SpO2 -4 %": {"spo2": ("add", -4)},
    "Resp rate +6 /min": {"rr": ("add", 6)},
    "Ambient +6 C and humidity +15 %": {"amb_t": ("add", 6), "hum": ("add", 15)},
}


def perturb(df, spec):
    out = df.copy()
    for col, (op, v) in spec.items():
        out.loc[after_cal, col] = out.loc[after_cal, col] + v if op == "add" else out.loc[after_cal, col] * v
    return out


R["perturbation"] = {}
base_free, base_mono = reg_free.predict(Xte), reg_mono.predict(Xte)
for name, spec in PERTS.items():
    Xp = build_features(perturb(test, spec))[FEATURE_COLS]
    row = {}
    for tag, model, base in [("free", reg_free, base_free), ("mono", reg_mono, base_mono)]:
        d = (model.predict(Xp) - base)[after_cal]
        row[tag] = dict(mean_delta=float(d.mean()), pct_readiness_rises_over_2=float(np.mean(d > 2.0) * 100),
                        pct_readiness_falls_over_2=float(np.mean(d < -2.0) * 100))
    R["perturbation"][name] = row
    log(f"  {name:34s} free: mean {row['free']['mean_delta']:+6.2f}, rises>2 {row['free']['pct_readiness_rises_over_2']:5.1f}% | "
        f"mono: mean {row['mono']['mean_delta']:+6.2f}, rises>2 {row['mono']['pct_readiness_rises_over_2']:5.1f}%")

# ------------------------------------------------------------------ verification 3: robustness, with/without augmentation
log("verification: sensor-loss and noise robustness (with vs without sensor-loss augmentation)")
clean = ~is_aug
clf_na = make_clf().fit(Xtr[clean], ytr[clean])
reg_na = (make_reg(monotonic=True) if use_mono else make_reg()).fit(Xtr[clean], rtr[clean])


def evaluate_variant(df_variant, c_model, r_model):
    X = build_features(df_variant)[FEATURE_COLS]
    m = X["calibrating"].to_numpy() == 0
    return dict(macro_f1=clf_metrics(yte[m], c_model.predict(X[m]))["macro_f1"],
                mae=reg_metrics(rte[m], np.clip(r_model.predict(X[m]), 0, 100))["mae"])


variants = {"no change": test}
for c in INPUT_COLS:
    v = test.copy(); v[c] = np.nan
    variants[f"{c} missing for whole session"] = v
rng = np.random.default_rng(7)
v, n = test.copy(), len(test)
v["hr"] += rng.normal(0, 3, n); v["rmssd"] *= np.exp(rng.normal(0, 0.2, n)); v["spo2"] += rng.normal(0, 1.0, n)
v["rr"] += rng.normal(0, 1.5, n); v["skin_t"] += rng.normal(0, 0.3, n); v["amb_t"] += rng.normal(0, 0.5, n)
v["hum"] += rng.normal(0, 3, n); v["imu"] = (v["imu"] + rng.normal(0, 0.03, n)).clip(lower=0)
variants["extra noise on all sensors"] = v
R["robustness"] = {k: dict(aug=evaluate_variant(v, clf, reg), noaug=evaluate_variant(v, clf_na, reg_na))
                   for k, v in variants.items()}
for k, val in R["robustness"].items():
    log(f"  {k:36s} with aug: F1 {val['aug']['macro_f1']:.3f} MAE {val['aug']['mae']:5.2f} | "
        f"no aug: F1 {val['noaug']['macro_f1']:.3f} MAE {val['noaug']['mae']:5.2f}")

# ------------------------------------------------------------------ alert timing on held-out sessions
log("alert timing (3 consecutive windows)")
K = 3


def first_run(arr, k=K):
    run = 0
    for i, a in enumerate(arr):
        run = run + 1 if a else 0
        if run >= k:
            return i
    return None


def last_run_end(arr, k=K):
    run, end = 0, None
    for i, a in enumerate(arr):
        run = run + 1 if a else 0
        if run >= k:
            end = i
    return end


R["alerts"] = {}
for target in (1, 2, 3):
    delays, detected, episodes, false_sessions, no_ep = [], 0, 0, 0, 0
    for sid, g in pred.groupby("session_id", sort=False):
        g = g.iloc[CALIBRATION_WINDOWS:]
        truth, guess = (g["state_id"].to_numpy() == target), (g["pred_state_id"].to_numpy() == target)
        t_start, t_end, a_start = first_run(truth), last_run_end(truth), first_run(guess)
        if t_start is not None:
            episodes += 1
            if a_start is not None and a_start <= t_end:      # first alert no later than the end of the true episode
                detected += 1
                delays.append((a_start - t_start) * DT_MIN)
        else:
            no_ep += 1
            false_sessions += int(a_start is not None)
    R["alerts"][STATES[target]] = dict(
        sessions_with_true_episode=episodes, detected=detected,
        median_delay_min=float(np.median(delays)) if delays else None,
        p10_delay_min=float(np.percentile(delays, 10)) if delays else None,
        p90_delay_min=float(np.percentile(delays, 90)) if delays else None,
        sessions_without_episode=no_ep, false_alert_sessions=false_sessions)
    log(f"  {STATES[target]:20s} {R['alerts'][STATES[target]]}")

# ------------------------------------------------------------------ permutation importance (explainability)
log("permutation importance")
sub = np.random.default_rng(1).choice(np.flatnonzero(mte), size=5000, replace=False)
pi = permutation_importance(clf, Xte.iloc[sub], yte[sub], scoring="f1_macro", n_repeats=3, random_state=0, n_jobs=2)
order = np.argsort(-pi.importances_mean)[:12]
R["importance"] = [[FEATURE_COLS[i], float(pi.importances_mean[i]), float(pi.importances_std[i])] for i in order]
for f, m, s in R["importance"]:
    log(f"  {f:22s} {m:.4f} +/- {s:.4f}")

with open(REPORTS / "metrics.json", "w") as fh:
    json.dump(R, fh, indent=2)
log("done; metrics saved to reports/metrics.json")
