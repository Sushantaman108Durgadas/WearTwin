"""Synthetic soldier physiology generator.

A small latent-state model (core temperature, fatigue, dehydration, heart-rate lag, skin
temperature) is driven by scripted scenarios. Sensor-style readings are derived from the latent
state, then corrupted with noise, motion artefacts and dropouts. Labels are computed from the
*latent* (noise-free) state using the rules in schema.py.

IMPORTANT: coefficients are physiologically *plausible*, not calibrated against real data.
A model trained on this learns this generator, which is enough to demonstrate the pipeline
but is not evidence of real-world detection accuracy.
"""
import numpy as np
import pandas as pd

from schema import (DT_MIN, apparent_temp, label_state, readiness_score,
                    severity_indices, STATES)

SCENARIOS = ["rest_duty", "steady_march", "heavy_load_hot", "heavy_load_mild", "hot_dehydration",
             "prolonged_exertion", "high_intensity", "altitude_climb"]


def make_soldier(rng, shifted=False):
    """Draw one soldier's personal physiology. `shifted=True` = older, less fit, heat-sensitive, noisier sensors."""
    age = rng.integers(25, 46) if shifted else rng.integers(19, 38)
    return dict(
        age=int(age),
        hr_rest=float(np.clip(rng.normal(62, 7), 48, 80)),
        hr_max=float(207 - 0.7 * age + rng.normal(0, 5)),
        fitness=float(np.clip(rng.normal(0.88 if shifted else 1.0, 0.12), 0.7, 1.3)),
        rmssd_base=float(np.clip(np.exp(rng.normal(np.log(50), 0.35)), 20, 120)),
        rr_rest=float(np.clip(rng.normal(14, 1.5), 10, 19)),
        spo2_base=float(np.clip(rng.normal(97.8, 0.6), 95.5, 99.5)),
        tsk_base=float(rng.normal(33.5, 0.5)),
        tc0=float(rng.normal(37.0, 0.12)),
        mass=float(np.clip(rng.normal(75, 8), 55, 100)),
        heat_sens=float(np.clip(rng.normal(1.1 if shifted else 1.0, 0.12), 0.75, 1.35)),
        tau_tc=float(np.clip(rng.normal(18, 3), 10, 28)),
        noise=1.5 if shifted else 1.0,
    )


def _plan(name, rng):
    """Return (segments, (ambient_C, rel_humidity, altitude_m), drink_fraction).
    A segment is (minutes, demand, speed). demand ~ fraction of the average soldier's aerobic reserve."""
    # Ambient temperature ranges deliberately overlap across scenarios so that temperature is not a
    # shortcut for "which scenario is this" (a model must use the physiology, not the weather).
    U = rng.uniform
    if name == "rest_duty":
        return [(U(60, 90), U(0.05, 0.15), 0.10)], (U(18, 34), U(30, 70), 0.0), 0.9
    if name == "steady_march":
        return [(U(60, 90), U(0.34, 0.46), 0.55)], (U(5, 30), U(35, 70), 0.0), 0.8
    if name == "heavy_load_hot":
        return [(U(45, 70), U(0.52, 0.62), 0.45)], (U(33, 40), U(35, 70), 0.0), U(0.4, 0.7)
    if name == "heavy_load_mild":
        return [(U(45, 70), U(0.52, 0.62), 0.45)], (U(8, 26), U(35, 70), 0.0), U(0.4, 0.7)
    if name == "hot_dehydration":
        return [(U(75, 110), U(0.40, 0.50), 0.55)], (U(30, 38), U(30, 60), 0.0), U(0.0, 0.2)
    if name == "prolonged_exertion":
        return [(U(150, 200), U(0.38, 0.46), 0.50)], (U(12, 34), U(40, 65), 0.0), 0.8
    if name == "high_intensity":
        segs = []
        for _ in range(int(rng.integers(5, 8))):
            segs.append((5.0, U(0.82, 0.94), 0.90))
            segs.append((3.0, 0.30, 0.50))
        return segs, (U(10, 36), U(35, 65), 0.0), 0.8
    if name == "altitude_climb":
        return [(U(45, 70), U(0.45, 0.55), 0.40)], (U(-2, 18), U(25, 60), U(3000, 4800)), 0.7
    raise ValueError(name)


def _dropout(n, p_start, rng, max_len=6):
    p_start = np.broadcast_to(p_start, n)
    mask = np.zeros(n, dtype=bool)
    for i in np.flatnonzero(rng.random(n) < p_start):
        mask[i:i + int(rng.integers(1, max_len + 1))] = True
    return mask


