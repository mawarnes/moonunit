#pragma once

// =============================================================
// Metron View ESP32 Telemetry Unit — Hardware & Network Config
// Target: LILYGO T-SIM A7670E SA R2 (4G LTE CAT1)
// Edit this file for each unit type / deployment environment.
// =============================================================

// --- A7670E modem pins (LILYGO T-SIM A7670E SA R2) ---
#define MODEM_RST        5
#define MODEM_PWKEY      4
#define MODEM_POWER_ON   12   // Board power enable (GPIO12 on LILYGO, was 23 on SIM800L board)
#define MODEM_TX         26
#define MODEM_RX         27

// --- 4-20 mA ADC pins
#define ANALOG_CH1_PIN   34
#define ANALOG_CH2_PIN   35

// --- Cellular network ---
#define APN              "internet"   // O2: "internet" | Vodafone: "pp.vodafone.co.uk"

// --- Telemetry API ---
#define API_HOST         "telemetry-api-functions.azurewebsites.net"
#define UNIT_ID          2            // TelemetryHub ID in the database

// --- Device identity (sent in every payload) ---
#define DEVICE_SESSION   "esp32-unit"
#define DEVICE_ID        "unit-2"

// --- Defaults (overridden by server config after first successful send) ---
#define DEFAULT_INTERVAL_MS  60000UL  // 60 seconds

// --- Debug logging ---
// When true, prints raw AT command/response traffic and enables Serial<->modem passthrough.
#define MODEM_TRAFFIC_DEBUG  false

// --- Battery voltage source ---
// If enabled, battery_voltage is read from BAT_ADC_PIN and scaled by BAT_ADC_DIVIDER.
#define USE_ADC_BATTERY_VOLTAGE 1
#define BAT_ADC_PIN             35
#define BAT_ADC_DIVIDER         2.0f

