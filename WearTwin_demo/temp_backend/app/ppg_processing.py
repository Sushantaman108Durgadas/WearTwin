"""Streaming-window equivalent of the project's CSV cleaning/filtering workflow.

Uses a recent 2000-record window, handles resets as separate epochs, averages duplicate
timestamps, avoids interpolating across large gaps, and filters IR/RED independently.
Estimates are experimental and not medically validated.
"""
import numpy as np
from scipy.signal import butter, sosfiltfilt, windows
from .config import PPG_WINDOW_SAMPLES, TARGET_SAMPLE_RATE_HZ


def _num(row, key):
    value = row.get(key)
    try:
        value = float(value)
        return value if np.isfinite(value) else np.nan
    except (TypeError, ValueError):
        return np.nan


def process_ppg(rows):
    raw_count = len(rows)
    if not rows:
        return {"status":"insufficient_data", "message":"Waiting for PPG samples", "sample_count":0,
                "window_capacity":PPG_WINDOW_SAMPLES,"sample_rate_hz":None,"duration_s":0,
                "cleaning":{"invalid_rows":0,"duplicate_timestamps":0,"gap_count":0,"timestamp_resets":0},
                "points":[],"device_hr_bpm":None,"processed_hr_bpm":None,"experimental_spo2_percent":None}
    # Input rows are normally already epoch-filtered and ordered. Detect resets defensively.
    valid=[]; invalid=0; resets=0; last_ts=None; epoch=0
    for row in rows[-PPG_WINDOW_SAMPLES:]:
        ts=_num(row,"timestamp_ms"); ir=_num(row,"ir"); red=_num(row,"red")
        if not np.isfinite(ts) or not np.isfinite(ir) or not np.isfinite(red):
            invalid += 1; continue
        ts=int(ts)
        if last_ts is not None and ts < last_ts:
            resets += 1; epoch += 1; valid=[]
        valid.append({"timestamp_ms":ts,"ir":ir,"red":red,"heart_rate_bpm":row.get("heart_rate_bpm"),
                      "heart_rate_valid":row.get("heart_rate_valid",False),"spo2_percent":row.get("spo2_percent"),
                      "spo2_valid":row.get("spo2_valid",False)})
        last_ts=ts
    if not valid:
        return {"status":"insufficient_data","message":"No valid IR/RED samples in current window","sample_count":0,
                "window_capacity":PPG_WINDOW_SAMPLES,"sample_rate_hz":None,"duration_s":0,
                "cleaning":{"invalid_rows":invalid,"duplicate_timestamps":0,"gap_count":0,"timestamp_resets":resets},
                "points":[],"device_hr_bpm":None,"processed_hr_bpm":None,"experimental_spo2_percent":None}
    # Stable sort then average duplicate timestamps (same policy as offline cleanup intent).
    valid.sort(key=lambda r:r["timestamp_ms"])
    grouped={}
    for r in valid: grouped.setdefault(r["timestamp_ms"], []).append(r)
    duplicate_count=sum(len(v)-1 for v in grouped.values())
    clean=[]
    for ts, group in grouped.items():
        clean.append({"timestamp_ms":ts,"ir":float(np.mean([g["ir"] for g in group])),
                      "red":float(np.mean([g["red"] for g in group])),
                      "heart_rate_bpm":next((g["heart_rate_bpm"] for g in reversed(group) if g["heart_rate_valid"]),None),
                      "heart_rate_valid":any(g["heart_rate_valid"] for g in group),
                      "spo2_percent":next((g["spo2_percent"] for g in reversed(group) if g["spo2_valid"]),None),
                      "spo2_valid":any(g["spo2_valid"] for g in group)})
    ts=np.array([r["timestamp_ms"] for r in clean],dtype=float)
    ir=np.array([r["ir"] for r in clean],dtype=float); red=np.array([r["red"] for r in clean],dtype=float)
    dt=np.diff(ts)/1000.0; positive=dt[dt>0]
    median_dt=float(np.median(positive)) if len(positive) else None
    fs=float(1/median_dt) if median_dt and median_dt>0 else None
    duration=float((ts[-1]-ts[0])/1000) if len(ts)>1 else 0.0
    gap_count=int(np.sum(dt > max(0.25, (median_dt or .1)*1.5))) if len(dt) else 0
    # Do not bridge large gaps. Split into contiguous segments, process the latest segment only.
    split_threshold=max(0.25,(median_dt or .1)*1.5)
    split_idx=np.flatnonzero(dt>split_threshold)
    start=int(split_idx[-1]+1) if len(split_idx) else 0
    ts,ir,red=ts[start:],ir[start:],red[start:]
    if len(ts)>=2:
        # Use nominal 10 Hz grid, but cap the target grid to the actual segment duration.
        target_fs=TARGET_SAMPLE_RATE_HZ
        t=(ts-ts[0])/1000.0
        if t[-1] >= 1.0 and np.all(np.diff(t)>0):
            grid=np.arange(0, t[-1], 1.0/target_fs)
            if len(grid)>=2:
                ir=np.interp(grid,t,ir); red=np.interp(grid,t,red); ts=ts[0]+grid*1000; fs=target_fs
    points=[]; filtered_ir=None; filtered_red=None; hr=None; spo2=None; status="warming_up"; message="Collecting enough samples for stable filtering"
    # Filtering requires a sufficient length. Use a lower order Butterworth matching the offline intent.
    if len(ir)>=40 and fs and 2.5 < fs/2:
        try:
            sos=butter(3,[0.5,2.5],btype="bandpass",fs=fs,output="sos")
            filtered_ir=sosfiltfilt(sos,ir); filtered_red=sosfiltfilt(sos,red)
            status="ready"; message="Window cleaned and filtered; estimates remain experimental"
        except ValueError:
            filtered_ir=filtered_red=None
    # FFT estimate over latest 10 s, same broad method as the offline pipeline.
    if filtered_ir is not None and len(filtered_ir)>=max(40,int(fs*10)):
        n=int(fs*10); signal=filtered_ir[-n:]; signal=signal-np.mean(signal)
        spec=np.abs(np.fft.rfft(signal*windows.hann(len(signal))))
        freq=np.fft.rfftfreq(len(signal),d=1/fs)
        mask=(freq>=0.8)&(freq<=2.5)
        if mask.any() and np.max(spec[mask])>0:
            hr=float(freq[mask][np.argmax(spec[mask])]*60.0)
    # Experimental ratio-of-ratios: preserve raw/DC baseline, not only bandpassed signals.
    if len(ir)>=max(40,int((fs or 10)*10)):
        n=min(len(ir),int((fs or 10)*10)); irw=ir[-n:]; redw=red[-n:]
        dc_i=float(np.mean(irw)); dc_r=float(np.mean(redw))
        ac_i=float(np.std(irw)); ac_r=float(np.std(redw))
        if dc_i>0 and dc_r>0 and ac_i>0 and ac_r>0:
            q=(ac_r/dc_r)/(ac_i/dc_i)
            spo2=float(np.clip(110-25*q,70,100))
    # Plot downsampled points for responsive browser rendering.
    stride=max(1,int(np.ceil(len(ir)/1000)))
    for i in range(0,len(ir),stride):
        points.append({"t_s":round(float((ts[i]-ts[0])/1000.0),3),"timestamp_ms":int(ts[i]),
                       "ir_raw":round(float(ir[i]),3),"red_raw":round(float(red[i]),3),
                       "ir_filtered":round(float(filtered_ir[i]),5) if filtered_ir is not None else None,
                       "red_filtered":round(float(filtered_red[i]),5) if filtered_red is not None else None})
    last=clean[-1]
    return {"status":status,"message":message,"sample_count":len(clean),"processed_count":len(ir),
            "window_capacity":PPG_WINDOW_SAMPLES,"sample_rate_hz":round(float(fs),3) if fs else None,
            "duration_s":round(duration,3),"processing_window_duration_s":round(float((ts[-1]-ts[0])/1000),3) if len(ts)>1 else 0,
            "cleaning":{"input_rows":raw_count,"invalid_rows":invalid,"duplicate_timestamps":duplicate_count,
                        "gap_count":gap_count,"timestamp_resets":resets},"points":points,
            "device_hr_bpm":float(last["heart_rate_bpm"]) if last["heart_rate_valid"] and last["heart_rate_bpm"] is not None else None,
            "processed_hr_bpm":round(hr,1) if hr is not None else None,
            "device_spo2_percent":float(last["spo2_percent"]) if last["spo2_valid"] and last["spo2_percent"] is not None else None,
            "experimental_spo2_percent":round(spo2,1) if spo2 is not None else None,
            "frequency_resolution_bpm":round(60/10,1) if len(ir)>=int((fs or 10)*10) else None}
