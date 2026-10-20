# Experimental PPG CycleGAN

`train_cyclegan.py` trains two generators and two discriminators to translate
two-channel IR/RED PPG windows between the stationary and moving recordings.
It resamples the CSVs independently, trains on the first 70% of each recording,
and uses that training portion alone to calculate each domain's normalization
statistics. The remaining samples are not used to fit the model.

This is an experimental signal-processing prototype, not a validated denoiser
or medical device. With only one recording in each domain, CycleGAN can learn
recording-specific differences or alter real pulse morphology. Verify translated
waveforms and HR against an independent reference before interpreting the
output physiologically.

## Install

From the project root, install the dependencies in the active Python environment:

```powershell
python -m pip install numpy scipy torch
```

## Train and translate

```powershell
python "denoising/train_cyclegan.py" `
  --stationary "hardware/rawdata_leftthumb_stationary.csv" `
  --moving "hardware/rawdata_leftthumb_moving.csv" `
  --model "hardware/output/wear_twin_cyclegan.pt" `
  --output "hardware/output/moving_stationary_like.csv" `
  --fs 10 `
  --window 256 `
  --stride 128 `
  --epochs 100 `
  --batch-size 16
```

The run writes the model checkpoint, an epoch-level training-loss CSV next to
the checkpoint, and the translated recording. Timestamp resets use the final
segment; duplicate timestamps are averaged, and invalid timestamp/IR/RED rows
are skipped with warnings.

To translate a recording later without retraining:

```powershell
python "denoising/train_cyclegan.py" `
  --moving "hardware/rawdata_leftthumb_moving.csv" `
  --model "hardware/output/wear_twin_cyclegan.pt" `
  --output "hardware/output/moving_stationary_like.csv" `
  --inference-only
```

The translated CSV contains relative resampled time, both input channels, both
generated channels, and a rolling HR estimate. HR follows the logic in
`preprocessing_rawdata/process_ppg.py`: filter the generated IR with a
second-order Butterworth bandpass (0.5–2.5 Hz), then choose the strongest FFT
frequency in the 0.8–2.5 Hz HR band over the trailing 10-second window. The
`--hr-window-seconds` option changes that window length. HR remains blank until
the first full window is available. `HR_Window_Quality_Flag` is 1 when an
estimate is available and 0 otherwise; it is not a confidence or clinical
quality assessment.
