"""Model factories, a hand-written rule baseline, and metric helpers."""
import numpy as np
from sklearn.ensemble import HistGradientBoostingClassifier, HistGradientBoostingRegressor
from sklearn.impute import SimpleImputer
from sklearn.linear_model import Ridge
from sklearn.metrics import (accuracy_score, balanced_accuracy_score, confusion_matrix, f1_score,
                             mean_absolute_error, r2_score, recall_score)
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler

from features import FEATURE_COLS, READINESS_MONOTONIC
from schema import STATES

_HGB = dict(max_iter=200, learning_rate=0.08, max_leaf_nodes=24, min_samples_leaf=80,
            l2_regularization=1.0, early_stopping=False)


def make_clf(seed=0):
    return HistGradientBoostingClassifier(class_weight="balanced", random_state=seed, **_HGB)


def make_reg(seed=0, monotonic=False):
    cst = {c: READINESS_MONOTONIC.get(c, 0) for c in FEATURE_COLS} if monotonic else None
    return HistGradientBoostingRegressor(random_state=seed, monotonic_cst=cst, **_HGB)


def make_ridge():
    return make_pipeline(SimpleImputer(strategy="median"), StandardScaler(), Ridge(alpha=1.0))


def rule_state(X):
    """Plain-English threshold rules, fixed a priori (NOT tuned on the generated data):
         cardio-resp: HR > 165, or SpO2 < 92, or resp rate > 32
         thermal    : skin temp > 35.5 C, or (HR up > 40 bpm over baseline while apparent temp > 32 C)
         fatigue    : more than 120 min of cumulative activity
       Priority cardio > thermal > fatigue. Missing sensors simply never trigger their rule."""
    cardio = (X["hr"] > 165) | (X["spo2"] < 92) | (X["rr"] > 32)
    thermal = (X["skin_t"] > 35.5) | ((X["hr_dev_med_10m"] > 40) & (X["apparent_temp"] > 32))
    fatigue = X["active_min"] > 120
    out = np.zeros(len(X), dtype=int)
    out[fatigue.to_numpy()] = 1
    out[thermal.to_numpy()] = 2
    out[cardio.to_numpy()] = 3
    return out


def clf_metrics(y, p):
    labels = list(range(len(STATES)))
    return dict(
        accuracy=float(accuracy_score(y, p)),
        balanced_accuracy=float(balanced_accuracy_score(y, p)),
        macro_f1=float(f1_score(y, p, average="macro", labels=labels, zero_division=0)),
        recall=[float(r) for r in recall_score(y, p, labels=labels, average=None, zero_division=0)],
        confusion=confusion_matrix(y, p, labels=labels).tolist(),
    )


def reg_metrics(y, p):
    y, p = np.asarray(y, float), np.asarray(p, float)
    return dict(mae=float(mean_absolute_error(y, p)),
                rmse=float(np.sqrt(np.mean((y - p) ** 2))),
                r2=float(r2_score(y, p)),
                within10=float(np.mean(np.abs(y - p) <= 10)))
