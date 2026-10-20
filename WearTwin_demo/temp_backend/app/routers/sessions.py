from fastapi import APIRouter, HTTPException
from fastapi.responses import StreamingResponse
import csv, io
from ..database import connection, utc_now
from ..schemas import SessionStart

router=APIRouter(prefix='/api/sessions',tags=['sessions'])

@router.post('/start')
def start_session(payload:SessionStart=SessionStart()):
    with connection() as c:
        row=c.execute("SELECT id,started_at FROM sessions WHERE status='active' LIMIT 1").fetchone()
        if row: return {"id":row['id'],"started_at":row['started_at'],"status":"active","already_active":True}
        now=utc_now(); cur=c.execute("INSERT INTO sessions(started_at,status,notes) VALUES(?, 'active', ?)",(now,payload.notes))
        return {"id":cur.lastrowid,"started_at":now,"status":"active","already_active":False}

@router.post('/stop')
def stop_session():
    with connection() as c:
        row=c.execute("SELECT id FROM sessions WHERE status='active' ORDER BY id DESC LIMIT 1").fetchone()
        if not row: raise HTTPException(409,'No active monitoring session')
        now=utc_now(); c.execute("UPDATE sessions SET status='stopped',ended_at=? WHERE id=?",(now,row['id']))
        return {"id":row['id'],"ended_at":now,"status":"stopped"}

@router.get('')
def list_sessions():
    with connection() as c:
        rows=c.execute("SELECT s.*,COUNT(r.id) AS sample_count FROM sessions s LEFT JOIN readings r ON r.session_id=s.id GROUP BY s.id ORDER BY s.id DESC").fetchall()
        active=c.execute("SELECT id FROM sessions WHERE status='active' LIMIT 1").fetchone()
    return {"sessions":[dict(r) for r in rows],"active_session_id":active['id'] if active else None}

@router.get('/{session_id}')
def session_detail(session_id:int):
    with connection() as c:
        row=c.execute("SELECT s.*,COUNT(r.id) AS sample_count FROM sessions s LEFT JOIN readings r ON r.session_id=s.id WHERE s.id=? GROUP BY s.id",(session_id,)).fetchone()
        if not row: raise HTTPException(404,'Session not found')
        return dict(row)

@router.get('/{session_id}/export.csv')
def export_session(session_id:int):
    with connection() as c:
        if not c.execute('SELECT id FROM sessions WHERE id=?',(session_id,)).fetchone(): raise HTTPException(404,'Session not found')
        rows=c.execute('SELECT timestamp_ms,epoch_id,received_at,ir,red,heart_rate_bpm,heart_rate_valid,spo2_percent,spo2_valid,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,roll,pitch FROM readings WHERE session_id=? ORDER BY id',(session_id,)).fetchall()
    out=io.StringIO(); writer=csv.writer(out); writer.writerow(rows[0].keys() if rows else ['timestamp_ms','epoch_id','received_at','ir','red','heart_rate_bpm','heart_rate_valid','spo2_percent','spo2_valid','accel_x','accel_y','accel_z','gyro_x','gyro_y','gyro_z','roll','pitch'])
    writer.writerows([tuple(r) for r in rows]); out.seek(0)
    return StreamingResponse(iter([out.getvalue()]),media_type='text/csv',headers={'Content-Disposition':f'attachment; filename=weartwin_session_{session_id}.csv'})
