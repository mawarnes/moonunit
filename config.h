#pragma once

// =============================================================
// Metron View ESP32 Telemetry Unit — Hardware & Network Config
// Edit this file for each unit type / deployment environment.
// =============================================================

// --- SIM800L modem pins ---
#define MODEM_RST        5
#define MODEM_PWKEY      4
#define MODEM_POWER_ON   23
#define MODEM_TX         27
#define MODEM_RX         26

// --- Neo 6M GPS pins ---
#define GPS_RX           32
#define GPS_TX           33

// --- 4-20 mA ADC pins (input-only, 165Ω shunt to GND) ---
#define ANALOG_CH1_PIN   34
#define ANALOG_CH2_PIN   35

// --- Cellular network ---
#define APN              "internet"   // O2: "internet" | Vodafone: "pp.vodafone.co.uk"

// --- Telemetry API ---
#define API_HOST         "telemetry-api-functions.azurewebsites.net"
#define API_PORT         80
#define UNIT_ID          2            // TelemetryHub ID in the database

// --- Device identity (sent in every payload) ---
#define DEVICE_SESSION   "esp32-unit"
#define DEVICE_ID        "unit-2"

// --- Defaults (overridden by server config after first successful send) ---
#define DEFAULT_INTERVAL_MS  60000UL  // 60 seconds
