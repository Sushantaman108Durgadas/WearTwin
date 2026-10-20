from fastapi import APIRouter, HTTPException, Query
from ..database import connection
from ..schemas import ReadingIn
from ..ingestion import ingest_batch
from ..ppg_processing import process_ppg

router=APIRouter(prefix="/api", tags=["readings"])

@router.post('/readings/batch')
def post_batch(payload: list[ReadingIn] | dict):
    try:
        if isinstance(payload,dict): payload=[ReadingIn.model_validate(x) for x in payload.get('readings',[])]
        result=ingest_batch(payload)
        return {"ok":True,**result}
    except ValueError as e:
        raise HTTPException(400,str(e))

@router.get('/readings/latest')
def latest():
    with connection() as c:
        r=c.execute('SELECT * FROM readings ORDER BY id DESC LIMIT 1').fetchone()
        return {"reading":dict(r) if r else None,"has_data":r is not None}

@router.get('/ppg/raw')
def ppg_raw(limit:int=Query(2000,ge=1,le=5000),session_id:int|None=None):
    with connection() as c:
        if session_id is None:
            rows=c.execute('SELECT timestamp_ms,epoch_id,received_at,ir,red,heart_rate_bpm,heart_rate_valid,spo2_percent,spo2_valid FROM readings WHERE ir IS NOT NULL AND red IS NOT NULL ORDER BY id DESC LIMIT ?', (limit,)).fetchall()
        else:
            rows=c.execute('SELECT timestamp_ms,epoch_id,received_at,ir,red,heart_rate_bpm,heart_rate_valid,spo2_percent,spo2_valid FROM readings WHERE session_id=? AND ir IS NOT NULL AND red IS NOT NULL ORDER BY id DESC LIMIT ?', (session_id,limit)).fetchall()
    return {"samples":[dict(r) for r in reversed(rows)],"count":len(rows)}

@router.get('/ppg/processed')
def ppg_processed(limit:int=Query(2000,ge=1,le=5000),session_id:int|None=None):
    with connection() as c:
        if session_id is None:
            rows=c.execute('SELECT timestamp_ms,epoch_id,ir,red,heart_rate_bpm,heart_rate_valid,spo2_percent,spo2_valid FROM readings WHERE ir IS NOT NULL AND red IS NOT NULL ORDER BY id DESC LIMIT ?', (limit,)).fetchall()
        else:
            rows=c.execute('SELECT timestamp_ms,epoch_id,ir,red,heart_rate_bpm,heart_rate_valid,spo2_percent,spo2_valid FROM readings WHERE session_id=? AND ir IS NOT NULL AND red IS NOT NULL ORDER BY id DESC LIMIT ?', (session_id,limit)).fetchall()
    data=[dict(r) for r in reversed(rows)]
    # Process only one timestamp epoch to prevent mixing data across an ESP32 reboot.
    if data:
        epoch=data[-1]['epoch_id']; data=[r for r in data if r['epoch_id']==epoch]
    return process_ppg(data)

@router.get('/imu/latest')
def imu_latest(limit:int=Query(500,ge=1,le=2000),session_id:int|None=None):
    with connection() as c:
        if session_id is None:
            rows=c.execute('SELECT timestamp_ms,received_at,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,roll,pitch FROM readings ORDER BY id DESC LIMIT ?', (limit,)).fetchall()
        else:
            rows=c.execute('SELECT timestamp_ms,received_at,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,roll,pitch FROM readings WHERE session_id=? ORDER BY id DESC LIMIT ?', (session_id,limit)).fetchall()
    samples=[dict(r) for r in reversed(rows)]
    if len(samples)>1:
        import numpy as np
        ts=np.array([x['timestamp_ms'] for x in samples],float); d=np.diff(ts); d=d[d>0]
        fs=round(1000/float(np.median(d)),2) if len(d) else None
        gaps=int(np.sum(np.diff(ts)>150))
    else: fs=None; gaps=0
    return {"samples":samples,"count":len(samples),"sample_rate_hz":fs,"gap_count":gaps}
