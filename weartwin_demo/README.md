# WearTwin AI demo: sensor windows in, digital-twin state and readiness out

Goal of this stage: show that an AI model, fed digital sample data in the same format the ESP32 will later
send, produces a soldier "digital twin" output: a **state** (Normal / Fatigue / Thermal strain / Cardio-resp stress)
and a **readiness score** (0-100), updated every 30 s. No hardware is involved.

**Read this first.** All data here is *synthetic*. Labels come from physiology rules written into the generator
(`schema.py`, `simulate.py`). The model therefore shows that the *pipeline* works and learns those rules from noisy
sensor-style readings. It is **not evidence of real-world detection accuracy**. Real validation needs real
volunteer data (see "Next steps").

## Run it

**Easiest: Google Colab.** Open `WearTwin_demo.ipynb` in Colab (colab.research.google.com -> File -> Upload notebook), run the cells
top to bottom, and upload `weartwin_demo.zip` when asked. It trains the model in the notebook and shows tables and plots instead of
terminal text, including sliders for feeding your own values.

**Locally** (Windows: use `python` or `py`; Linux/macOS: `python3`), from inside this folder:

```bash
pip install -r requirements.txt
python train_eval.py         # ~5 min: generate data, train, evaluate, verification checks -> models/ and reports/metrics.json
python verify_engine.py      # streaming engine gives identical output to batch scoring
python make_inputs.py --timeline   # six hand-made scenarios, final state and how it evolves
python demo_replay.py        # replay a 6-soldier squad as a live feed (--delay 0.05 for a live feel)
python demo_replay.py --csv your_file.csv   # your own windows (columns below)
python make_figures.py       # figures in reports/
```

Run `train_eval.py` once on the machine you will present from. A model file is included (`models/weartwin_model.joblib`) but it was saved with
scikit-learn 1.9.1; other versions can load it with warnings, so retraining is the safe route.

**Windows "Application Control policy has blocked this file" (numpy `.pyd`/DLL):** Windows is refusing unsigned compiled files
(typically Smart App Control, or an IT-managed policy). It is not a bug in this project. Use Colab, or turn Smart App Control off
(Windows Security -> App & browser control -> Smart App Control settings) if your machine is not IT-managed.

## Input contract (what the ESP32 side will eventually send)

One row per 30 s window, in time order, per soldier:

| column | unit |
|---|---|
| hr | bpm |
| rmssd | ms (HRV over the window) |
| spo2 | % |
| rr | breaths/min |
| skin_t | deg C |
| amb_t | deg C |
| hum | % relative humidity |
| imu | g, mean absolute body acceleration (gravity removed) |

Missing values (sensor offline) are allowed and treated as unknown. The first 20 windows (10 min) of a feed must
be the soldier at rest: they set that soldier's personal baseline (the "twin"). Status is "Calibrating" until then.

## Files

| file | purpose |
|---|---|
| `schema.py` | input contract, state names, label rules, readiness formula |
| `simulate.py` | synthetic physiology generator: 8 scenarios, per-soldier baselines, noise, motion artefacts, dropouts |
| `features.py` | causal feature extraction (uses only past windows, so it works on a live stream) |
| `models.py` | gradient-boosting models, hand-written rule baseline, metrics |
| `train_eval.py` | training, leave-soldier-out evaluation, all verification checks |
| `engine.py` | streaming `SoldierTwin` class: `update(window)` returns state, readiness, indices, probabilities |
| `demo_replay.py`, `verify_engine.py`, `make_figures.py` | demo, equivalence check, figures |
| `make_inputs.py`, `plot_scenarios.py` | six hand-made scenarios (editable) and notebook plots |
| `WearTwin_demo.ipynb` | Colab notebook: upload, train, scenario plots, slider form for your own input, squad replay |
| `dashboard/index.html` | Interactive Holographic Digital Twin Dashboard (browser standalone, fluid readiness fill, sandbox & squad replay) |
| `SPEAKER_NOTES.md` | Complete presentation script & speaker notes for Slides 1–7 with talking points and Q&A prep |

## Results (held-out soldiers, calibration windows excluded)

48 soldiers train, 12 held-out test soldiers from the same generator, 12 more from a "shifted" population
(older, less fit, noisier sensors; same generator, so a mild shift only).

