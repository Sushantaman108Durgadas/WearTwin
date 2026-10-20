#!/usr/bin/env python3
"""Train a two-channel PPG CycleGAN and translate a moving recording."""

from __future__ import annotations

import argparse
import csv
import math
import random
import sys
import warnings
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


@dataclass
class Recording:
    times: "object"
    signals: "object"
    invalid_rows: int
    reset_count: int


def load_recording(path: Path, sample_rate: float) -> Recording:
    import numpy as np

    if not path.is_file():
        raise FileNotFoundError(f"Input CSV does not exist: {path}")

    rows: list[tuple[float, float, float]] = []
    invalid_rows = 0
    with path.open("r", newline="", encoding="utf-8-sig") as csv_file:
        reader = csv.DictReader(csv_file)
        required = {"Timestamp_ms", "IR", "RED"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError(
                f"{path} must contain columns: {', '.join(sorted(required))}"
            )
        for row_number, row in enumerate(reader, start=2):
            try:
                timestamp_ms = float(row["Timestamp_ms"])
                infrared = float(row["IR"])
                red = float(row["RED"])
                if not all(math.isfinite(value) for value in (timestamp_ms, infrared, red)):
                    raise ValueError
                rows.append((timestamp_ms, infrared, red))
            except (TypeError, ValueError):
                invalid_rows += 1
                warnings.warn(
                    f"Skipping invalid timestamp/IR/RED values at {path}:{row_number}",
                    stacklevel=2,
                )

    if len(rows) < 2:
        raise ValueError(f"{path} has fewer than two usable samples")

    data = np.asarray(rows, dtype=np.float64)
    resets = np.flatnonzero(np.diff(data[:, 0]) < 0)
    reset_count = int(len(resets))
    if reset_count:
        data = data[int(resets[-1]) + 1 :]
        warnings.warn(
            f"{path} has {reset_count} timestamp reset(s); using the final segment",
            stacklevel=2,
        )
    if len(data) < 2:
        raise ValueError(f"{path} has fewer than two samples after its final timestamp reset")

    order = np.argsort(data[:, 0], kind="stable")
    data = data[order]
    unique_times, inverse = np.unique(data[:, 0], return_inverse=True)
    if len(unique_times) < 2:
        raise ValueError(f"{path} has fewer than two unique timestamps")
    if len(unique_times) != len(data):
        sums = np.zeros((len(unique_times), 2), dtype=np.float64)
        counts = np.bincount(inverse)
        np.add.at(sums, inverse, data[:, 1:3])
        data = np.column_stack((unique_times, sums / counts[:, None]))
        warnings.warn(
            f"{path} has duplicate timestamps; averaged IR/RED samples at each timestamp",
            stacklevel=2,
        )

    relative_seconds = (data[:, 0] - data[0, 0]) / 1000.0
    duration = float(relative_seconds[-1])
    if duration <= 0:
        raise ValueError(f"{path} does not span a positive duration")
    sample_count = int(math.floor(duration * sample_rate)) + 1
    uniform_times = np.arange(sample_count, dtype=np.float64) / sample_rate
    uniform_signals = np.column_stack(
        (
            np.interp(uniform_times, relative_seconds, data[:, 1]),
            np.interp(uniform_times, relative_seconds, data[:, 2]),
        )
    )
    return Recording(uniform_times, uniform_signals, invalid_rows, reset_count)


def make_windows(signal: "object", window: int, stride: int) -> "object":
    import numpy as np

    starts = range(0, len(signal) - window + 1, stride)
    windows = [signal[start : start + window] for start in starts]
    if not windows:
        raise ValueError(
            f"A training partition has {len(signal)} samples, shorter than "
            f"the requested {window}-sample window"
        )
    return np.asarray(windows, dtype=np.float32)


def make_models(window: int, torch: "object", nn: "object"):
    class ResidualBlock(nn.Module):
        def __init__(self, channels: int):
            super().__init__()
            self.layers = nn.Sequential(
                nn.Conv1d(channels, channels, kernel_size=3, padding=1),
                nn.InstanceNorm1d(channels),
                nn.ReLU(inplace=True),
                nn.Conv1d(channels, channels, kernel_size=3, padding=1),
                nn.InstanceNorm1d(channels),
            )

        def forward(self, value):
            return value + self.layers(value)

    class Generator(nn.Module):
        def __init__(self):
            super().__init__()
            self.layers = nn.Sequential(
                nn.Conv1d(2, 32, kernel_size=7, padding=3),
                nn.InstanceNorm1d(32),
                nn.ReLU(inplace=True),
                nn.Conv1d(32, 64, kernel_size=4, stride=2, padding=1),
                nn.InstanceNorm1d(64),
                nn.ReLU(inplace=True),
                nn.Conv1d(64, 128, kernel_size=4, stride=2, padding=1),
                nn.InstanceNorm1d(128),
                nn.ReLU(inplace=True),
                *[ResidualBlock(128) for _ in range(6)],
                nn.ConvTranspose1d(128, 64, kernel_size=4, stride=2, padding=1),
                nn.InstanceNorm1d(64),
                nn.ReLU(inplace=True),
                nn.ConvTranspose1d(64, 32, kernel_size=4, stride=2, padding=1),
                nn.InstanceNorm1d(32),
                nn.ReLU(inplace=True),
                nn.Conv1d(32, 2, kernel_size=7, padding=3),
                nn.Tanh(),
            )

        def forward(self, value):
            return self.layers(value)

    class Discriminator(nn.Module):
        def __init__(self):
            super().__init__()
            self.layers = nn.Sequential(
                nn.Conv1d(2, 32, kernel_size=4, stride=2, padding=1),
                nn.LeakyReLU(0.2, inplace=True),
                nn.Conv1d(32, 64, kernel_size=4, stride=2, padding=1),
                nn.InstanceNorm1d(64),
                nn.LeakyReLU(0.2, inplace=True),
                nn.Conv1d(64, 128, kernel_size=4, stride=2, padding=1),
                nn.InstanceNorm1d(128),
                nn.LeakyReLU(0.2, inplace=True),
                nn.Conv1d(128, 1, kernel_size=3, padding=1),
            )

        def forward(self, value):
            return self.layers(value)

    if window % 8:
        raise ValueError("window must be divisible by 8 for the convolution layers")
    return Generator(), Generator(), Discriminator(), Discriminator()


def training_statistics(signal: "object", split_fraction: float):
    import numpy as np

    boundary = int(len(signal) * split_fraction)
    if boundary < 2:
        raise ValueError("Training split is too short to calculate normalization statistics")
    mean = signal[:boundary].mean(axis=0)
    scale = signal[:boundary].std(axis=0)
    scale = np.where(scale < 1e-8, 1.0, scale)
    return mean, scale, boundary


def train(args: argparse.Namespace, stationary: Recording, moving: Recording):
    import numpy as np
    import torch
    from torch import nn

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    random.seed(args.seed)
    if torch.cuda.is_available() and args.device != "cpu":
        device = torch.device(args.device or "cuda")
    else:
        if args.device not in (None, "cpu"):
            raise RuntimeError(f"Requested device {args.device!r} is unavailable")
        device = torch.device("cpu")

    stationary_mean, stationary_scale, stationary_boundary = training_statistics(
        stationary.signals, args.train_fraction
    )
    moving_mean, moving_scale, moving_boundary = training_statistics(
        moving.signals, args.train_fraction
    )
    stationary_windows = make_windows(
        (stationary.signals[:stationary_boundary] - stationary_mean) / stationary_scale,
        args.window,
        args.stride,
    )
    moving_windows = make_windows(
        (moving.signals[:moving_boundary] - moving_mean) / moving_scale,
        args.window,
        args.stride,
    )

    generator_moving_to_stationary, generator_stationary_to_moving, discriminator_stationary, discriminator_moving = make_models(
        args.window, torch, nn
    )
    models = (
        generator_moving_to_stationary,
        generator_stationary_to_moving,
        discriminator_stationary,
        discriminator_moving,
    )
    for model in models:
        model.to(device)

    optimizer_generators = torch.optim.Adam(
        list(generator_moving_to_stationary.parameters())
        + list(generator_stationary_to_moving.parameters()),
        lr=args.learning_rate,
        betas=(0.5, 0.999),
    )
    optimizer_discriminators = torch.optim.Adam(
        list(discriminator_stationary.parameters())
        + list(discriminator_moving.parameters()),
        lr=args.learning_rate,
        betas=(0.5, 0.999),
    )
    mse = nn.MSELoss()
    l1 = nn.L1Loss()
    history: list[dict[str, float]] = []
    steps = max(
        1,
        math.ceil(max(len(stationary_windows), len(moving_windows)) / args.batch_size),
    )

    for epoch in range(1, args.epochs + 1):
        totals = {"generator": 0.0, "discriminator": 0.0, "cycle": 0.0, "identity": 0.0}
        for _ in range(steps):
            stationary_indices = np.random.randint(
                0, len(stationary_windows), size=args.batch_size
            )
            moving_indices = np.random.randint(0, len(moving_windows), size=args.batch_size)
            stationary_batch = (
                torch.from_numpy(stationary_windows[stationary_indices])
                .transpose(1, 2)
                .to(device)
            )
            moving_batch = (
                torch.from_numpy(moving_windows[moving_indices])
                .transpose(1, 2)
                .to(device)
            )

            optimizer_generators.zero_grad(set_to_none=True)
            translated_stationary = generator_moving_to_stationary(moving_batch)
            cycled_moving = generator_stationary_to_moving(translated_stationary)
            translated_moving = generator_stationary_to_moving(stationary_batch)
            cycled_stationary = generator_moving_to_stationary(translated_moving)
            identity_stationary = generator_moving_to_stationary(stationary_batch)
            identity_moving = generator_stationary_to_moving(moving_batch)
            stationary_fake_for_generator = discriminator_stationary(translated_stationary)
            moving_fake_for_generator = discriminator_moving(translated_moving)
            adversarial = mse(
                stationary_fake_for_generator,
                torch.ones_like(stationary_fake_for_generator),
            ) + mse(
                moving_fake_for_generator,
                torch.ones_like(moving_fake_for_generator),
            )
            cycle = l1(cycled_moving, moving_batch) + l1(cycled_stationary, stationary_batch)
            identity = l1(identity_stationary, stationary_batch) + l1(
                identity_moving, moving_batch
            )
            generator_loss = (
                adversarial + args.cycle_weight * cycle + args.identity_weight * identity
            )
            generator_loss.backward()
            optimizer_generators.step()

            optimizer_discriminators.zero_grad(set_to_none=True)
            stationary_real = discriminator_stationary(stationary_batch)
            stationary_fake = discriminator_stationary(translated_stationary.detach())
            moving_real = discriminator_moving(moving_batch)
            moving_fake = discriminator_moving(translated_moving.detach())
            discriminator_loss = 0.5 * (
                mse(stationary_real, torch.ones_like(stationary_real))
                + mse(stationary_fake, torch.zeros_like(stationary_fake))
                + mse(moving_real, torch.ones_like(moving_real))
                + mse(moving_fake, torch.zeros_like(moving_fake))
            )
            discriminator_loss.backward()
            optimizer_discriminators.step()

            totals["generator"] += float(generator_loss.detach().cpu())
            totals["discriminator"] += float(discriminator_loss.detach().cpu())
            totals["cycle"] += float(cycle.detach().cpu())
            totals["identity"] += float(identity.detach().cpu())

        average = {key: value / steps for key, value in totals.items()}
        history.append({"epoch": epoch, **average})
        if epoch == 1 or epoch % args.log_interval == 0 or epoch == args.epochs:
            print(
                f"Epoch {epoch:03d}/{args.epochs}: "
                f"G={average['generator']:.4f} D={average['discriminator']:.4f} "
                f"cycle={average['cycle']:.4f} identity={average['identity']:.4f}"
            )

    checkpoint = {
        "generator_moving_to_stationary": generator_moving_to_stationary.state_dict(),
        "generator_stationary_to_moving": generator_stationary_to_moving.state_dict(),
        "discriminator_stationary": discriminator_stationary.state_dict(),
        "discriminator_moving": discriminator_moving.state_dict(),
        "stationary_mean": stationary_mean.tolist(),
        "stationary_scale": stationary_scale.tolist(),
        "moving_mean": moving_mean.tolist(),
        "moving_scale": moving_scale.tolist(),
        "sample_rate": args.fs,
        "window": args.window,
        "stride": args.stride,
        "seed": args.seed,
    }
    args.model.parent.mkdir(parents=True, exist_ok=True)
    torch.save(checkpoint, args.model)
    history_path = args.model.with_name(f"{args.model.stem}_training_history.csv")
    with history_path.open("w", newline="", encoding="utf-8") as history_file:
        writer = csv.DictWriter(history_file, fieldnames=list(history[0]))
        writer.writeheader()
        writer.writerows(history)
    print(f"Saved checkpoint: {args.model}")
    print(f"Saved loss history: {history_path}")
    return (
        generator_moving_to_stationary,
        moving_mean,
        moving_scale,
        stationary_mean,
        stationary_scale,
        device,
    )


def load_generator(model_path: Path, device: str | None, window: int):
    import torch
    from torch import nn

    if not model_path.is_file():
        raise FileNotFoundError(f"Model checkpoint does not exist: {model_path}")
    target_device = torch.device(
        device or ("cuda" if torch.cuda.is_available() else "cpu")
    )
    checkpoint = torch.load(model_path, map_location=target_device, weights_only=True)
    if checkpoint.get("window") != window:
        raise ValueError(
            f"Checkpoint window is {checkpoint.get('window')}; requested window is {window}"
        )
    generator, _, _, _ = make_models(window, torch, nn)
    generator.load_state_dict(checkpoint["generator_moving_to_stationary"])
    generator.to(target_device).eval()
    return (
        generator,
        checkpoint,
        target_device,
    )


def translate(
    signal: "object",
    generator: "object",
    input_mean: "object",
    input_scale: "object",
    output_mean: "object",
    output_scale: "object",
    window: int,
    stride: int,
    device: "object",
):
    import numpy as np
    import torch

    normalized = (signal - input_mean) / input_scale
    if len(normalized) < window:
        padding = window - len(normalized)
        padded = np.pad(normalized, ((0, padding), (0, 0)), mode="edge")
        starts = [0]
    else:
        starts = list(range(0, len(normalized) - window + 1, stride))
        final_start = len(normalized) - window
        if starts[-1] != final_start:
            starts.append(final_start)
        padded = normalized

    output = np.zeros_like(padded, dtype=np.float64)
    weights = np.zeros(len(padded), dtype=np.float64)
    blend = np.hanning(window + 2)[1:-1]
    blend = np.maximum(blend, 0.05)
    generator.eval()
    with torch.no_grad():
        for start in starts:
            segment = padded[start : start + window]
            tensor = torch.from_numpy(segment.astype(np.float32)).transpose(0, 1).unsqueeze(0)
            generated = generator(tensor.to(device)).squeeze(0).transpose(0, 1).cpu().numpy()
            output[start : start + window] += generated * blend[:, None]
            weights[start : start + window] += blend
    translated = output / np.maximum(weights[:, None], 1e-8)
    return translated[: len(signal)] * output_scale + output_mean


def estimate_heart_rate(
    signal: "object",
    sample_rate: float,
    window_seconds: float = 10.0,
    filter_low: float = 0.5,
    filter_high: float = 2.5,
    hr_low: float = 0.8,
    hr_high: float = 2.5,
):
    import numpy as np
    from scipy.signal import butter, sosfiltfilt

    if not 0 < filter_low < filter_high < sample_rate / 2:
        raise ValueError(
            "HR filter cutoffs must satisfy 0 < low < high < sampling_rate / 2"
        )
    if not 0 <= hr_low < hr_high <= sample_rate / 2:
        raise ValueError(
            "HR frequency bounds must satisfy 0 <= low < high <= sampling_rate / 2"
        )
    window_samples = int(round(window_seconds * sample_rate))
    if window_samples < 4:
        raise ValueError("HR analysis window must contain at least 4 samples")
    if len(signal) < 20:
        raise ValueError("Signal is too short for HR bandpass filtering")

    filter_sos = butter(
        2,
        (filter_low, filter_high),
        btype="bandpass",
        fs=sample_rate,
        output="sos",
    )
    filtered = sosfiltfilt(filter_sos, signal)
    estimates = np.full(len(signal), np.nan, dtype=np.float64)
    frequencies = np.fft.rfftfreq(window_samples, d=1.0 / sample_rate)
    valid_frequencies = (frequencies >= hr_low) & (frequencies <= hr_high)
    if not np.any(valid_frequencies):
        return estimates

    window = np.hanning(window_samples)
    for end in range(window_samples - 1, len(signal)):
        start = end - window_samples + 1
        segment = filtered[start : end + 1]
        spectrum = np.abs(np.fft.rfft((segment - np.mean(segment)) * window))
        peak_index = np.flatnonzero(valid_frequencies)[
            np.argmax(spectrum[valid_frequencies])
        ]
        estimates[end] = frequencies[peak_index] * 60.0
    return estimates


def write_output(
    path: Path,
    times: "object",
    moving_signals: "object",
    translated_signals: "object",
    sample_rate: float,
    hr_window_seconds: float,
):
    import numpy as np

    hr_per_sample = estimate_heart_rate(
        translated_signals[:, 0], sample_rate, hr_window_seconds
    )
    quality_per_sample = np.isfinite(hr_per_sample).astype(np.int8)

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as output_file:
        writer = csv.writer(output_file)
        writer.writerow(
            [
                "Timestamp_s",
                "IR_Moving_Resampled",
                "RED_Moving_Resampled",
                "IR_StationaryLike",
                "RED_StationaryLike",
                "HR_BPM_Recalculated",
                "HR_Window_Quality_Flag",
            ]
        )
        for values in zip(
            times,
            moving_signals[:, 0],
            moving_signals[:, 1],
            translated_signals[:, 0],
            translated_signals[:, 1],
            hr_per_sample,
            quality_per_sample,
        ):
            writer.writerow(
                [
                    f"{values[0]:.6f}",
                    f"{values[1]:.6f}",
                    f"{values[2]:.6f}",
                    f"{values[3]:.6f}",
                    f"{values[4]:.6f}",
                    "" if math.isnan(values[5]) else f"{values[5]:.2f}",
                    int(values[6]),
                ]
            )
    print(f"Saved translated recording: {path}")


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Train a two-channel CycleGAN on stationary/moving PPG CSVs."
    )
    parser.add_argument("--stationary", type=Path, help="Stationary-domain CSV (required for training)")
    parser.add_argument("--moving", type=Path, required=True, help="Moving-domain CSV to translate")
    parser.add_argument("--model", type=Path, required=True, help="Checkpoint path")
    parser.add_argument("--output", type=Path, required=True, help="Translated CSV output path")
    parser.add_argument("--fs", type=float, default=10.0, help="Resampling frequency in Hz (default: 10)")
    parser.add_argument("--window", type=int, default=256, help="Samples per CycleGAN window")
    parser.add_argument("--stride", type=int, default=128, help="Window stride in samples")
    parser.add_argument(
        "--hr-window-seconds",
        type=float,
        default=10.0,
        help="Rolling HR analysis window in seconds (default: 10)",
    )
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--learning-rate", type=float, default=2e-4)
    parser.add_argument("--cycle-weight", type=float, default=10.0)
    parser.add_argument("--identity-weight", type=float, default=5.0)
    parser.add_argument("--train-fraction", type=float, default=0.7)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--device", help="Torch device, e.g. cpu or cuda; defaults automatically")
    parser.add_argument("--log-interval", type=int, default=10)
    parser.add_argument(
        "--inference-only",
        action="store_true",
        help="Translate with an existing checkpoint instead of training",
    )
    args = parser.parse_args(argv)
    if args.fs <= 0 or not math.isfinite(args.fs):
        parser.error("--fs must be a finite positive number")
    if args.window < 16 or args.window % 8:
        parser.error("--window must be at least 16 and divisible by 8")
    if args.stride < 1 or args.stride > args.window:
        parser.error("--stride must be between 1 and --window")
    if args.hr_window_seconds <= 0 or not math.isfinite(args.hr_window_seconds):
        parser.error("--hr-window-seconds must be a finite positive number")
    if args.epochs < 1 or args.batch_size < 1 or args.log_interval < 1:
        parser.error("--epochs, --batch-size, and --log-interval must be positive")
    if not 0 < args.train_fraction < 1:
        parser.error("--train-fraction must be strictly between 0 and 1")
    if args.cycle_weight < 0 or args.identity_weight < 0 or args.learning_rate <= 0:
        parser.error("loss weights must be non-negative and learning rate must be positive")
    if not args.inference_only and args.stationary is None:
        parser.error("--stationary is required unless --inference-only is set")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        import numpy  # noqa: F401
        import torch
        import scipy  # noqa: F401
    except ImportError as error:
        print(
            f"Missing Python dependency {error.name!r}. Install them with: "
            "python -m pip install numpy scipy torch",
            file=sys.stderr,
        )
        return 1

    moving = load_recording(args.moving, args.fs)
    print(f"Moving recording: {len(moving.times)} resampled samples")
    if moving.invalid_rows:
        print(f"Skipped invalid input rows: {moving.invalid_rows}")

    if args.inference_only:
        generator, checkpoint, device = load_generator(args.model, args.device, args.window)
        if not math.isclose(float(checkpoint["sample_rate"]), args.fs):
            raise ValueError(
                f"Checkpoint sample rate is {checkpoint['sample_rate']} Hz; requested {args.fs} Hz"
            )
        moving_mean = numpy.asarray(checkpoint["moving_mean"], dtype=numpy.float64)
        moving_scale = numpy.asarray(checkpoint["moving_scale"], dtype=numpy.float64)
        stationary_mean = numpy.asarray(checkpoint["stationary_mean"], dtype=numpy.float64)
        stationary_scale = numpy.asarray(checkpoint["stationary_scale"], dtype=numpy.float64)
    else:
        stationary = load_recording(args.stationary, args.fs)
        print(f"Stationary recording: {len(stationary.times)} resampled samples")
        if stationary.invalid_rows:
            print(f"Skipped invalid input rows: {stationary.invalid_rows}")
        (
            generator,
            moving_mean,
            moving_scale,
            stationary_mean,
            stationary_scale,
            device,
        ) = train(args, stationary, moving)
        times = moving.times
        checkpoint = None

    if checkpoint is not None:
        times = moving.times
    translated = translate(
        moving.signals,
        generator,
        moving_mean,
        moving_scale,
        stationary_mean,
        stationary_scale,
        args.window,
        args.stride,
        device,
    )
    write_output(
        args.output,
        times,
        moving.signals,
        translated,
        args.fs,
        args.hr_window_seconds,
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
