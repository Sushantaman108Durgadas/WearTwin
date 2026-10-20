#!/usr/bin/env python3
"""WearTwin two-channel PPG denoising autoencoder (research prototype).

Trains on stationary PPG windows by adding synthetic corruption, then denoises a
separate moving recording. This is NOT paired-supervised motion-artifact removal:
the learned result depends on whether synthetic corruption resembles real motion.

Accepted CSV columns include Timestamp_ms, IR, RED or Timestamp_s, IR_Raw, RED_Raw. Example:
  python train_denoising_autoencoder.py \
    --stationary hardware/rawdata_leftthumb_stationary.csv \
    --moving hardware/rawdata_leftthumb_motion_clean.csv

Requires: pip install numpy pandas scipy torch
"""
from __future__ import annotations
import argparse
import copy
import json
import random
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.signal import butter, sosfiltfilt, find_peaks
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset


class DenoisingAutoencoder(nn.Module):
    """Small fully convolutional 1-D autoencoder; input/output [batch, 2, time]."""
    def __init__(self):
        super().__init__()
        self.encoder = nn.Sequential(
            nn.Conv1d(2, 32, kernel_size=7, padding=3), nn.BatchNorm1d(32), nn.ReLU(),
            nn.Conv1d(32, 32, kernel_size=5, stride=2, padding=2), nn.BatchNorm1d(32), nn.ReLU(),
            nn.Conv1d(32, 64, kernel_size=5, stride=2, padding=2), nn.BatchNorm1d(64), nn.ReLU(),
            nn.Conv1d(64, 96, kernel_size=3, stride=2, padding=1), nn.BatchNorm1d(96), nn.ReLU(),
        )
        self.decoder = nn.Sequential(
            nn.ConvTranspose1d(96, 64, kernel_size=4, stride=2, padding=1), nn.BatchNorm1d(64), nn.ReLU(),
            nn.ConvTranspose1d(64, 32, kernel_size=4, stride=2, padding=1), nn.BatchNorm1d(32), nn.ReLU(),
            nn.ConvTranspose1d(32, 32, kernel_size=4, stride=2, padding=1), nn.BatchNorm1d(32), nn.ReLU(),
            nn.Conv1d(32, 2, kernel_size=7, padding=3),
        )
    def forward(self, x):
        y = self.decoder(self.encoder(x))
        # Cropping/padding guards against odd lengths if the window size is changed.
        if y.shape[-1] > x.shape[-1]:
            y = y[..., :x.shape[-1]]
        elif y.shape[-1] < x.shape[-1]:
            y = nn.functional.pad(y, (0, x.shape[-1] - y.shape[-1]))
        return y


