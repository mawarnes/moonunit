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

### 1. Install Arduino IDE and ESP32 support

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software)
2. Go to **File → Preferences** and add this to *Additional boards manager URLs*:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Go to **Tools → Board → Boards Manager**, search `esp32`, install **esp32 by Espressif**

### 2. Install libraries

Go to **Tools → Manage Libraries** and install:
- **TinyGPSPlus** by Mikal Hart
- **ArduinoJson** by Benoit Blanchon

### 3. Open the sketch

Open `moonunit.ino` in Arduino IDE. Both `moonunit.ino` and `config.h` must be in the same folder.

### 4. Configure for your unit

Edit `config.h`:

```c
#define APN         "internet"   // your SIM's APN
#define UNIT_ID     2            // TelemetryHub ID from the database
#define DEVICE_ID   "unit-2"     // human-readable device name
```

### 5. Build and flash

- Select **Tools → Board → esp32 → ESP32 Dev Module**
- Select the correct port under **Tools → Port**
- Disconnect the GPS module, then click **Upload** (→)
- Reconnect the GPS after flashing
- Open **Tools → Serial Monitor** at **115200 baud** to see output

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
