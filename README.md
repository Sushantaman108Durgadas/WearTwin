# WearTwin

### A Wearable Multimodal Physiological Digital Twin for Soldier Operational Readiness, Casualty Triage, and Rehabilitation Monitoring.

WearTwin is a proposed wearable physiological monitoring system designed to combine
multimodal sensing, signal processing, feature extraction, state estimation, and
Digital Twin modeling into a continuously updated representation of a subject's
physiological and functional state.

> **Current development focus:** simulation, signal modeling, embedded data
> acquisition, signal processing, feature extraction, and Digital Twin architecture.

The eventual goal is a physical wearable system. At the current stage, the project
is being developed primarily through simulation and software prototypes so that the
sensing, processing, and modeling architecture can be investigated before hardware
integration.

---

# 1. Product Vision

WearTwin is intended to support three operational contexts:

| Mode | Main Question | Intended Output |
|---|---|---|
| **Operational Readiness** | Is the soldier becoming overstrained? | Readiness, fatigue, and thermal-strain state |
| **Casualty Triage** | Is the soldier deteriorating or immobile? | Priority and alert |
| **Recovery Camp** | Is the injured soldier improving safely? | Recovery trend and medic priority |

The same underlying wearable and physiological state model can therefore be
interpreted differently depending on the operating mode.

---

# 2. System Concept

The system is designed around a wearable sensing node that continuously acquires
physiological, motion, and environmental information.

```text
                    WearTwin
                       │
              ┌────────┴────────┐
              │                 │
       Wearable Node       Digital Twin
              │                 │
       Multimodal Data     Physiological State
              │                 │
              └────────┬────────┘
                       │
                 Decision Layer
                       │
              Medic / Commander
````

---

# 3. Wearable Node

Each test subject is intended to use one ESP32-based wearable node.

The wearable is responsible for:

* Sensor acquisition
* Local signal processing where appropriate
* Timestamping
* Data packaging
* Communication with the gateway or receiving system
* Monitoring signal quality and battery state

The initial development uses simulation to validate the data and processing
architecture before moving toward a physical wearable implementation.

---

# 4. Gateway Architecture

A Raspberry Pi can optionally be used as an edge gateway.

```text
[Wearable ESP32 Nodes]
          │
          │ BLE / Wi-Fi / LoRa
          ▼
[Raspberry Pi Edge Gateway]
          │
          ├── Data ingestion
          ├── Time synchronization
          ├── Signal processing
          ├── Feature extraction
          ├── AI inference
          ├── Digital Twin database
          └── Local dashboard
                    │
                    ▼
          [Medic / Commander Display]
```

The Raspberry Pi is optional because the architecture should also allow processing
to be distributed between the wearable, gateway, and higher-level systems.

---

# 5. Multimodal Data

The wearable packet is intended to contain:

```text
soldier_id
sequence_number
timestamp

ECG sample / ECG summary
PPG sample / PPG summary

accelerometer_x
accelerometer_y
accelerometer_z

gyroscope_x
gyroscope_y
gyroscope_z

skin_temperature
ambient_temperature
ambient_humidity

battery_level
signal_quality
```

The exact packet format will evolve as the sensor and communication architecture
is finalized.

---

# 6. Physiological Signal Pipeline

A central design principle of WearTwin is that raw sensor values should not
immediately be treated as physiological state.

The intended processing chain is:

```text
Raw Sensor Data
       │
       ▼
Signal Conditioning
       │
       ▼
Sampling / Synchronization
       │
       ▼
Digital Signal Processing
       │
       ▼
Feature Extraction
       │
       ▼
State Estimation
       │
       ▼
Digital Twin State
       │
       ▼
Decision / Visualization
```

This separation allows each stage to be independently investigated and validated.

---

# 7. Feature Extraction

During normal operation, meaningful features are extracted over sliding windows
rather than directly feeding raw sensor values into a model.

### Current proposed window

```text
Window length: 10 seconds
Overlap:        5 seconds
```

## 7.1 Physiological Features

* Mean heart rate
* Heart-rate slope
* RMSSD
* SDNN
* SpO₂ level and recent trend
* Respiration rate
* Respiration-rate variability
* Skin-temperature level and slope

## 7.2 Motion Features

* Acceleration magnitude
* Activity intensity
* Step frequency
* Stationary duration
* Walking/running classification
* Sudden fall or posture-change event
* Gait symmetry, if two IMUs are used

## 7.3 Environmental Features

* Ambient temperature
* Relative humidity
* Heat-index or apparent-temperature proxy
* Exposure duration
* Mission or rehabilitation-session duration

## 7.4 Signal-Quality Features

* ECG contact quality
* PPG confidence
* Motion-artifact score
* Missing-data percentage

---

# 8. Current ECG Development

The current prototype includes a simulated ECG source and an ESP32 acquisition
and processing pipeline.

```text
ECG Signal Simulator
        │
        ▼
     ECG_OUT
        │
        ▼
    ESP32 ADC
        │
        ▼
    CardialState
        │
        ├── Signal samples
        ├── R-peak detection
        ├── RR interval
        └── BPM