def simulate_session(s, name, rng, soldier_id, session_id):
    segs, (tamb0, rh0, alt_m), drink = _plan(name, rng)
    plan = [(10.0, 0.02, 0.0)] + segs + [(float(rng.uniform(10, 15)), 0.02, 0.0)]
    demand = np.concatenate([np.full(int(round(m / DT_MIN)), d) for m, d, _ in plan])
    speed = np.concatenate([np.full(int(round(m / DT_MIN)), sp) for m, _, sp in plan])
    n = len(demand)
    t = np.arange(n) * DT_MIN

    period, phase = rng.uniform(60, 120), rng.uniform(0, 2 * np.pi)
    amb = tamb0 + 1.2 * np.sin(2 * np.pi * t / period + phase)
    rh = np.clip(rh0 + 3 * np.sin(2 * np.pi * t / period + phase + 1.0), 10, 100)
    ap = apparent_temp(amb, rh)
    hf = np.clip((ap - 22.0) / 20.0, 0, 1.2) * s["heat_sens"]   # heat factor

    ar_d, ar_s = np.zeros(n), np.zeros(n)
    for i in range(1, n):
        ar_d[i] = 0.92 * ar_d[i - 1] + rng.normal(0, 0.04)
        ar_s[i] = 0.92 * ar_s[i - 1] + rng.normal(0, 0.05)
    d_eff = np.where(demand > 0.1, demand * (1 + ar_d), demand)
    effort = np.clip(d_eff / s["fitness"], 0, 1.2)

    # ---- latent dynamics
    tc = np.zeros(n); tsk = np.zeros(n); fat = np.zeros(n); dehy = np.zeros(n); es = np.zeros(n)
    tc_i, f_i, d_i, es_i = s["tc0"], 0.0, 0.0, 0.0

    def skin_eq(i, tc_now):
        return np.clip(31.0 + 0.30 * (amb[i] - 15.0) + 0.7 * (tc_now - 37.0) + (s["tsk_base"] - 33.5), 28.0, 37.0)

    tsk_i = skin_eq(0, tc_i)
    for i in range(n):
        e = effort[i]
        es_i += (e - es_i) * DT_MIN / 1.0                                    # HR response lag ~1 min
        tc_eq = s["tc0"] + 1.8 * e * (1 + 0.6 * hf[i]) + 0.25 * d_i
        tc_i += (tc_eq - tc_i) * DT_MIN / s["tau_tc"]
        sweat_lph = np.clip(0.1 + 1.6 * e * (0.5 + hf[i]), 0, 2.2)
        d_i += sweat_lph * DT_MIN / 60.0 * (1 - drink) / s["mass"] * 100.0   # % body mass lost
        acc = 0.006 * (max(e - 0.15, 0.0) / 0.25) ** 1.5 * (1 + 0.5 * max(tc_i - 38.0, 0.0))
        rec = 0.003 if e < 0.1 else 0.0
        f_i += DT_MIN * (acc * (1 - f_i) - rec * f_i)
        tsk_i += (skin_eq(i, tc_i) - tsk_i) * DT_MIN / 6.0
        tc[i], tsk[i], fat[i], dehy[i], es[i] = tc_i, tsk_i, f_i, d_i, es_i

    # ---- true physiological signals from latent state
    hr_true = (s["hr_rest"] + (s["hr_max"] - s["hr_rest"]) * np.clip(es, 0, 1.0)
               + 6.0 * (tc - s["tc0"]) + 3.0 * dehy + 6.0 * fat)
    hr_true = np.minimum(hr_true, s["hr_max"] + 3)
    hrr_true = (hr_true - s["hr_rest"]) / (s["hr_max"] - s["hr_rest"])
    alt_km = alt_m / 1000.0
    spo2_true = np.clip(s["spo2_base"] - 1.9 * alt_km - 1.0 * es ** 2 * (1 + 0.5 * alt_km), 70, 100)
    rr_true = np.clip(s["rr_rest"] + 22.0 * np.clip(es, 0, None) ** 1.1 + 1.5 * (tc - s["tc0"])
                      + 2.5 * fat + 0.8 * np.maximum(0, 95 - spo2_true), 6, 55)
    rmssd_true = np.maximum(4.0, s["rmssd_base"] * np.exp(-2.2 * es) * (1 - 0.35 * fat)
                            * (1 - 0.05 * dehy) * (1 - 0.1 * (tc - s["tc0"])))
    speed_act = np.clip(speed * (1 - 0.12 * fat) * (1 + ar_s), 0, None)
    imu_true = 0.015 + 0.55 * speed_act ** 1.2

    # ---- sensor model: noise, motion artefacts, dropouts
    ns = s["noise"]
    imu_n = np.clip(imu_true / 0.5, 0, 1.5)
    hr_m = hr_true + rng.normal(0, ns * (1.5 + 4.0 * imu_n))
    lock = (imu_n > 0.3) & (rng.random(n) < 0.08 * imu_n * ns)   # PPG locks onto step cadence (needs real motion)
    hr_m = np.where(lock, rng.uniform(110, 170, n), hr_m)
    rmssd_m = rmssd_true * np.exp(rng.normal(0, ns * (0.15 + 0.30 * imu_n)))
    spo2_m = np.clip(spo2_true + rng.normal(0, ns * (0.5 + 0.8 * imu_n)), 70, 100)
    rr_m = rr_true + rng.normal(0, ns * (1.0 + 1.5 * imu_n))
    skin_m = tsk + rng.normal(0, ns * 0.15)
    amb_m = amb + rng.normal(0, 0.3 * ns)
    hum_m = np.clip(rh + rng.normal(0, 1.5 * ns), 5, 100)
    imu_m = np.clip(imu_true + rng.normal(0, 0.02 + 0.05 * imu_n), 0, None)

    sensors = dict(hr=hr_m, rmssd=rmssd_m, spo2=spo2_m, rr=rr_m, skin_t=skin_m,
                   amb_t=amb_m, hum=hum_m, imu=imu_m)
    p_drop = dict(hr=0.004, rmssd=0.008, spo2=0.01 + 0.05 * imu_n, rr=0.006,
                  skin_t=0.001, amb_t=0.001, hum=0.001, imu=0.0005)
    for c, arr in sensors.items():
        arr[_dropout(n, p_drop[c], rng)] = np.nan

    # ---- labels from the latent state
    thermal, fatigue_i, cardio = severity_indices(tc, fat, hrr_true, spo2_true, rr_true)
    state_id = label_state(thermal, fatigue_i, cardio)
    ready = readiness_score(thermal, fatigue_i, cardio)

    out = pd.DataFrame({"soldier_id": soldier_id, "session_id": session_id, "scenario": name, "t_min": t})
    for c, arr in sensors.items():
        out[c] = np.round(arr, 2)
    out["state"] = [STATES[k] for k in state_id]
    out["state_id"] = state_id
    out["readiness"] = np.round(ready, 2)
    out["thermal_idx"] = np.round(thermal, 3)
    out["fatigue_idx"] = np.round(fatigue_i, 3)
    out["cardio_idx"] = np.round(cardio, 3)
    # latent values: for analysis only, never given to the model
    out["lat_core_temp"] = np.round(tc, 3)
    out["lat_fatigue"] = np.round(fat, 3)
    out["lat_dehydration_pct"] = np.round(dehy, 3)
    out["lat_hrr"] = np.round(hrr_true, 3)
    return out


