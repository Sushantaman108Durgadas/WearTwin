# WearTwin — local ESP32 monitoring demo

A local full-stack prototype for ESP32 + MAX30102 PPG + MPU6050 IMU. The backend accepts the existing firmware's JSON batch, stores readings in SQLite, exposes separate PPG/IMU endpoints, and runs a rolling-window PPG cleaning/filtering/experimental-estimation pipeline. The React dashboard polls API data and also connects to `/ws/live` for live notifications.

## Project structure

- `temp_backend/` — FastAPI, SQLite, signal processing and REST/WebSocket endpoints.
- `temp_frontend/` — React + Vite + TypeScript + Recharts + Lucide dashboard.
- `firmware/` — copy of the modular ESP32 firmware ZIP contents for reference; firmware itself is unchanged.

## 1. Run the backend on Windows

Open PowerShell:

```powershell
cd temp_backend
py -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

If PowerShell blocks activation, use `Set-ExecutionPolicy -Scope Process Bypass` in that terminal and activate again, or run `.venv\Scripts\python.exe -m pip install -r requirements.txt` and `.venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload`.

Check `http://localhost:8000/api/health` and interactive API docs at `http://localhost:8000/docs`.

SQLite is created automatically at `temp_backend/weartwin.sqlite3`. To use another path, set `WEARTWIN_DB_PATH` before starting the backend.

## 2. Run the frontend

In a second PowerShell terminal:

```powershell
cd temp_frontend
npm install
Copy-Item .env.example .env
npm run dev
```

Open `http://localhost:5173`.

## 3. Connect the ESP32

The laptop's current LAN IPv4 supplied for this setup is `10.115.143.22`. In the firmware `config.h`, set:

```cpp
#define BACKEND_BATCH_URL "http://10.115.143.22:8000/api/readings/batch"
```

Make sure `WIFI_SSID` and `WIFI_PASSWORD` are configured. Laptop and ESP32 must be on the same reachable Wi-Fi network. Verify the IP with `ipconfig` before each demo because DHCP can change it. Do not use `localhost` in the ESP32 URL. Windows Firewall may ask permission for Python; allow it on the trusted private network, or add a private-network inbound TCP rule for port 8000.

The browser frontend uses `http://localhost:8000`; the ESP32 uses the laptop's LAN IP. CORS allows Vite's localhost origins by default.

## 4. Data flow

1. `POST /api/readings/batch` accepts a JSON array or `{ "readings": [...] }`.
2. Validated samples are stored in SQLite. Samples received with no active session are retained with `session_id = null`; they are not silently discarded.
3. Start a session in the dashboard. New readings are associated with it.
4. The PPG endpoint queries up to 2,000 latest IR/RED rows, keeps only the newest timestamp epoch, handles duplicates, detects large gaps, resamples when possible, filters channels independently, and estimates HR via a 10-second Hann-window FFT.
5. The IMU endpoint exposes raw accelerometer/gyroscope/roll/pitch values. The browser refreshes periodically and connects to a WebSocket for live updates.

The live PPG implementation follows the known intent of the earlier CSV pipeline. The original `process_ppg.py` was not present among the attached files in this request, so filter/FFT settings are based on the project details provided and are clearly documented rather than claimed to be a byte-for-byte port. If you have a different current copy, compare the parameters before reporting exact equivalence.

## 5. Endpoints

- `GET /api/health`
- `POST /api/readings/batch`
- `GET /api/readings/latest`
- `GET /api/ppg/raw?limit=2000`
- `GET /api/ppg/processed?limit=2000`
- `GET /api/imu/latest?limit=500`
- `GET /api/sessions`
- `POST /api/sessions/start`
- `POST /api/sessions/stop`
- `GET /api/sessions/{session_id}`
- `GET /api/sessions/{session_id}/export.csv`
- `GET /api/processing/status`
- `GET /api/processing/config`
- `WS /ws/live`

## 6. Test a batch manually

PowerShell:

```powershell
$body = '[{"timestamp_ms":1000,"ir":120000,"red":85000,"heart_rate_bpm":72,"heart_rate_valid":true,"spo2_percent":98,"spo2_valid":true,"accel_x":0.01,"accel_y":0.02,"accel_z":9.79,"gyro_x":0.2,"gyro_y":-0.1,"gyro_z":0.05,"roll":0.3,"pitch":-0.5}]'
Invoke-RestMethod -Uri http://localhost:8000/api/readings/batch -Method Post -ContentType 'application/json' -Body $body
Invoke-RestMethod http://localhost:8000/api/health
```

This is a test payload, not a real sensor reading. Do not run synthetic test data during the live demonstration unless you intend to show a test.

## Important limitations

- The approximate `110 - 25Q` SpO₂ formula is uncalibrated and experimental, not clinically validated. Device and backend estimates are shown separately.
- FFT HR resolution for a 10-second window is about 6 BPM at 0.1 Hz frequency-bin spacing; motion can dominate the spectrum.
- Device timestamps are `millis()` since boot, not UTC. The backend records UTC receive time separately and starts a new epoch on timestamp reset.
- This demo doesn't diagnose disease or replace medical equipment.
- SQLite ingestion and processing are suitable for a student demo. For high-rate production use, optimize streaming and decouple processing more rigorously.