def read_recording(path: str, fs: float) -> pd.DataFrame:
    """Load raw or processed CSVs, normalize timestamp units, and resample.

    Preferred signals are IR/RED when present; otherwise IR_Raw/RED_Raw.
    Timestamp_s is interpreted as seconds; Timestamp_ms as milliseconds.
    """
    df = pd.read_csv(path)
    if "Timestamp_s" in df.columns:
        timestamp_col, time_scale = "Timestamp_s", 1.0
    elif "Timestamp_ms" in df.columns:
        timestamp_col, time_scale = "Timestamp_ms", 0.001
    else:
        raise ValueError(f"{path}: expected Timestamp_s or Timestamp_ms; found columns: {list(df.columns)}")

    if {"IR", "RED"}.issubset(df.columns):
        ir_col, red_col = "IR", "RED"
    elif {"IR_Raw", "RED_Raw"}.issubset(df.columns):
        ir_col, red_col = "IR_Raw", "RED_Raw"
    elif {"IR_Filtered", "RED_Filtered"}.issubset(df.columns):
        ir_col, red_col = "IR_Filtered", "RED_Filtered"
        print(f"NOTE: {path}: using filtered channels because raw IR/RED are unavailable.")
    else:
        raise ValueError(
            f"{path}: expected IR/RED, IR_Raw/RED_Raw, or IR_Filtered/RED_Filtered; "
            f"found columns: {list(df.columns)}"
        )

    for c in (timestamp_col, ir_col, red_col):
        df[c] = pd.to_numeric(df[c], errors="coerce")
    df = df.replace([np.inf, -np.inf], np.nan).dropna(subset=[timestamp_col, ir_col, red_col])
    if len(df) < 20:
        raise ValueError(f"{path}: too few valid rows ({len(df)})")

    t_original = df[timestamp_col].to_numpy(float)
    resets = np.flatnonzero(np.diff(t_original) < 0)
    if len(resets):
        print(f"NOTE: {path}: found {len(resets)} timestamp reset(s); keeping the final segment.")
        df = df.iloc[resets[-1] + 1:].copy()

    # Average duplicate timestamps, then convert to seconds and resample.
    df = df.groupby(timestamp_col, as_index=False, sort=True)[[ir_col, red_col]].mean()
    t = df[timestamp_col].to_numpy(float) * time_scale
    t = t - t[0]
    vals = df[[ir_col, red_col]].to_numpy(float)
    keep = np.r_[True, np.diff(t) > 0]
    t, vals = t[keep], vals[keep]
    if len(t) < 20 or t[-1] <= 0:
        raise ValueError(f"{path}: insufficient unique timestamps after cleaning")
    grid = np.arange(0.0, t[-1], 1.0 / fs)
    if len(grid) < 20:
        raise ValueError(f"{path}: recording too short after resampling")
    return pd.DataFrame({
        "Timestamp_s": grid,
        "IR": np.interp(grid, t, vals[:, 0]),
        "RED": np.interp(grid, t, vals[:, 1]),
    })


def windowize(x: np.ndarray, window: int, stride: int) -> np.ndarray:
    if len(x) < window:
        raise ValueError(f"Need at least {window} resampled samples; got {len(x)}")
    starts = list(range(0, len(x) - window + 1, stride))
    if starts[-1] != len(x) - window:
        starts.append(len(x) - window)
    return np.stack([x[s:s+window] for s in starts]).astype(np.float32)


def normalize_windows(windows: np.ndarray, eps: float = 1e-6):
    # Normalize each channel/window independently. Keep mean/std for restoring
    # output scale and baseline after inference.
    means = windows.mean(axis=1, keepdims=True)
    stds = windows.std(axis=1, keepdims=True)
    stds = np.maximum(stds, eps)
    return (windows - means) / stds, means, stds


