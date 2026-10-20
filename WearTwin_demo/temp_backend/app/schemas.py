from typing import Optional, Union
from pydantic import BaseModel, Field, ConfigDict

class ReadingIn(BaseModel):
    model_config = ConfigDict(extra="ignore")
    timestamp_ms: int = Field(ge=0)
    accel_x: Optional[float] = None; accel_y: Optional[float] = None; accel_z: Optional[float] = None
    gyro_x: Optional[float] = None; gyro_y: Optional[float] = None; gyro_z: Optional[float] = None
    roll: Optional[float] = None; pitch: Optional[float] = None
    ir: Optional[float] = None; red: Optional[float] = None
    heart_rate_bpm: Optional[float] = None; heart_rate_valid: bool = False
    spo2_percent: Optional[float] = None; spo2_valid: bool = False

class BatchIn(BaseModel):
    readings: list[ReadingIn]

class SessionStart(BaseModel):
    notes: str = "WearTwin demo session"