def generate_dataset(n_soldiers, seed, shifted=False, id_prefix="S", extra_sessions=2):
    """Each soldier does all 8 scenarios once plus `extra_sessions` random repeats, in random order."""
    rng = np.random.default_rng(seed)
    frames = []
    for k in range(n_soldiers):
        s = make_soldier(rng, shifted)
        sid = f"{id_prefix}{k + 1:03d}"
        names = [str(x) for x in SCENARIOS] + [str(x) for x in rng.choice(SCENARIOS, size=extra_sessions)]
        rng.shuffle(names)
        for j, name in enumerate(names):
            frames.append(simulate_session(s, name, rng, sid, f"{sid}-{j + 1:02d}"))
    return pd.concat(frames, ignore_index=True)


if __name__ == "__main__":
    df = generate_dataset(6, seed=0)
    print(df.shape)
    g = df.groupby("scenario")
    print(g.agg(hr_max=("hr", "max"), tc_max=("lat_core_temp", "max"), fat_max=("lat_fatigue", "max"),
                dehy_max=("lat_dehydration_pct", "max"), spo2_min=("spo2", "min"),
                ready_min=("readiness", "min")).round(2))
    print(pd.crosstab(df["scenario"], df["state"]))
    print(df["state"].value_counts(normalize=True).round(3))
