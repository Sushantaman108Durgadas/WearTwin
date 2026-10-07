# DSP experiments

These files are kept as standalone DSP experiments/reference implementations.
They are intentionally outside the Arduino sketch source directory so they are
not compiled into the ESP32/Wokwi firmware.

The production ECG path currently uses the 2nd-order 35 Hz Butterworth IIR
inside `CardialState`.
