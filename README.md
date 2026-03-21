# Metron View ESP32 Telemetry Unit

GPS + SIM800L cellular telemetry with remote configuration via the Metron View API.

## Features

- Sends GPS location and optional 4–20 mA sensor readings to the telemetry API
- Remote configuration: interval, fields, and analog sensor names/ranges are controlled by the server
- Config persists in NVS (survives reboots)
- Field filtering: only sends fields the firmware knows about and the server has requested

## Hardware

| Component      | Connection                          |
|----------------|-------------------------------------|
| Neo 6M GPS     | TX→GPIO32, RX→GPIO33, VCC→5V, GND  |
| SIM800L modem  | RX→GPIO26, TX→GPIO27 + PWR pins     |
| 4–20mA Ch1     | GPIO34, via 165Ω shunt to GND       |
| 4–20mA Ch2     | GPIO35, via 165Ω shunt to GND       |

> **Note:** GPIO16/17 are reserved for PSRAM on WROVER-based boards. Do not use them for peripherals.

## Setup

### 1. Install VS Code + PlatformIO

1. Install [VS Code](https://code.visualstudio.com/)
2. Open the Extensions panel and install **PlatformIO IDE**
3. Restart VS Code

### 2. Open the project

Open the `esp32-telemetry-firmware` folder in VS Code. PlatformIO will automatically install the ESP32 platform and libraries defined in `platformio.ini` on first build.

### 3. Configure for your unit

Edit `include/config.h`:

```c
#define APN         "internet"   // your SIM's APN
#define UNIT_ID     2            // TelemetryHub ID from the database
#define DEVICE_ID   "unit-2"     // human-readable device name
```

### 4. Build and flash

- **Build:** click the ✓ (tick) button in the PlatformIO toolbar, or run `pio run`
- **Flash:** connect the board via USB (disconnect GPS first), click → (arrow) button, or run `pio run --target upload`
- **Serial monitor:** click the plug icon, or run `pio device monitor`

> Disconnect the GPS module before flashing, then reconnect after upload.

## CI/CD

Every push to `main` compiles the firmware via GitHub Actions.
Creating a GitHub Release (tag e.g. `v1.0.0`) attaches `firmware.bin` to the release for distribution.

## Wiring diagram — 4–20 mA input

```
Sensor (source)
    +──────────────── GPIO34 or GPIO35
    |
   [165 Ω]
    |
   GND
```

With a 165 Ω shunt: 4 mA = 0.66 V, 20 mA = 3.3 V — within the ESP32 ADC range.