def corrupt_clean_windows(clean: np.ndarray, rng: np.random.Generator) -> np.ndarray:
    """Synthetic corruption augmentation; not a physical MAX30102 motion model."""
    noisy = clean.copy()
    n, length, channels = noisy.shape
    t = np.linspace(0, 1, length, dtype=np.float32)
    for i in range(n):
        for ch in range(channels):
            # Broadband noise
            noisy[i, :, ch] += rng.normal(0, rng.uniform(0.03, 0.18), length)
            # Baseline drift at low frequencies
            phase = rng.uniform(0, 2*np.pi)
            f1, f2 = rng.uniform(0.03, 0.25), rng.uniform(0.25, 0.8)
            noisy[i, :, ch] += rng.uniform(0.05, 0.45) * np.sin(2*np.pi*f1*t + phase)
            noisy[i, :, ch] += rng.uniform(0.01, 0.12) * np.sin(2*np.pi*f2*t + phase/2)
            # Random transient artifacts / contact disturbances
            if rng.random() < 0.75:
                for _ in range(int(rng.integers(1, 4))):
                    center = int(rng.integers(0, length))
                    width = int(rng.integers(1, max(2, length//20)))
                    lo, hi = max(0, center-width), min(length, center+width+1)
                    noisy[i, lo:hi, ch] += rng.uniform(-1.0, 1.0) * np.hanning(max(2, hi-lo))[:hi-lo]
            # Smooth gain variation
            gain = rng.uniform(0.75, 1.25)
            noisy[i, :, ch] *= gain
    return noisy


def overlap_add(pred: np.ndarray, starts: list[int], total: int, window: int) -> np.ndarray:
    out = np.zeros((total, 2), dtype=np.float64)
    weights = np.zeros(total, dtype=np.float64)
    w = np.hanning(window + 2)[1:-1]
    w = np.maximum(w, 0.05)
    for p, s in zip(pred, starts):
        out[s:s+window] += p * w[:, None]
        weights[s:s+window] += w
    return (out / np.maximum(weights[:, None], 1e-8)).astype(np.float32)


def estimate_hr(ir: np.ndarray, fs: float, window_seconds: float = 10.0):
    """Simple offline dominant-frequency HR estimate; not a clinical algorithm."""
    if len(ir) < int(window_seconds * fs):
        return np.full(len(ir), np.nan), np.zeros(len(ir), dtype=np.int8)
    sos = butter(2, (0.7, 2.5), btype="bandpass", fs=fs, output="sos")
    filtered = sosfiltfilt(sos, ir)
    win = max(int(window_seconds * fs), 16)
    out = np.full(len(ir), np.nan)
    valid = np.zeros(len(ir), dtype=np.int8)
    hann = np.hanning(win)
    for end in range(win, len(ir) + 1):
        segment = filtered[end-win:end]
        segment = (segment - segment.mean()) * hann
        spec = np.abs(np.fft.rfft(segment))
        freqs = np.fft.rfftfreq(win, d=1/fs)
        mask = (freqs >= 0.7) & (freqs <= 2.5)
        if not np.any(mask):
            continue
        peak = np.argmax(spec[mask])
        freq = freqs[mask][peak]
        out[end-1] = 60.0 * freq
        valid[end-1] = 1
    return out, valid


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stationary", default="hardware/rawdata_leftthumb_stationary.csv")
    ap.add_argument("--moving", default="hardware/rawdata_leftthumb_motion_clean.csv")
    ap.add_argument("--output-dir", default="hardware/output")
    ap.add_argument("--fs", type=float, default=10.0, help="resampled frequency in Hz; inspect timestamps first")
    ap.add_argument("--window", type=int, default=256)
    ap.add_argument("--stride", type=int, default=128)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--batch-size", type=int, default=32)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda"])
    args = ap.parse_args()
    if args.fs <= 5.0:
        raise ValueError("fs must exceed 5 Hz for the configured 2.5 Hz HR upper band")
    if args.window % 8:
        raise ValueError("window must be divisible by 8 for the encoder/decoder")
    random.seed(args.seed); np.random.seed(args.seed); torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if args.device == "cuda" or (args.device == "auto" and torch.cuda.is_available()) else "cpu")
    outdir = Path(args.output_dir); outdir.mkdir(parents=True, exist_ok=True)

    stationary = read_recording(args.stationary, args.fs)
    moving = read_recording(args.moving, args.fs)
    stat_raw = stationary[["IR", "RED"]].to_numpy(np.float32)
    mov_raw = moving[["IR", "RED"]].to_numpy(np.float32)
    stat_w_raw = windowize(stat_raw, args.window, args.stride)
    # Normalize each stationary window; train target is stationary-like waveform.
    stat_clean, _, _ = normalize_windows(stat_w_raw)
    # Split by chronological windows before corruption to keep a validation set.
    n = len(stat_clean)
    if n < 5:
        raise ValueError(f"Only {n} stationary windows; need at least 5")
    split = max(1, int(n * 0.8))
    if split >= n: split = n - 1
    train_clean = stat_clean[:split]
    val_clean = stat_clean[split:]
    train_noisy = corrupt_clean_windows(train_clean, rng)
    val_noisy = corrupt_clean_windows(val_clean, rng)
    train_ds = TensorDataset(torch.from_numpy(train_noisy.transpose(0,2,1)), torch.from_numpy(train_clean.transpose(0,2,1)))
    val_x = torch.from_numpy(val_noisy.transpose(0,2,1)).to(device)
    val_y = torch.from_numpy(val_clean.transpose(0,2,1)).to(device)
    loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True, drop_last=False)

    model = DenoisingAutoencoder().to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    best_val = float("inf"); best_state = copy.deepcopy(model.state_dict()); history = []
    for epoch in range(1, args.epochs + 1):
        model.train(); train_losses = []
        for xb, yb in loader:
            xb, yb = xb.to(device), yb.to(device)
            optimizer.zero_grad(set_to_none=True)
            pred = model(xb)
            # L1 is less sensitive than MSE to occasional large synthetic artifacts.
            loss = torch.mean(torch.abs(pred - yb))
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()
            train_losses.append(float(loss.detach().cpu()))
        model.eval()
        with torch.no_grad():
            val_pred = model(val_x)
            val_loss = float(torch.mean(torch.abs(val_pred - val_y)).cpu())
        row = {"epoch": epoch, "train_l1": float(np.mean(train_losses)), "validation_l1_synthetic_corruption": val_loss}
        history.append(row)
        print(f"Epoch {epoch:03d}/{args.epochs} | train L1={row['train_l1']:.5f} | val L1={val_loss:.5f}")
        if val_loss < best_val:
            best_val = val_loss; best_state = copy.deepcopy(model.state_dict())
    model.load_state_dict(best_state); model.eval()
    pd.DataFrame(history).to_csv(outdir / "autoencoder_training_history.csv", index=False)

    # Translate moving windows. Per-window normalization preserves local scale/DC
    # when restored, but this alone does not make SpO2 estimates validated.
    mov_windows_raw = windowize(mov_raw, args.window, args.stride)
    mov_norm, mov_means, mov_stds = normalize_windows(mov_windows_raw)
    starts = list(range(0, len(mov_raw) - args.window + 1, args.stride))
    if starts[-1] != len(mov_raw) - args.window: starts.append(len(mov_raw) - args.window)
    den_windows = []
    with torch.no_grad():
        for i in range(0, len(mov_norm), args.batch_size):
            xb = torch.from_numpy(mov_norm[i:i+args.batch_size].transpose(0,2,1)).to(device)
            p = model(xb).cpu().numpy().transpose(0,2,1)
            # Restore each window's original channel mean and scale.
            p = p * mov_stds[i:i+len(p)] + mov_means[i:i+len(p)]
            den_windows.append(p)
    den_windows = np.concatenate(den_windows, axis=0)
    denoised = overlap_add(den_windows, starts, len(mov_raw), args.window)
    hr, hr_valid = estimate_hr(denoised[:, 0], args.fs)

    out = pd.DataFrame({
        "Timestamp_s": moving["Timestamp_s"].to_numpy(),
        "IR_Moving_Resampled": mov_raw[:, 0],
        "RED_Moving_Resampled": mov_raw[:, 1],
        "IR_Denoised": denoised[:, 0],
        "RED_Denoised": denoised[:, 1],
        "HR_BPM_Recalculated": hr,
        "HR_Window_Valid": hr_valid,
    })
    out.to_csv(outdir / "denoised_moving.csv", index=False)
    checkpoint = {
        "model_state_dict": model.state_dict(), "fs": args.fs, "window": args.window,
        "stride": args.stride, "seed": args.seed, "best_validation_l1_synthetic_corruption": best_val,
        "normalization": "per-window mean/std; restored after inference",
        "training_note": "Trained on stationary windows with synthetic corruption; not paired real-motion ground truth.",
    }
    torch.save(checkpoint, outdir / "ppg_denoising_autoencoder.pt")
    report = {
        "stationary_resampled_samples": len(stationary), "moving_resampled_samples": len(moving),
        "stationary_windows": n, "train_windows": len(train_clean), "validation_windows": len(val_clean),
        "sample_rate_hz": args.fs, "window_samples": args.window,
        "window_seconds": args.window / args.fs, "stride_samples": args.stride,
        "best_validation_l1_synthetic_corruption": best_val,
        "device": str(device),
        "warning": "Synthetic-corruption validation measures only the synthetic task. It does not establish real motion-artifact removal or medical accuracy.",
    }
    (outdir / "autoencoder_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("\nFinished.")
    print(f"Denoised CSV: {outdir / 'denoised_moving.csv'}")
    print(f"Model:        {outdir / 'ppg_denoising_autoencoder.pt'}")
    print(f"History:      {outdir / 'autoencoder_training_history.csv'}")
    print(f"Report:       {outdir / 'autoencoder_report.json'}")
    print("WARNING: this is a research baseline; validate against independent reference data before interpreting HR clinically.")

if __name__ == "__main__":
    main()
