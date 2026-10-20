from fastapi.testclient import TestClient
from app import database
from app.main import app

def test_ingestion_and_ppg_warmup(tmp_path, monkeypatch):
    monkeypatch.setattr(database, 'DB_PATH', tmp_path / 'test.sqlite3')
    with TestClient(app) as client:
        assert client.get('/api/health').status_code == 200
        payload=[{'timestamp_ms':1000,'ir':10000,'red':8000,'heart_rate_bpm':None,'heart_rate_valid':False,'spo2_percent':None,'spo2_valid':False,
                  'accel_x':0.1,'accel_y':0.2,'accel_z':9.8,'gyro_x':0.0,'gyro_y':0.1,'gyro_z':0.2,'roll':0.5,'pitch':-0.5}]
        result=client.post('/api/readings/batch',json=payload)
        assert result.status_code == 200
        assert result.json()['accepted'] == 1
        assert client.get('/api/readings/latest').json()['has_data'] is True
        assert client.get('/api/ppg/processed').json()['status'] == 'warming_up'
        assert client.get('/api/imu/latest').json()['count'] == 1
