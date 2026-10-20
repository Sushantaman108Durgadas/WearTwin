from fastapi import APIRouter
from ..database import connection
from ..config import PPG_WINDOW_SAMPLES, TARGET_SAMPLE_RATE_HZ
router=APIRouter(prefix='/api/processing',tags=['processing'])
@router.get('/status')
def status():
    with connection() as c:
        n=c.execute('SELECT COUNT(*) n FROM readings WHERE ir IS NOT NULL AND red IS NOT NULL').fetchone()['n']
        latest=c.execute('SELECT received_at FROM readings ORDER BY id DESC LIMIT 1').fetchone()
    return {'status':'warming_up' if n<100 else 'ready','stored_ppg_samples':n,'window_capacity':PPG_WINDOW_SAMPLES,'last_received_at':latest['received_at'] if latest else None}
@router.get('/config')
def config():
    return {'window_samples':PPG_WINDOW_SAMPLES,'target_sample_rate_hz':TARGET_SAMPLE_RATE_HZ,'filter':'3rd-order Butterworth bandpass 0.5–2.5 Hz; zero-phase sosfiltfilt when enough samples','hr':'10-second Hann-window FFT, search 0.8–2.5 Hz','spo2':'Experimental uncalibrated AC/DC ratio-of-ratios approximation','medical_validation':False}
