# WearTwin AI: Complete Presentation Guide, Dashboard Walkthrough & Technical Defense

---

## Table of Contents
1. [System Architecture: How Google Colab and the Dashboard Connect](#1-system-architecture-how-google-colab-and-the-dashboard-connect)
2. [Dashboard Walkthrough: Component-by-Component Guide](#2-dashboard-walkthrough-component-by-component-guide)
3. [Example-Based Dry Run Simulations for Your Presentation](#3-example-based-dry-run-simulations-for-your-presentation)
4. [7-Slide Presentation Script & Speaker Notes](#4-7-slide-presentation-script--speaker-notes)
5. [Technical Defense Q&A: 15 Core Questions & In-Depth Answers](#5-technical-defense-qa-15-core-questions--in-depth-answers)

---

## 1. System Architecture: How Google Colab and the Dashboard Connect

A key question evaluators will ask is:
> *"What is the relationship between the Google Colab notebook (`WearTwin_demo.ipynb`) and this interactive web dashboard?"*

### The Two-Tier Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│                        TIER 1: GOOGLE COLAB                            │
│                 (Offline Training, Verification & Batch)                │
├────────────────────────────────────────────────────────────────────────┤
│  1. simulate.py: Generates 72 synthetic soldiers (48 train, 12 test,  │
│     12 shifted) with latent differential physiology equations.         │
│  2. features.py: Builds 34 causal streaming features.                  │
│  3. train_eval.py: Trains HistGradientBoostingClassifier & Regressors  │
│     with GroupKFold (leave-soldier-out) + sensor-loss augmentation.    │
│  4. Verification Suite: Shuffled labels, perturbation monotonicity,    │
│     sensor dropouts, and batch-vs-streaming bit parity checks.         │
│  5. Export Pipeline: Exports models, metrics (reports/metrics.json),   │
│     and squad logs (demo_squad.csv & demo_replay_log.csv).             │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Packaged into
                                    │ squad_data.js & twin_engine.js
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     TIER 2: INTERACTIVE DASHBOARD                      │
│                (Client-Side Edge Telemetry & Replay)                   │
├────────────────────────────────────────────────────────────────────────┤
│  • twin_engine.js: Exact JavaScript port of schema.py & features.py    │
│    (runs causal feature extraction, baseline deviation & twin logic).  │
│  • squad_data.js: Complete 102-min mission telemetry of all 6 squad    │
│    members (Devesh, Suyash, Priyanka, Sairaj, Kedar, Bharati).         │
│  • hologram.js: Procedural 3D holographic digital mesh & real-time    │
│    fluid wave dynamics simulation (100% full -> 0% empty).             │
│  • audio.js: Web Audio API tactical sound synthesis (ECG heartbeat,    │
│    strain alarms, fluid surge - 0 external audio files).               │
└────────────────────────────────────────────────────────────────────────┘
```

### How They Handshake
1. **Model & Metrics Validation:** Google Colab executes heavy computations that take ~5 minutes: training 5 gradient-boosted trees, running 5-fold cross-validation across 150,000+ data windows, and performing permutation importance. The outputs are saved to `metrics.json`.
2. **Telemetry Ingestion:** The 6-soldier mission generated in Colab (`demo_squad.csv`) and its corresponding model inferences (`demo_replay_log.csv`) are embedded in `squad_data.js`.
3. **Identical Math (Bit-Parity):** When a user moves sliders in the dashboard's "Biometric Sandbox," `twin_engine.js` runs the exact mathematical formulas from `schema.py` and `features.py` inside the browser in real time (under 1 millisecond per update), matching the batch results from Colab.

---

## 2. Dashboard Walkthrough: Component-by-Component Guide

### A. Top Navigation Header
* **Logo Badge `[W]` & Title `WearTwin AI`:** Brand identity for the tactical decision-support system.
* **`Biometric Sandbox` Tab:** Switches to interactive simulation mode. Here, the user has full manual control over all 8 sensor channels, presets, and duration ramps.
* **`Squad Telemetry Replay` Tab:** Switches to mission playback mode, replaying the 6 squad members with pre-computed telemetry from the Colab notebook.
* **`AI Verification` Button:** Opens the technical audit modal displaying Leave-Soldier-Out F1 scores, the confusion matrix, sensor dropout resilience data, and feature importance rankings.
* **`Audio: ON / OFF` Button:** Toggles the procedural Web Audio synthesizer. When active, it generates a real-time heartbeat thump synchronized to the soldier's current BPM, alert chimes on state transitions, and fluid energy surge effects.
* **`Fullscreen` Button:** Toggles browser fullscreen mode for presentation on projectors.

---

### B. Left Panel: Biometric Controls & Mission Presets (Sandbox Mode)

#### 1. Mission Profile Presets
Clicking any preset automatically initializes the 8 sensors and scenario duration to standard operational profiles:
* **`Rest / Guard`:** Baseline resting duty (HR 62 bpm, RMSSD 50 ms, SpO2 98%, RR 14, Skin 33.5°C, Amb 24°C, Hum 50%, IMU 0.02 g, Duration 30 min). State: `Normal`.
* **`Steady March`:** Moderate pack hike in mild weather (HR 100 bpm, RR 18, Skin 33.5°C, IMU 0.45 g, Duration 60 min). State: `Normal`.
* **`Heavy Load (Hot)`:** High thermal burden in 36°C heat with heavy backpack (HR 140 bpm, RMSSD 7 ms, Skin 37.1°C, IMU 0.25 g, Duration 60 min). State: `Thermal strain`.
* **`Altitude Climb`:** Rapid ascent above 3,500m (HR 135 bpm, SpO2 88%, RR 28, Amb 5°C, Duration 45 min). State: `Cardio-resp stress`.
* **`Cardio Stress`:** Near-maximal aerobic exertion (HR 175 bpm, RMSSD 18 ms, SpO2 91%, RR 36 brpm, IMU 0.60 g, Duration 30 min). State: `Cardio-resp stress`.
* **`Fatigue (2.5h)`:** Long-duration prolonged patrol (HR 118 bpm, RMSSD 14 ms, RR 25, IMU 0.30 g, Duration 150 min). State: `Fatigue`.
* **`Dehydrated March`:** High ambient heat (35°C), low humidity, zero fluid intake (HR 130 bpm, Skin 36.0°C, Duration 75 min).
* **`HI Intervals`:** Sprints alternating with short recovery (HR 180 bpm, RR 38, IMU 0.90 g, Duration 40 min). State: `Cardio-resp stress`.

#### 2. Live Sensor Channel Cards (8 Channels)
Each card represents one 30-second telemetric window sent by the soldier's wearable unit:
* **Heart Rate (bpm):** Range 40–210 bpm. Resting: 62 bpm. Shows live deviation badge (`+X bpm`).
* **HRV (RMSSD, ms):** Root Mean Square of Successive Differences between heartbeats. High at rest (50 ms), drops significantly under physical or autonomic stress (<15 ms).
* **SpO₂ Saturation (%):** Blood oxygen saturation from optical PPG. Healthy baseline is 98%. Drops under hypoxia, respiratory exhaustion, or high altitude (<90%).
* **Respiration Rate (brpm):** Breaths per minute. Resting: 14 brpm. Severe hyperventilation: >35 brpm.
* **Skin Temperature (°C):** Measured by surface thermistor. Resting: 33.5°C. In extreme heat or uncompensable strain, rises above 36.5°C.
* **Ambient Temperature (°C) & Humidity (%):** Environmental stress inputs used by Steadman's apparent temperature formula to compute ambient heat load.
* **Movement (IMU, g):** Mean absolute acceleration (gravity subtracted). Resting: 0.02 g. Walking: 0.3–0.5 g. Sprinting: >0.8 g.
* **Active/Offline Toggle Switch on Each Sensor:** Simulates broken or disconnected hardware. When toggled off, the card dims and passes `NaN` to the engine, allowing live demonstration of sensor-loss resilience.

#### 3. Ramp Simulation Parameters & Execution Buttons
* **Mission Duration Slider (10 to 180 min):** Controls how long the mission lasts. Strain builds progressively: the first 20 windows (10 min) are resting calibration, and values ramp smoothly from rest to your slider targets over the remaining duration.
* **`Run Scenario` Button:** Initiates animated time-stepped simulation. Steps through the windows at 45 ms per tick, dynamically updating the timeline, HUD, and holographic fluid level.
* **`Reset` Button:** Halts simulation, clears history, and restores the resting baseline.

---

### C. Left Panel: Squad Selector (Squad Telemetry Replay Mode)
Displays the 6 active squad members:
* **Devesh (`SOL-01`):** Heavy load in high heat. Shows thermal strain alert @ 48 min (truth: 46 min).
* **Suyash (`SOL-02`):** Steady march in mild weather. True negative (remains healthy throughout).
* **Priyanka (`SOL-03`):** Hot, dehydrated march. The critical "missed case" where dehydration did not cause an overt surface skin temperature spike.
* **Sairaj (`SOL-04`):** Altitude climb. Early warning cardio alert @ 27.5 min (ahead of ground truth @ 41 min).
* **Kedar (`SOL-05`):** High-intensity intervals. Spot-on cardio alert @ 13 min.
* **Bharati (`SOL-06`):** Guard duty. Clean resting profile.
* **Ground Truth Comparison Box:** Shows the true latent physiological state, true readiness, core body temperature $T_c$, and dehydration percentage directly from the simulator.

---

### D. Center Stage: Holographic Soldier & Fluid Readiness HUD

#### 1. HUD Readiness Banner
* **Readiness Score (0 to 100):** Prominent numeric readout calculated via:
  $$\text{Readiness} = 100 \times \left(1.0 - \left[0.7 \times \max(\text{thermal}, \text{fatigue}, \text{cardio}) + 0.3 \times \text{mean}(\text{thermal}, \text{fatigue}, \text{cardio})\right]\right)$$
* **Readiness Band Badge:**
  * `READY` (Score $\ge 75$): Green. Fully mission capable.
  * `CAUTION` (Score $50 - 74$): Amber. Performance degrading, monitor closely.
  * `REST / ROTATE` (Score $< 50$): Crimson. Critical risk of acute casualty. Rotate soldier immediately.
* **Confirmed State Pill:** Shows the debounced physiological state (`Normal`, `Fatigue`, `Thermal strain`, `Cardio-resp stress`). Requires 3 consecutive 30s predictions before transitioning to eliminate momentary sensor noise flickering.

#### 2. The Holographic Soldier Canvas (`hologram.js`)
* **Holographic Digital Mesh:** 60+ anatomical vertices forming the humanoid soldier silhouette, connected by triangulation wireframes.
* **Liquid Readiness Simulation:**
  * **100% Readiness:** Fluid fills the skeleton completely to the crown of the head. Shimmers with electric bio-cyan/emerald light, gentle surface waves, and rising bubbles.
  * **74%–50% (Caution):** Fluid drains down to the mid-abdomen and turns warm amber-gold. The upper torso skeleton is exposed as a dry, flickering wireframe.
  * **< 50% (Rest/Rotate):** Fluid drains to the knees and feet, shifting to emergency crimson red.
  * **0% (Exhaustion):** Fluid drains completely to the soles. The exposed wireframe skeleton flickers with red critical warning pulses.
* **Pulsating Organs:**
  * *Heart (Chest):* Rhythmic double-thump pulse (lub-dub) synchronized to current BPM.
  * *Lungs (Thorax):* Rhythmic expansion and contraction synchronized to Respiration Rate.
  * *Brain (Skull):* Synaptic flashes scaling with cognitive fatigue and thermal strain.
* **Interactive Hover Inspection:** Move your cursor over any body part (Head, Heart, Lungs, Abdomen, Limbs) to see a floating HUD callout with real-time localized physiological metrics.

#### 3. Bottom Severity Strain Gauges
Three continuous strain indices ($0.00$ to $1.00$). When any index crosses $0.50$, an operational alert is triggered:
* **Thermal Strain ($0.00 - 1.00$):** Piecewise linear scale of core temperature ($37.5^\circ\text{C} \rightarrow 0.0$, $38.3^\circ\text{C} \rightarrow 0.5$, $39.2^\circ\text{C} \rightarrow 1.0$).
* **Fatigue Index ($0.00 - 1.00$):** Latent fatigue level ($0.15 \rightarrow 0.0$, $0.50 \rightarrow 0.5$, $0.85 \rightarrow 1.0$).
* **Cardio Strain ($0.00 - 1.00$):** Worst of Heart Rate Reserve ($0.60/0.80/0.95$), SpO2 ($96/90/84\%$), or Respiration Rate ($25/35/45 \text{ brpm}$).

---

### E. Right Panel: Timelines, Derived Features & Chronology
* **Dual-Track Timeline Chart:** Displays the soldier's historical readiness curve over time. In Squad mode, it shows the **Predicted Twin Curve** (solid cyan) side-by-side with the **Ground Truth Curve** (dashed white), with $50$ and $75$ guide hairlines.
* **Playback Scrubber & Controls (Squad Mode):** Scrub to any minute in the 102-minute mission. Includes Play/Pause and variable speed toggles ($0.5\times, 1\times, 3\times, 10\times$).
* **Derived Causal Features Inspector:** Displays key engineered features:
  * `active_min`: Cumulative minutes of motion ($>0.12\text{ g}$). Permutation importance rank #1.
  * `hr_dev`: Current heart rate minus resting baseline median.
  * `apparent_temp`: Environmental heat load calculated from temperature and humidity.
  * `hr_per_imu`: Cardiac cost of movement (heart rate rise per unit of acceleration; flags cardiac drift).
* **Mission Chronology & Event Log:** Terminal-style feed recording state transitions, calibration completions, and alert announcements with timestamps.

---

### F. AI Model Verification & Audit Modal
Opened by clicking the **`AI Verification`** button in the top navigation bar:
* **Macro-F1 on Unseen Soldiers:** **0.842** (vs. **0.487** for hand-written threshold rules).
* **Readiness MAE:** **4.5 points** on a 0–100 scale.
* **Shuffled Labels Check:** Drops to **0.235** (chance: 0.25), proving the model learned true physiological relationships.
* **Top Feature Importances:** `active_min` (0.2314), `spo2` (0.0877), `skin_t` (0.0873), `rr_dev_mean_10m` (0.0411).
* **Sensor Dropout Resilience Table:** Shows Macro-F1 when channels fail completely.

---

## 3. Example-Based Dry Run Simulations for Your Presentation

Practice these three quick walkthroughs to guide your demonstration during the presentation:

---

### Dry Run 1: The Biometric Sandbox & Fluid Readiness Drainage (2 Minutes)

1. **Set Baseline:** Ensure the dashboard is in **Biometric Sandbox** mode. Click the **`Rest / Guard`** preset.
   * *What to show:* Readiness is $100$, state is `Normal`, fluid completely fills the holographic soldier to the head. Explain that the heart beats calmly at 62 bpm.
2. **Apply Acute Stress:** Click the **`Heavy Load (Hot)`** preset.
   * *What to point out:* The sliders jump to: HR 140 bpm, RMSSD 7 ms, Skin Temp 37.1°C, Ambient Temp 36°C.
   * *What happens on screen:* The fluid rapidly drains from the head down to the waist. Readiness drops to $48$ (`REST / ROTATE`), shifting from emerald-cyan to crimson red. The heart pulse accelerates visibly. The Thermal Strain gauge fills past the $0.50$ threshold.
3. **Execute Scenario Ramp:** Click **`Run Scenario`**.
   * *What to explain:* The model simulates the soldier at rest for 10 minutes to lock in their baseline, then ramps values smoothly over 60 minutes. Watch the timeline chart draw the readiness curve in real time as the event log announces state transitions.

---

### Dry Run 2: Simulating Sensor Hardware Failure (1 Minute)

1. **Disconnect Sensor:** While the soldier is under exertion, click the toggle switch on the **Movement (IMU)** card to switch it off.
   * *What to point out:* The card dims. The log announces `Sensor [IMU] lost! Simulating channel dropout.`
   * *What happens:* The digital twin does not crash. Explain: *"Because we trained the HistGradientBoosting model with 25% random sensor-loss augmentation, the tree splits know how to handle missing (`NaN`) values by routing to alternate surrogate features like Heart Rate and Respiration Rate."*
2. **Show Verification Modal:** Click **`AI Verification`** and show the sensor robustness table where Macro-F1 remains above $0.81$ for physiological sensors and $0.705$ for IMU.

---

### Dry Run 3: Squad Telemetry Replay — Devesh vs. Priyanka (2 Minutes)

1. **Switch Mode:** Click the **`Squad Telemetry Replay`** tab.
2. **Select Devesh (`SOL-01`):** Click Devesh's card.
   * *Narrate:* *"Devesh is on a heavy load march in 35°C heat. As we scrub forward to minute 48, his skin temperature and heart rate rise. At minute 48.0, the digital twin raises a Thermal strain alert. Ground truth says the episode started at minute 46.0 — that's a 2-minute detection response."*
3. **Select Priyanka (`SOL-03`):** Click Priyanka's card.
   * *Narrate:* *"Now look at Priyanka. She is on a hot march while dehydrated. Ground truth indicates a thermal strain episode at minute 78.0, but the digital twin stays in Normal state. Why? Dehydration caused her internal core temperature to rise, but her surface skin temperature didn't spike. This is a known limitation of surface wearables: skin temperature is not core temperature. We present this openly because real engineering requires acknowledging data boundaries."*

---

## 4. 7-Slide Presentation Script & Speaker Notes

Use this script during your presentation:

---

### Slide 1: Problem Statement & Operational Need
* **Slide Title:** *WearTwin: AI-Powered Digital Twin for Soldier Readiness Monitoring*
* **Opening Hook:**
  > *"Imagine you are a platoon commander in 40-degree heat. One of your soldiers begins to lag behind. Is she dehydrated? Fatigued? Or just pacing herself? Today, leaders rely on scheduled rest breaks and soldier self-reporting. But soldiers routinely under-report distress until heat exhaustion or cardiovascular collapse has already occurred."*
* **Key Points:**
  * Heat casualties and cardio-respiratory failure are preventable, but current doctrine lacks individual-level, continuous monitoring.
  * Standard population thresholds fail because people have different resting baselines: an HR of 150 bpm is moderate for an elite athlete, but near-exhaustion for an older soldier.
* **The Solution:** An on-body digital twin that interprets incoming vitals relative to the individual's baseline, providing continuous readiness scoring.
* **Transition:** *"Let's look at how data moves from soldier to command dashboard."*

---

### Slide 2: End-to-End System Architecture
* **Slide Title:** *Data Contract & Personal Calibration*
* **Talking Points:**
  * **Input Contract:** 8 commodity sensor channels sampled every 30 seconds: Heart Rate, HRV (RMSSD), SpO2, Respiration Rate, Skin Temperature, Ambient Temperature, Humidity, and IMU Acceleration.
  * **30-Second Window:** Balances responsiveness with artifact filtering, smoothing out transient motion spikes.
  * **Personal Baseline Calibration:** The first 20 windows (10 minutes) at rest define the individual's resting median. From then on, the twin measures *deviations from personal baseline*, not arbitrary population cutoffs.
  * **Three Outputs:**
    1. Categorical State: Normal, Fatigue, Thermal Strain, or Cardio-Respiratory Stress.
    2. Readiness Score: Continuous $0 - 100$ scale ($75+$ Ready, $50-75$ Caution, $<50$ Rest/Rotate).
    3. Three Strain Indices: Continuous severity ratings ($0.0 - 1.0$) for Thermal, Fatigue, and Cardio strain.
* **Transition:** *"Raw sensors are noisy; the intelligence comes from causal feature engineering."*

---

### Slide 3: Causal Feature Engineering (34 Features)
* **Slide Title:** *Domain-Engineered Causal Features for Streaming Inference*
* **Talking Points:**
  * **Strict Streaming Causality:** Every feature at window $t$ uses strictly windows $0..t$. There is zero look-ahead or batch leakage.
  * **Key Feature Families:**
    * *Baseline Deviations:* `hr_dev` (heart rate rise above resting), `rmssd_lr` (log-ratio drop in HRV), `skin_dev`.
    * *Temporal Summaries:* 3-minute and 10-minute trailing rolling medians. Using medians for HR prevents optical cadence-lock spikes from triggering false alarms.
    * *Cardiac Cost of Motion:* `hr_per_imu` ($(\Delta\text{HR}) / (\text{IMU} + 0.1)$). High heart rate with low movement indicates cardiovascular drift or heat strain.
    * *Exposure Memory:* `active_min` and `heat_exposure_min` act as cumulative fatigue timers.
  * **Bit-Parity Verification:** Streaming engine outputs matched batch scoring across 1,506 windows with zero mismatches.
* **Transition:** *"With 34 features defined, let's examine model training and validation."*

---

### Slide 4: Model Training & Evaluation
* **Slide Title:** *HistGradientBoosting with Leave-Soldier-Out Splits*
* **Talking Points:**
  * **Cohort Partitioning:** 48 training soldiers, 12 held-out test soldiers from the same population, and 12 shifted soldiers (older, less fit, noisier sensors). Train and test sets never share a soldier (`GroupKFold`).
  * **Results on Held-Out Test Soldiers:**
    * State Classification Macro-F1: **0.842** (vs. **0.487** for hand-written threshold rules).
    * Readiness Score MAE: **4.5 points** out of 100.
    * 5-Fold Cross-Validation: **$0.847 \pm 0.028$**, showing consistent stability.
  * **Sensor-Loss Augmentation:** 25% of training sessions randomly masked out a sensor channel, teaching the model redundant physiological pathways for when hardware fails in the field.
* **Transition:** *"Good metrics are not enough; we ran four verification checks to test model behavior."*

---

### Slide 5: The Four Verification Checks
* **Slide Title:** *Robustness & Sanity Checks*
* **Talking Points:**
  * **1. Shuffled-Label Sanity Check:** Retrained the model with randomly shuffled labels. Macro-F1 collapsed to **0.235** (chance is 0.25), proving the model learns true physiological relationships rather than memorizing noise.
  * **2. Perturbation Monotonicity:** Added physiological stressors after calibration: HR $+15$ bpm, Skin $+1.0^\circ\text{C}$, RMSSD $\times 0.6$, SpO2 $-4\%$, Resp Rate $+6$. Every stressor lowered readiness as expected ($\Delta\text{RR}+6 \rightarrow -14.2$ points).
  * **3. Sensor Dropout Resilience:** Dropping any single sensor for an entire mission keeps F1 at **0.81–0.84**, except IMU, which drops to **0.705**. Losing motion awareness hurts classification because movement contextualizes heart rate.
  * **4. Feature Importance:** Permutation importance shows `active_min` (0.231), `spo2` (0.088), and `skin_t` (0.087) as the primary drivers.
* **Transition:** *"Now let's see the system in action with our interactive dashboard."*

---

### Slide 6: Live Squad Demonstration
* **Slide Title:** *Squad Mission Telemetry & Replay*
* **Talking Points (Switch to Dashboard):**
  * Present Devesh (`SOL-01`): Heavy load march in 36°C heat. Point out the thermal alert triggered at 48 min (truth: 46 min).
  * Present Sairaj (`SOL-04`): Altitude climb. Cardio stress flagged early at 27.5 min (truth: 41 min), providing an early warning.
  * Present Priyanka (`SOL-03`): Dehydrated march. Acknowledge that the twin missed her thermal episode because dehydration did not trigger a surface skin temperature spike. Emphasize that surface temperature is a proxy, not core body temperature.
  * Demonstrate the Biometric Sandbox: Move sliders, show the fluid readiness level responding dynamically, and trigger a sensor disconnect.
* **Transition:** *"Where do we go from here?"*

---

### Slide 7: Scientific Limitations & Path to Real Volunteers
* **Slide Title:** *Data Boundaries & Future Roadmap*
* **Talking Points:**
  * **All Current Data is Synthetic:** The `simulate.py` physics generator is biologically plausible, but it demonstrates pipeline viability, not clinical efficacy.
  * **`active_min` Stopwatch Dominance:** The model relies heavily on cumulative active time for fatigue. Real fatigue involves sleep debt, nutrition, and cognitive load that IMUs cannot detect.
  * **Public Wearable Datasets (WESAD, PAMAP2, PPG-DaLiA):** Studied as potential benchmarks; however, none have the 8-sensor suite or military-specific fatigue/thermal labels.
  * **Next Steps:**
    1. Human volunteer trials on treadmills in climate chambers with Borg RPE fatigue ratings.
    2. ESP32 hardware firmware integration over MQTT/BLE.
    3. FastAPI microservice backend connected to this dashboard for live field deployment.
* **Closing:**
  > *"WearTwin demonstrates an end-to-end wearable digital-twin pipeline. With synthetic validation complete, the next phase is validating with real human trials."*

---

## 5. Technical Defense Q&A: 15 Core Questions & In-Depth Answers

Use these prepared responses for questions during your evaluation:

---

### Q1: Why did you use gradient boosted decision trees (HistGradientBoosting) instead of Deep Learning (LSTM, Transformers, or CNNs)?
**Answer:**
1. **Sample Efficiency on Tabular/Summary Data:** Our input contract consists of 34 engineered domain features extracted from 30s summary windows, rather than raw 100 Hz waveforms. On structured tabular data, Gradient Boosted Trees consistently match or outperform neural networks without requiring millions of parameters.
2. **Native Missing Value Handling:** Real wearable sensors disconnect frequently due to sweat, motion artifacts, or dead batteries. `HistGradientBoosting` natively handles `NaN` values by routing them to whichever child split minimizes loss, without needing crude mean or median imputation that distorts feature distributions.
3. **Execution Latency & Edge Footprint:** HistGradientBoosting evaluates decisions via binary threshold lookups. Inference takes under 5 microseconds per window, easily fitting within the memory constraints of an ESP32 or small edge gateway.
4. **Monotonic Constraints:** HistGradientBoosting supports monotonic constraints (`monotonic_cst`), guaranteeing that increases in heart rate or drops in SpO2 can never cause readiness to rise counterintuitively.

---

### Q2: What are decision tree training methods and how does HistGradientBoosting work?
**Answer:**
1. **Histogram Binning:** Traditional gradient boosting (like standard GBDT) scans all sorted feature values to find split points, which is $O(n_{\text{samples}} \times n_{\text{features}})$. `HistGradientBoosting` bins continuous features into 256 integer bins (histograms). Splitting evaluates bin boundaries in $O(n_{\text{bins}})$, running 10 to 50 times faster.
2. **Gradient Boosting Mechanism:** Trees are trained sequentially. Each new tree fits the negative gradient (pseudo-residuals) of the loss function (cross-entropy for classification, squared error for regression) with respect to the current ensemble's predictions.
3. **Shrinkage & Regularization:** Each tree's contribution is scaled by a learning rate ($\eta = 0.08$) and penalized by $L_2$ regularization on leaf values and minimum sample requirements per leaf (`min_samples_leaf=80`), preventing over-specialization to specific individuals.

---

### Q3: What exact hyperparameters were used for model training?
**Answer:**
From `models.py`:
* `max_iter`: 200 boosting iterations (trees).
* `learning_rate`: 0.08 (step shrinkage).
* `max_leaf_nodes`: 24 (controls tree complexity and limits overfitting).
* `min_samples_leaf`: 80 (prevents creating leaves based on a few outlier windows).
* `l2_regularization`: 1.0 (regularizes leaf weights).
* `class_weight`: `"balanced"` (inversely balances class frequencies so rarer states like Thermal Strain and Cardio Stress receive equal loss weight).
* `early_stopping`: False (ensures consistent comparisons across folds).

---

### Q4: How is the synthetic data created in `simulate.py`?
**Answer:**
Data generation uses coupled differential equations modeling five physiological states:
1. **Effort Lag:** Exponential smoothing of physical demand ($\tau \approx 1\text{ min}$).
2. **Core Body Temperature ($T_c$):** Newton's law of cooling modified by metabolic heat production, ambient apparent temperature, and dehydration:
   $$\frac{dT_c}{dt} = \frac{T_{c,\text{eq}}(\text{effort}, \text{heat}, \text{dehydration}) - T_c}{\tau_{T_c}}$$
3. **Dehydration Accumulation:** Cumulative sweat rate calculated as a function of metabolic effort and heat load, minus simulated fluid consumption.
4. **Fatigue Accumulation:** Power law based on sustained effort and thermal strain:
   $$\frac{df}{dt} = \text{acc} \cdot (1 - f) - \text{recovery} \cdot f$$
5. **Sensor Noise & Motion Artifacts:** True physiological vitals are mapped to sensor outputs with realistic noise, motion-induced dropouts, and step-cadence locking (PPG false heart rates under rhythmic running).

---

### Q5: How will you use public datasets like WESAD in the future?
**Answer:**
* **What WESAD Contains:** 15 subjects wearing chest (RespiBAN) and wrist (Empatica E4) sensors measuring ECG (700 Hz), respiration, skin temperature, accelerometer, and EDA under neutral, stress, and amusement conditions.
* **The Domain Gap:** WESAD was collected in quiet seated laboratory settings with cognitive stress (mental arithmetic, public speaking). It lacks physical march exertion, high ambient heat, military load carriage, and SpO2 sensors.
* **How We Will Use It:**
  1. *Signal-to-Feature Verification:* Validate our HRV extraction algorithms (RMSSD from inter-beat intervals) against WESAD's 700 Hz lab ECG to quantify PPG error margins during motion.
  2. *Transfer Learning / Pre-training:* Pre-train feature extractors on WESAD's physiological distributions before fine-tuning on our target soldier dataset.

---

### Q6: How are the Google Colab notebook and the Dashboard connected?
**Answer:**
1. **Notebook Execution:** The Colab notebook runs data generation, model training, Leave-Soldier-Out cross-validation, and saves model outputs to CSV and JSON files (`reports/metrics.json` and `demo_replay_log.csv`).
2. **Dashboard Deployment:** The dashboard runs in any web browser and incorporates:
   * `squad_data.js`: Contains the telemetry and predictions generated by the Colab notebook for the 6 squad members.
   * `twin_engine.js`: A JavaScript port of `schema.py` and `features.py` that replicates the notebook's feature extraction and digital-twin calculations directly in the browser, matching batch outputs with zero discrepancies.

---

### Q7: How does the interactive dashboard work under the hood?
**Answer:**
* **Frontend Architecture:** Self-contained vanilla HTML5, CSS3, and JavaScript with zero external frameworks or internet dependencies.
* **Canvas Hologram (`hologram.js`):** Uses an HTML5 Canvas rendering loop running at 60 FPS. It models 60+ anatomical vertices connected by wireframe edges and applies a sinusoidal fluid wave simulation clipped to the soldier's silhouette.
* **Twin Engine (`twin_engine.js`):** Maintains a rolling historical buffer of sensor windows. When a slider moves, it computes apparent temperature, baseline deviations, rolling medians, strain indices, and the readiness score.
* **Tactical Audio (`audio.js`):** Generates sound effects procedurally using the Web Audio API's oscillators and gain envelopes, creating heartbeat audio, strain alarm chimes, and fluid surge sounds without relying on external MP3 files.

---

### Q8: Why is the 10-minute calibration period necessary?
**Answer:**
Human physiology varies widely between individuals:
* Resting heart rates range from 48 bpm in endurance athletes to 80 bpm in average adults.
* Normal resting skin temperature ranges from 32.0°C to 34.5°C based on ambient temperature and peripheral perfusion.
* Resting HRV (RMSSD) varies from 20 ms to 100 ms depending on age and autonomic tone.

If we evaluated raw heart rate without calibration, a soldier with a natural baseline of 50 bpm reaching 110 bpm would show a 60 bpm elevation, while a soldier with a baseline of 80 bpm reaching 110 bpm would show only a 30 bpm elevation. Interpreting vitals relative to the individual's resting baseline normalizes these differences.

---

### Q9: Why did the model miss Priyanka's (`SOL-03`) dehydration episode?
**Answer:**
This is an important limitation to discuss:
In scenario `hot_dehydration`, Priyanka's internal core body temperature rose past $38.5^\circ\text{C}$ due to dehydration and metabolic effort, but her surface skin temperature did not show a sharp spike. Because non-invasive wearables measure *surface skin temperature* rather than internal core body temperature, the model did not detect enough surface thermal signal to push the thermal strain index past the $0.50$ alert threshold.

This highlights an important takeaway: **skin temperature is a proxy, not core body temperature**. Resolving this in production requires integrating physiological estimators (like the Buller-Tharion core temperature model) or non-invasive heat-flux sensors.

---

### Q10: Why does losing the IMU sensor degrade Macro-F1 more than any other sensor (down to 0.705)?
**Answer:**
As shown in our verification checks:
* An elevated heart rate of 150 bpm can mean two completely different things:
  1. If IMU is $0.8\text{ g}$ (sprinting), this is a normal cardiovascular response.
  2. If IMU is $0.02\text{ g}$ (standing still), this indicates severe heat exhaustion, panic, or cardiac decompensation.

Without the IMU accelerometer, the model cannot contextualize physiological exertion against kinetic effort. Losing this signal removes the model's ability to distinguish expected cardiovascular demand from pathological strain.

---

### Q11: What is cardiac drift and how does the feature `hr_per_imu` capture it?
**Answer:**
* **Cardiac Drift:** During prolonged steady exercise (especially in heat), heart rate gradually drifts upward by 10–20% even when workload (speed/pace) remains constant. This occurs because sweating reduces blood plasma volume, lowering stroke volume and forcing the heart to beat faster to maintain cardiac output.
* **`hr_per_imu`:** Measures heart rate elevation relative to kinetic acceleration:
  $$\text{hr\_per\_imu} = \frac{\Delta\text{HR}_{10\text{m}}}{\text{IMU}_{10\text{m}} + 0.1}$$
  If work output remains flat while heart rate climbs, `hr_per_imu` rises significantly, allowing the model to detect developing heat strain and dehydration before core temperature spikes dangerously.

---

### Q12: Why did you use a 3-window debounce filter and EMA smoothing in `engine.py`?
**Answer:**
1. **EMA Smoothing ($\alpha = 0.35$):** Raw tree predictions can fluctuate from window to window due to sensor noise. Exponential Moving Average smoothing produces stable readiness trends without introducing lag.
2. **Debounce Filter ($k = 3$):** Wearable sensors produce occasional motion artifacts (e.g., optical PPG misreads during adjustments). Requiring 3 consecutive identical raw states (90 seconds of consistent evidence) prevents temporary spikes from triggering false alarms.

---

### Q13: How did you ensure there was no data leakage during feature extraction?
**Answer:**
* **Causal Feature Extraction:** All rolling features (3m median, 10m mean, 5m slope) use strictly past and current windows ($0..t$). We do not use centered rolling windows or future-looking metrics.
* **GroupKFold by Soldier:** Data is split by soldier identity, not by randomly shuffling windows. Randomly splitting windows from the same time series causes the model to memorize individual-specific traits rather than learning generalizable physiology.
* **Causal Baseline Freezing:** Personal baselines are computed as expanding medians over the first 20 windows, then frozen. Subsequent exertion windows cannot alter the baseline.

---

### Q14: Is this system intended for clinical diagnosis or operational decision support?
**Answer:**
WearTwin is an **operational decision-support tool**, not a medical diagnostic device:
* It does not diagnose specific medical conditions or prescribe interventions.
* It provides commanders and squad leaders with an objective readiness score to inform tactical pacing, hydration breaks, and work-rest cycles.
* This framing places it under administrative decision-support guidelines rather than strict Class II/III medical device regulations.

---

### Q15: Can this system run directly on an ESP32 microcontroller in the field?
**Answer:**
Yes, by design:
1. **Lightweight Computation:** A 200-tree gradient boosted ensemble can be converted into C arrays of nested if-else statements or flattened index lookups (using tools like `m2cgen` or `MicroMLP`).
2. **Low Memory Footprint:** The entire ensemble takes under 250 KB of flash memory. The ESP32 provides 520 KB of SRAM and 4 MB of Flash, comfortably accommodating the model, rolling window buffer, and BLE/WiFi stack.
3. **Execution Time:** Running inference every 30 seconds requires less than 2 milliseconds of active CPU time, allowing the ESP32 to remain in deep sleep between windows to conserve battery life.

---

### Summary Checklist for Presentation Day
* Open **`dashboard/index.html`** in your browser and verify that all 6 squad member names (Devesh, Suyash, Priyanka, Sairaj, Kedar, Bharati) are displayed.
* Test toggling **Audio** and switching to **Fullscreen**.
* Run through the **Heavy Load (Hot)** preset to confirm the fluid drains smoothly.
* Review **Dry Run 3** (Devesh vs. Priyanka) to present the hit and miss cases comfortably.
* Keep this document open on a secondary screen or print it for reference during questions.
