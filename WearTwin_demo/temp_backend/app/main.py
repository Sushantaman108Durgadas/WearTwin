from contextlib import asynccontextmanager
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
import asyncio
from .database import init_db, connection
from .config import CORS_ORIGINS
from .routers import readings, sessions, processing

class LiveHub:
    def __init__(self): self.clients=set()
    async def connect(self,ws): await ws.accept(); self.clients.add(ws)
    def disconnect(self,ws): self.clients.discard(ws)
    async def broadcast(self,msg):
        dead=[]
        for ws in list(self.clients):
            try: await ws.send_json(msg)
            except Exception: dead.append(ws)
        for ws in dead: self.disconnect(ws)
hub=LiveHub()

@asynccontextmanager
async def lifespan(app:FastAPI):
    init_db()
    task=asyncio.create_task(broadcast_loop())
    yield
    task.cancel()

async def broadcast_loop():
    while True:
        await asyncio.sleep(1.0)
        if not hub.clients: continue
        with connection() as c:
            r=c.execute('SELECT * FROM readings ORDER BY id DESC LIMIT 1').fetchone()
            s=c.execute("SELECT id,started_at FROM sessions WHERE status='active' LIMIT 1").fetchone()
        await hub.broadcast({'type':'live','reading':dict(r) if r else None,'active_session':dict(s) if s else None})

app=FastAPI(title='WearTwin API',version='1.0.0',description='Local wearable sensor demo API',lifespan=lifespan)
app.add_middleware(CORSMiddleware,allow_origins=[x.strip() for x in CORS_ORIGINS],allow_credentials=True,allow_methods=['*'],allow_headers=['*'])
app.include_router(readings.router); app.include_router(sessions.router); app.include_router(processing.router)
@app.get('/api/health')
def health():
    with connection() as c:
        n=c.execute('SELECT COUNT(*) n FROM readings').fetchone()['n']
        last=c.execute('SELECT received_at FROM readings ORDER BY id DESC LIMIT 1').fetchone()
        active=c.execute("SELECT id FROM sessions WHERE status='active' LIMIT 1").fetchone()
    return {'ok':True,'service':'WearTwin API','stored_readings':n,'last_received_at':last['received_at'] if last else None,'active_session_id':active['id'] if active else None}
@app.websocket('/ws/live')
async def ws_live(ws:WebSocket):
    await hub.connect(ws)
    try:
        while True: await ws.receive_text()
    except WebSocketDisconnect: hub.disconnect(ws)
    except Exception: hub.disconnect(ws)
