import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[1]
DB_PATH = Path(os.getenv("WEARTWIN_DB_PATH", str(BASE_DIR / "weartwin.sqlite3")))
CORS_ORIGINS = os.getenv("WEARTWIN_CORS_ORIGINS", "http://localhost:5173,http://127.0.0.1:5173").split(",")
PPG_WINDOW_SAMPLES = int(os.getenv("WEARTWIN_PPG_WINDOW", "2000"))
TARGET_SAMPLE_RATE_HZ = float(os.getenv("WEARTWIN_TARGET_FS", "10"))
