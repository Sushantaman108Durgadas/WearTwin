
# ESP32 Sensor Data Acquisition: MPU6050 & MAX30102

**Embedded C/C++ • ESP32 • I²C • IMU • PPG • CSV Data Logging**

## Overview

This repository contains standalone ESP32 firmware for acquiring motion and optical pulse-sensing data using two I²C sensors:

- **MPU6050:** Acquires 3-axis accelerometer and gyroscope measurements and estimates roll and pitch using a Kalman filter.
- **MAX30102:** Acquires raw infrared (IR) and red photoplethysmography (PPG) signals and estimates heart rate (HR) and blood oxygen saturation (SpO₂) using the Maxim algorithm.

Both programs output timestamped, comma-separated data over the ESP32 serial interface, making it easy to capture measurements in CSV files for signal processing, visualization, and further analysis.

The sensors have separate firmware files so they can be tested and operated independently.

## Repository Structure

```text
.
├── MPU6050/
│   └── MPU6050.ino
├── MAX30102/
│   └── MAX30102.ino
└── README.md
```

*Adjust the filenames and folder structure above to match your actual repository.*

## Hardware Requirements

- ESP32 development board
- MPU6050 accelerometer and gyroscope module
- MAX30102 optical sensor module
- Jumper wires
- USB data cable
- Computer with Arduino IDE

## Hardware Connections

Both sensors communicate with the ESP32 over the I²C interface.

| Sensor Pin | ESP32 Connection |
|---|---|
| MPU6050 VCC | Module-compatible supply voltage |
| MPU6050 GND | GND |
| MPU6050 SDA | GPIO 21 |
| MPU6050 SCL | GPIO 22 |
| MAX30102 VIN | Supply supported by your module |
| MAX30102 GND | GND |
| MAX30102 SDA | GPIO 21 |
| MAX30102 SCL | GPIO 22 |

**Important:** Both sensors share the same SDA, SCL, and ground connections. Verify the voltage requirements of your specific sensor breakout boards before powering them.

## 1. MPU6050: Motion Data Acquisition

The MPU6050 firmware reads accelerometer and gyroscope measurements at a target sampling frequency of approximately **100 Hz**.

### Features

- 3-axis accelerometer acquisition
- 3-axis gyroscope acquisition
- Gyroscope bias calibration during initialization
- Roll and pitch estimation using a Kalman filter
- Timestamped serial output
- CSV-compatible data format

### Output Format

```csv
Timestamp_ms,Accel_X,Accel_Y,Accel_Z,Gyro_X,Gyro_Y,Gyro_Z,Roll,Pitch
```

| Field | Description |
|---|---|
| `Timestamp_ms` | Elapsed time since the ESP32 started |
| `Accel_X`, `Accel_Y`, `Accel_Z` | Acceleration along the three axes |
| `Gyro_X`, `Gyro_Y`, `Gyro_Z` | Angular velocity along the three axes |
| `Roll` | Estimated rotation about the roll axis |
| `Pitch` | Estimated rotation about the pitch axis |

The accelerometer and gyroscope readings are provided by the sensor library in its configured units; in the Adafruit library, acceleration is expressed in m/s² and angular velocity in rad/s unless converted in the firmware. If your sketch converts gyroscope readings to degrees/s, the corresponding output columns will be in degrees/s.

## 2. MAX30102: PPG Data Acquisition

The MAX30102 firmware acquires optical measurements using its red and infrared channels. The raw signals can be used to examine pulse waveforms and explore physiological signal processing.

### Features

- Raw red and infrared data acquisition
- PPG waveform data logging
- Heart-rate estimation
- SpO₂ estimation
- Validity flags for algorithm results
- Timestamped CSV-compatible serial output

### Output Format

```csv
Timestamp_ms,IR,RED,SpO2,SpO2_Valid,HR_Algorithm,HR_Valid
```

| Field | Description |
|---|---|
| `Timestamp_ms` | Elapsed time since the ESP32 started |
| `IR` | Raw infrared-channel measurement |
| `RED` | Raw red-channel measurement |
| `SpO2` | Estimated oxygen saturation percentage |
| `SpO2_Valid` | Indicates whether the algorithm considers the SpO₂ estimate valid |
| `HR_Algorithm` | Estimated heart rate in beats per minute |
| `HR_Valid` | Indicates whether the algorithm considers the heart-rate estimate valid |

The firmware uses the Maxim heart-rate and SpO₂ algorithm. Estimates may be unavailable or inaccurate when signal quality is poor, the sensor is not positioned correctly, or movement introduces artifacts.

## Software Requirements

Install the following through the Arduino IDE Library Manager or the corresponding library sources:

### MPU6050 firmware
- Adafruit MPU6050
- Adafruit Unified Sensor
- Wire library (included with the ESP32 Arduino core)

### MAX30102 firmware
- SparkFun MAX3010x Sensor Library
- The `spo2_algorithm.h` header included with the relevant Maxim/SparkFun example or library version
- Wire library (included with the ESP32 Arduino core)

The exact library versions and header availability may vary. Ensure that your installed library provides the APIs used in the sketch.

## Getting Started

1. Install the ESP32 board support package in Arduino IDE.
2. Install the required libraries.
3. Connect the sensor corresponding to the sketch you want to run.
4. Open the relevant `.ino` file.
5. Select your ESP32 board and the correct serial port.
6. Upload the firmware.
7. Open Serial Monitor at **115200 baud**.
8. Confirm that the CSV header and subsequent data rows are being printed.

For the MAX30102, keep the finger still and properly positioned over the optical sensor while collecting data. Allow the readings to stabilize before evaluating the estimates.

## Exporting Serial Data to CSV

The firmware prints comma-separated rows, which can be captured using a serial logging script.

Example Python dependencies:

```bash
pip install pyserial
```

A serial logger can read the ESP32's COM port and save each line to a CSV file.

Suggested filenames:

```text
mpu_data.csv
ppg_data.csv
```

Close Arduino Serial Monitor or Serial Plotter before starting a separate Python logger, because only one application can generally access the serial port at a time.

The timestamps represent elapsed milliseconds since startup, not calendar date and time. For long-duration recordings or synchronized datasets, account for sampling intervals and possible timestamp resolution limitations.

## Potential Applications

- Motion and orientation analysis
- PPG waveform visualization
- Sensor data collection for signal-processing experiments
- Activity and physiological-signal correlation
- Dataset preparation for future machine-learning experiments
- Prototyping wearable IoT monitoring systems

## Limitations

- The MPU6050 roll and pitch estimates can drift or become inaccurate under dynamic acceleration.
- Optical PPG measurements are sensitive to movement, sensor contact, ambient light, and individual differences.
- HR and SpO₂ values are algorithmic estimates and are not guaranteed to be medically accurate.
- The project is intended for educational prototyping and research, not diagnosis, treatment, or emergency monitoring.

## Future Improvements

- Combine both sensor streams into a synchronized acquisition pipeline.
- Add a robust serial-to-CSV logging utility.
- Visualize live accelerometer, gyroscope, and PPG waveforms.
- Implement signal-quality checks and motion-artifact analysis.
- Develop preprocessing and feature-extraction pipelines for machine-learning experiments.

