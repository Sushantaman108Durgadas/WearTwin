"""Replay a CSV of sensor windows as a simulated live feed and watch each soldier's digital twin update.

    python3 demo_replay.py                      # built-in 6-soldier squad (fresh simulated soldiers, not in training)
    python3 demo_replay.py --csv my_data.csv    # your own file

CSV columns: soldier_id (optional), hr, rmssd, spo2, rr, skin_t, amb_t, hum, imu  - one row per 30 s window,
in time order. The first 10 minutes (20 rows) of each soldier must be at rest: they set the personal baseline.
If the CSV also has `state` / `readiness` columns they are treated as ground truth and shown for comparison.
"""
import argparse
import time
from pathlib import Path

import numpy as np
import pandas as pd

from engine import SoldierTwin, load_bundle
from schema import CALIBRATION_WINDOWS, DT_MIN, INPUT_COLS
from simulate import make_soldier, simulate_session

ROOT = Path(__file__).parent
SQUAD = [("SOL-01", "heavy_load_hot"), ("SOL-02", "steady_march"), ("SOL-03", "hot_dehydration"),
         ("SOL-04", "altitude_climb"), ("SOL-05", "high_intensity"), ("SOL-06", "rest_duty")]


def make_demo_squad(path, seed=909):
    rng = np.random.default_rng(seed)
    frames = [simulate_session(make_soldier(rng), scen, rng, sid, sid) for sid, scen in SQUAD]
    pd.concat(frames, ignore_index=True).to_csv(path, index=False)


def first_episode(states, k=3):
    """(state, window index) at which the first run of k identical non-Normal states completes, else None."""
    run, prev = 0, None
    for i, s in enumerate(states):
        run = run + 1 if (s == prev and s not in (None, "Normal")) else 1
        prev = s
        if s not in (None, "Normal") and run >= k:
            return s, i
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default=str(ROOT / "data" / "demo_squad.csv"))
    ap.add_argument("--out", default=str(ROOT / "reports" / "demo_replay_log.csv"))
    ap.add_argument("--print-every", type=int, default=40, help="print the squad table every N windows (30 s each)")
    ap.add_argument("--delay", type=float, default=0.0, help="seconds to sleep per window, for a live feel")
    args = ap.parse_args()

    csv = Path(args.csv)
    if not csv.exists() and csv == ROOT / "data" / "demo_squad.csv":
        csv.parent.mkdir(exist_ok=True)
        make_demo_squad(csv)
    df = pd.read_csv(csv)
    if "soldier_id" not in df:
        df["soldier_id"] = "SOL-01"
    absent = [c for c in INPUT_COLS if c not in df]
    if absent:
        print(f"WARNING: columns missing from CSV, treated as sensor-offline: {absent}")
    has_truth = "state" in df and "readiness" in df
    scen = df.groupby("soldier_id", sort=False)["scenario"].first().to_dict() if "scenario" in df else {}

    bundle = load_bundle()
    groups = {sid: g.reset_index(drop=True) for sid, g in df.groupby("soldier_id", sort=False)}
    twins = {sid: SoldierTwin(sid, bundle) for sid in groups}
    last_state = {sid: "Normal" for sid in groups}
    log, n_steps = [], max(len(g) for g in groups.values())
    print(f"Replaying {len(groups)} soldier(s), {n_steps} windows ({n_steps * DT_MIN:.0f} min). "
          f"First {CALIBRATION_WINDOWS * DT_MIN:.0f} min = resting calibration.\n")

    for i in range(n_steps):
        now = []
        for sid, g in groups.items():
            if i >= len(g):
                continue
            out = twins[sid].update(g.iloc[i])
            if has_truth:
                out["true_state"], out["true_readiness"] = g.state.iloc[i], g.readiness.iloc[i]
            log.append(out)
            now.append(out)
            if out["status"] == "Live" and out["state"] != last_state[sid]:
                print(f"  [{out['t_min']:6.1f} min] {sid}: {last_state[sid]} -> {out['state']}  "
                      f"(readiness {out['readiness']:.0f}, {out['band']})")
                last_state[sid] = out["state"]
        if (i + 1) % args.print_every == 0 or i == n_steps - 1:
            print(f"\n  t = {now[0]['t_min']:.1f} min      soldier  state                readiness  band         "
                  f"thermal fatigue cardio" + ("   truth" if has_truth else ""))
            live = sorted(now, key=lambda o: (o["readiness"] is None, o["readiness"] if o["readiness"] is not None else 0))
            for o in live:
                if o["status"] == "Calibrating":
                    print(f"                  {o['soldier_id']:8s} Calibrating")
                    continue
                tr = f"   {o['true_state']}" if has_truth else ""
                print(f"                  {o['soldier_id']:8s} {o['state']:20s} {o['readiness']:7.0f}    {o['band']:11s}"
                      f"  {o['thermal_idx']:5.2f}  {o['fatigue_idx']:5.2f}  {o['cardio_idx']:5.2f}{tr}")
            print()
        if args.delay:
            time.sleep(args.delay)

    res = pd.DataFrame(log)
    Path(args.out).parent.mkdir(exist_ok=True)
    res.to_csv(args.out, index=False)

    print("=" * 96)
    print("SUMMARY (alert = confirmed after 3 consecutive identical predictions)")
    print(f"{'soldier':8s} {'scenario':16s} {'min readiness':>13s}  {'first alert (twin)':32s} " + ("first episode (ground truth)" if has_truth else ""))
    for sid, g in res.groupby("soldier_id", sort=False):
        live = g[g.status == "Live"].reset_index(drop=True)
        fa = first_episode(list(live["state"]))
        fa_txt = f"{fa[0]} @ {live['t_min'][fa[1]]:.1f} min" if fa else "none"
        tr_txt = ""
        if has_truth:
            ft = first_episode(list(live["true_state"]))
            tr_txt = f"{ft[0]} @ {live['t_min'][ft[1]]:.1f} min" if ft else "none"
        print(f"{sid:8s} {scen.get(sid, ''):16s} {live['readiness'].min():13.0f}  {fa_txt:32s} {tr_txt}")
    print(f"\nPer-window log saved to {args.out}")


if __name__ == "__main__":
    main()
