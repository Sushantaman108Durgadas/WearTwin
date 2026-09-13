# WearTwin
WearTwin: A Wearable Multimodal Physiological Digital Twin for Soldier Operational Readiness, Casualty Triage, and Rehabilitation Monitoring. 

1) Three operating modes:

| Mode                  | Main question                             | Output                                   |
| --------------------- | ----------------------------------------- | ---------------------------------------- |
| Operational readiness | Is the soldier becoming overstrained?     | Readiness, fatigue, thermal-strain state |
| Casualty triage       | Is the soldier deteriorating or immobile? | Priority and alert                       |
| Recovery camp         | Is the injured soldier improving safely?  | Recovery trend and medic priority        |

2) Wearable node
Uses one ESP32-based wearable per test subject.

3) Uses Raspberry Pi as the gateway(optional)

4) Architecture:
   [Wearable ESP32 nodes]
        │
        │ BLE / Wi-Fi / LoRa
        ▼
[Raspberry Pi edge gateway]
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
       [Medic / commander display]

5) Wearable packet should contain:
   soldier_id
   sequence_number
   timestamp
   ecg_sample or ECG summary
   ppg_sample or PPG summary
   accelerometer_x, y, z
   gyroscope_x, y, z
   skin_temperature
   ambient_temperature
   ambient_humidity
   battery_level
   signal_quality

6) Feature mode
Used during normal operation:
  a) HR, HRV, respiration rate.
  b) Activity class.
  c) Temperature trend.
  d) Motion intensity.
  e) Signal-quality scores.

7) Recommended modules:
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

8) Extract meaningful features
   Do not send raw sensor values directly into a model and call the result a digital twin. First calculate interpretable features over sliding windows.
   Use a 10-second window with 5-second overlap.

   Physiological features
     Mean heart rate.
     Heart-rate slope.
     RMSSD and SDNN from ECG/PPG beat intervals.
     SpO₂ level and recent trend.
     Respiration rate.
     Respiration-rate variability.
     Skin-temperature level and slope.
   Motion features
     Acceleration magnitude.
     Activity intensity.
     Step frequency.
     Stationary duration.
     Walking/running classification.
     Sudden fall or posture-change event.
     Gait symmetry, if you use two IMUs.
   Environmental features
     Ambient temperature.
     Relative humidity.
     Heat-index or apparent-temperature proxy.
     Exposure duration.
     Mission or rehabilitation-session duration.
   Signal-quality features
     ECG contact quality.
     PPG confidence.
     Motion-artifact score.
     Missing-data percentage.
9) Define the Digital Twin state
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
