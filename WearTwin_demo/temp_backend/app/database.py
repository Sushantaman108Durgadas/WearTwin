import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from .config import DB_PATH

SCHEMA = '''
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS sessions (
 id INTEGER PRIMARY KEY AUTOINCREMENT, started_at TEXT NOT NULL, ended_at TEXT,
 status TEXT NOT NULL CHECK(status IN ('active','stopped')), notes TEXT DEFAULT ''
);
CREATE TABLE IF NOT EXISTS readings (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 session_id INTEGER REFERENCES sessions(id), timestamp_ms INTEGER NOT NULL,
 epoch_id INTEGER NOT NULL DEFAULT 0, received_at TEXT NOT NULL,
 ir REAL, red REAL, heart_rate_bpm REAL, heart_rate_valid INTEGER NOT NULL DEFAULT 0,
 spo2_percent REAL, spo2_valid INTEGER NOT NULL DEFAULT 0,
 accel_x REAL, accel_y REAL, accel_z REAL, gyro_x REAL, gyro_y REAL, gyro_z REAL,
 roll REAL, pitch REAL
);
CREATE INDEX IF NOT EXISTS idx_readings_session_time ON readings(session_id, epoch_id, timestamp_ms);
'''

def utc_now():
    return datetime.now(timezone.utc).isoformat()

@contextmanager
def connection():
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH, timeout=10, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()

def init_db():
    with connection() as c:
        c.executescript(SCHEMA)
        # A process restart ends any stale active session cleanly.
        c.execute("UPDATE sessions SET status='stopped', ended_at=COALESCE(ended_at, ?) WHERE status='active'", (utc_now(),))

def active_session_id(conn):
    row = conn.execute("SELECT id FROM sessions WHERE status='active' ORDER BY id DESC LIMIT 1").fetchone()
    return row["id"] if row else None