| | gradient boosting | hand-written rules |
|---|---|---|
| test macro-F1 | 0.842 | 0.487 |
| shifted macro-F1 | 0.868 | 0.523 |
| test per-class recall (Normal / Fatigue / Thermal / Cardio) | 0.87 / 0.90 / 0.89 / 0.95 | 0.55 / 0.20 / 0.80 / 0.94 |
| readiness MAE (points of 100), test / shifted | 4.5 / 5.8 | n/a |

Verification checks (all in `train_eval.py`, output in `reports/metrics.json`):
- **Leave-soldier-out only:** train, validation and test sets never share a soldier. 5-fold CV macro-F1 0.847 (+/- 0.028).
- **Shuffled labels:** macro-F1 0.235, balanced accuracy 0.269 (chance 0.25). Pass.
- **Perturbations after calibration:** HR +15 bpm, skin temp +1 C, RMSSD x0.6, SpO2 -4 %, resp rate +6 lower readiness on average
  (-2.0, -2.4, -9.0, -2.8, -14.2 points). Readiness rose by more than 2 points in at most 5.2 % of windows. Not a strict guarantee.
- **Sensor loss:** dropping any single sensor for a whole session keeps macro-F1 at 0.81-0.84, except **IMU, which falls to 0.71**
  (MAE 10.4). Training with sensor-loss augmentation raised macro-F1 by 0.05-0.06 when HR, respiration or IMU were missing, and
  changed it by between -0.03 and +0.00 for the other sensors.
- **Streaming equals batch:** `verify_engine.py` compared 1,506 windows, 0 mismatches.

## Known weaknesses (seen in this run)

- **Fatigue is mostly inferred from cumulative active minutes** (`active_min` is by far the top feature, permutation importance 0.23
  macro-F1 drop). That mirrors how the generator defines fatigue, and real fatigue will not follow it so neatly.
- **Alert timing is loose.** With 3-in-a-row alerts on held-out sessions: thermal strain detected in 29 of 33 sessions, fatigue 61 of 63,
  cardio-resp 31 of 32; false-alert sessions 5/87, 15/57, 5/88 respectively. For thermal strain the median alert comes 4.5 min before the ground-truth
  episode start (10th percentile 20.5 min before), so the twin often warns before the rule-defined episode begins; for fatigue and
  cardio-resp the median delay is 0 min.
- **In the 6-soldier demo** the twin missed the thermal episode of the dehydrated soldier (SOL-03: readiness fell to ~60 but the state stayed Normal),
  flagged SOL-04's cardio-resp stress about 13 min before the ground-truth label, and flickers between states during interval running.
  The readiness curve tracks the true one more closely than the state does.
- The rule baseline was written to be fixed in advance rather than tuned on generated data, but it was written alongside the generator, so treat the
  model-vs-rules gap as indicative only.
- Skin temperature is not core temperature. The thermal output is a "thermal strain indication", not a core-temperature estimate.

## Next steps

1. **Real data for a sanity check** (not yet done; the sandbox cannot download from these sites, so files would need to be provided):
   - [WESAD](https://archive.ics.uci.edu/dataset/465/wesad): 15 subjects; chest ECG, respiration, body temp, accelerometer (700 Hz) and wrist BVP/EDA/temp/accelerometer;
     labels neutral / stress / amusement. No SpO2, no ambient sensors, no fatigue or heat labels. Licence not stated on the UCI page; check the original site.
   - [PhysioNet wearable exercise-and-stress dataset](https://www.physionet.org/content/wearable-device-dataset/): Empatica E4 wristband (PPG-derived HR, accelerometer,
     skin temp, EDA) during stress tasks, aerobic cycling and Wingate-style anaerobic cycling; 30-36 volunteers per task; Open Data Commons Attribution licence; about 70 MB zipped.
     Useful for HR / skin-temp / activity behaviour under exertion; no ECG, SpO2 or respiration.
   - Neither labels heat strain or fatigue, so they can test parts of the signal pipeline, not the four states.
2. **Own ESP32 data** from consenting volunteers (rest, treadmill, warm room) with Borg RPE self-ratings as fatigue labels.
3. **Wiring:** ESP32 -> HTTP/MQTT -> FastAPI -> `SoldierTwin.update()` -> dashboard. `engine.py` already has this interface.
