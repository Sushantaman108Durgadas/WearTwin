"""Feed hand-made sensor values to the twin and print the result for each scenario.

    python make_inputs.py              # table of final states
    python make_inputs.py --timeline   # also print how the state evolves during each scenario

Each scenario = 20 resting windows (the personal baseline) followed by N windows (30 s each) whose values move in a
straight line from START to END. Strain builds up gradually in real life, so ramps are more realistic than constants.
Edit SCENARIOS to try your own inputs.
"""
import argparse

import pandas as pd

from engine import SoldierTwin, load_bundle
from schema import INPUT_COLS

REST = dict(hr=62, rmssd=50, spo2=98, rr=14, skin_t=33.5, amb_t=24, hum=50, imu=0.02)


def v(hr, rmssd, spo2, rr, skin_t, amb_t, hum, imu):
    return dict(hr=hr, rmssd=rmssd, spo2=spo2, rr=rr, skin_t=skin_t, amb_t=amb_t, hum=hum, imu=imu)


# name: (windows after the rest period, START values, END values)   -- 2 windows = 1 minute
SCENARIOS = {
    "1 Rest":            (60,  REST,                                      REST),
    "2 March, mild":     (60,  v(95, 42, 98, 17, 33.5, 15, 50, 0.45),     v(100, 40, 98, 18, 33.5, 15, 50, 0.45)),
    "3 Heavy load, hot": (120, v(110, 15, 98, 22, 34.5, 34, 50, 0.30),    v(140, 7, 98, 29, 37.1, 34, 50, 0.25)),
    "4 Altitude":        (60,  v(120, 30, 94, 22, 32.0, 5, 40, 0.40),     v(135, 25, 88, 28, 32.0, 5, 40, 0.40)),
    "5 Cardio stress":   (40,  v(140, 25, 95, 28, 34.0, 25, 50, 0.60),    v(170, 22, 91, 34, 34.0, 25, 50, 0.60)),
    "6 Fatigue (2.5 h)": (300, v(100, 25, 98, 18, 33.5, 25, 50, 0.30),    v(118, 14, 98, 25, 34.5, 25, 50, 0.30)),
}


def ramp(start, end, n):
    return [{c: start[c] + (end[c] - start[c]) * i / max(n - 1, 1) for c in INPUT_COLS} for i in range(n)]


def run(name, n, start, end, bundle):
    twin = SoldierTwin(name, bundle)
    rows = [REST] * 20 + ramp(start, end, n)
    return [twin.update(r) for r in rows]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeline", action="store_true", help="print the state every 10 minutes of each scenario")
    args = ap.parse_args()
    bundle = load_bundle()
    table = []
    for name, (n, start, end) in SCENARIOS.items():
        log = run(name, n, start, end, bundle)
        live = [o for o in log if o["status"] == "Live"]
        last = live[-1]
        table.append(dict(scenario=name, minutes=round(n * 0.5), final_state=last["state"],
                          final_readiness=round(last["readiness"]), min_readiness=round(min(o["readiness"] for o in live)),
                          band=last["band"]))
        if args.timeline:
            print(f"\n{name}")
            for o in live[::20]:
                print(f"  t={o['t_min']:6.1f} min  {o['state']:20s} readiness {o['readiness']:5.1f}")
    print()
    print(pd.DataFrame(table).to_string(index=False))


if __name__ == "__main__":
    main()