```

The current simulated ECG provides different heart-rate conditions:

```text
MODE 0 → 55 BPM
MODE 1 → 75 BPM
MODE 2 → 110 BPM
```

The simulated mode determines the generated ECG waveform. The ESP32 does not
receive the BPM value directly; it estimates BPM independently from the sampled
ECG waveform.

This allows the signal-processing pipeline to be tested as a measurement system
rather than simply passing the simulated heart rate directly to the controller.

---

# 9. Current IMU Development

The current prototype also includes an MPU6050-based inertial processing pipeline.

```text
MPU6050
   │
   ├── Accelerometer
   └── Gyroscope
          │
          ▼
    InertialState
          │
          ▼
     Kalman Filter
          │
          ▼
   Estimated Orientation
```

The current implementation provides the foundation for future motion and activity
features.

---

# 10. Digital Twin State

The Digital Twin is intended to maintain a higher-level representation of the
subject rather than simply storing sensor measurements.

A proposed state representation is:

```json
{
  "soldier_id": "S01",
  "mode": "operational",

  "cardiovascular_load": 0.72,
  "respiratory_load": 0.54,
  "thermal_strain": 0.81,
  "physical_fatigue": 0.68,

  "mobility_score": null,
  "recovery_score": null,

  "readiness_score": 0.39,
  "state": "HIGH_STRAIN",

  "confidence": 0.84,

  "recommended_action": "REST_AND_HYDRATE",

  "timestamp": "2026-09-11T23:00:00+05:30"
}
```

These values represent the proposed structure of the Digital Twin state.
They should not currently be interpreted as clinically validated measurements
or recommendations.

The state model will evolve as the underlying features, estimation methods,
datasets, and validation strategy are developed.

---

# 11. Digital Twin Architecture

The intended higher-level architecture is:

```text
                 SENSOR LAYER
                      │
       ┌──────────────┼──────────────┐
       │              │              │
      ECG            PPG            IMU
       │              │              │
       │       Temperature      Environment
       │              │              │
       └──────────────┼──────────────┘
                      ▼
             Signal Processing
                      │
                      ▼
             Feature Extraction
                      │
                      ▼
              State Estimation
                      │
                      ▼
              ┌──────────────┐
              │   WearTwin   │
              │     State    │
              └──────────────┘
                      │
             ┌────────┴────────┐
             ▼                 ▼
       Visualization      Decision Layer
