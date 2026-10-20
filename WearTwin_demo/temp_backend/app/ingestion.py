from .database import connection, utc_now, active_session_id
from .schemas import ReadingIn


def ingest_batch(readings: list[ReadingIn]):
    if not readings:
        return {"accepted":0,"duplicates":0,"session_id":None,"message":"Empty batch"}
    if len(readings)>500:
        raise ValueError("Batch too large; maximum is 500 readings")
    accepted=duplicates=0; epoch=0
    with connection() as c:
        sid=active_session_id(c)
        latest=c.execute("SELECT timestamp_ms, epoch_id FROM readings ORDER BY id DESC LIMIT 1").fetchone()
        if latest:
            epoch=int(latest["epoch_id"])
        last_ts=int(latest["timestamp_ms"]) if latest else None
        for r in readings:
            ts=r.timestamp_ms
            # A reset inside the incoming batch or since the last batch starts a new epoch.
            if last_ts is not None and ts < last_ts:
                epoch += 1
            last_ts=ts
            exists=c.execute("SELECT id FROM readings WHERE session_id IS ? AND epoch_id=? AND timestamp_ms=? LIMIT 1",(sid,epoch,ts)).fetchone()
            if exists:
                duplicates += 1
                continue
            c.execute("""INSERT INTO readings(session_id,timestamp_ms,epoch_id,received_at,ir,red,heart_rate_bpm,heart_rate_valid,
              spo2_percent,spo2_valid,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,roll,pitch)
              VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
              (sid,ts,epoch,utc_now(),r.ir,r.red,r.heart_rate_bpm,int(r.heart_rate_valid),r.spo2_percent,int(r.spo2_valid),
               r.accel_x,r.accel_y,r.accel_z,r.gyro_x,r.gyro_y,r.gyro_z,r.roll,r.pitch))
            accepted += 1
    return {"accepted":accepted,"duplicates":duplicates,"session_id":sid,"message":"Batch ingested"}