```

The Digital Twin is therefore intended to represent an evolving physiological
and functional state derived from multimodal measurements.

---

# 12. Recommended Gateway Modules

The proposed Raspberry Pi gateway software can be organized as:

```text
gateway/
├── receiver.py
├── synchronizer.py
├── signal_processing.py
├── feature_extraction.py
├── state_estimator.py
├── digital_twin.py
├── decision_engine.py
├── database.py
└── dashboard.py
```

The module boundaries are architectural targets and may change as the prototype
develops.

---

# 13. Development Status

| Component                           | Status                      |
| ----------------------------------- | --------------------------- |
| ECG signal simulation               | 🟢 Implemented              |
| ECG mode selection                  | 🟢 Implemented              |
| ESP32 ECG acquisition               | 🟢 Implemented              |
| ECG R-peak detection                | 🟢 Implemented              |
| RR interval calculation             | 🟢 Implemented              |
| BPM calculation                     | 🟢 Implemented              |
| MPU6050 acquisition                 | 🟢 Implemented              |
| Kalman-based orientation estimation | 🟢 Implemented              |
| Multimodal synchronization          | 🟡 Under Investigation      |
| Physiological feature pipeline      | 🟡 Under Investigation      |
| Digital Twin state model            | 🟡 Architecture / Prototype |
| AI inference                        | 🔵 Planned                  |
| Raspberry Pi gateway                | 🔵 Planned                  |
| Local dashboard                     | 🔵 Planned                  |
| Physical wearable                   | 🔵 Planned                  |
| Hardware validation                 | 🔵 Planned                  |

**Legend**

* 🟢 Implemented — currently developed and tested
* 🟡 Under Investigation — architecture or implementation is being evaluated
* 🔵 Planned — part of the intended future system

---

# 14. Research & Validation Questions

WearTwin is being developed using a simulation-first approach. Important engineering
and physiological assumptions are therefore being investigated before being
treated as fixed design decisions.

## ECG

* What frequency bandwidth needs to be preserved for the intended analysis?
* Why is the selected sampling rate appropriate?
* What anti-aliasing requirements follow from the sampling rate?
* How do noise and baseline wander affect R-peak detection?
* How should P-QRS-T morphology be represented in simulation?
* How robust is R-peak detection under changing heart rate and noise?

## Multimodal Synchronization

* How should ECG, PPG, IMU, temperature, and environmental data with different
  sampling rates be synchronized?
* What timestamp model should the wearable use?
* How should missing or irregular samples be handled?

## Feature Extraction

* Which features provide meaningful physiological information?
* Which variables are directly measured and which are inferred?
* Is a 10-second window with 5-second overlap appropriate for each feature?
* How should signal quality influence feature confidence?

## Digital Twin

* What variables should constitute the Digital Twin state?
* How frequently should the state be updated?
* How should uncertainty and confidence be represented?
* How should historical state influence the current state?
* How should the three operating modes interpret the same underlying physiological
  state differently?

---

# 15. Repository Structure

The repository is organized around the current simulation and embedded development
workflow.

```text
WearTwin/
│
├── firmware/
│   ├── ...
│
├── simulation/
│   ├── ...
│
├── gateway/
│   ├── receiver.py
│   ├── synchronizer.py
│   ├── signal_processing.py
│   ├── feature_extraction.py
│   ├── state_estimator.py
│   ├── digital_twin.py
│   ├── decision_engine.py
│   ├── database.py
│   └── dashboard.py
│
├── datasets/
│   └── ...
│
├── docs/
│   └── ...
│
└── README.md
```

The exact directory structure will evolve as additional components are implemented.

---

# 16. Roadmap

```text
Phase 1
Signal Simulation
      │
      ▼
Phase 2
ESP32 Sensor Acquisition
      │
      ▼
Phase 3
Signal Processing
      │
      ▼
Phase 4
Feature Extraction
      │
      ▼
Phase 5
Multimodal Synchronization
      │
      ▼
Phase 6
Physiological State Model
      │
      ▼
Phase 7
Digital Twin
      │
      ▼
Phase 8
Gateway / Dashboard
      │
      ▼
Phase 9
Physical Wearable Prototype
      │
      ▼
Phase 10
System Validation
```

The roadmap is intentionally iterative. Research findings may change the
architecture and implementation of later phases.

---

# 17. Design Principles

### Simulation before hardware

The signal-processing and data architecture should be testable before committing
to the final physical hardware implementation.

### Measurement before inference

Raw sensor data should first pass through appropriate signal processing and
feature extraction before higher-level physiological states are inferred.

### Interpretable intermediate representations

Features should have a defined meaning rather than treating the entire system as
a black-box mapping from sensor values to decisions.

### Modular implementation

Sensor acquisition, filtering, feature extraction, state estimation, and Digital
Twin logic should remain independently testable.

### Explicit uncertainty

The system should distinguish between measured values, estimated variables, and
higher-level inferred states.

### Evidence-driven development

Important physiological and signal-processing assumptions should be investigated
and validated before being treated as fixed design decisions.

---

# 18. Limitations

The current project is a development prototype and simulation environment.

At this stage:

* The physical wearable has not been fully implemented.
* Physiological signals are simulated for development and testing.
* The current ECG processing pipeline is focused on fundamental measurements such
  as R-peak detection, RR interval, and BPM.
* The Digital Twin state variables are architectural concepts and are not yet
  clinically validated.
* AI inference is not yet implemented.
* The gateway and dashboard architecture is currently planned.
* Physiological thresholds and recommended actions require appropriate validation
  before any real-world medical or operational use.

WearTwin is therefore currently a **research and engineering prototype**, not a
clinical diagnostic or medical decision-making system.

---

# 19. Project Status

**Current focus:**

```text
Signal Simulation
       ↓
Embedded Acquisition
       ↓
Signal Processing
       ↓
Feature Extraction
       ↓
Multimodal Architecture
```
