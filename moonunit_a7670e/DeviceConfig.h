#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// ── Capabilities — fields this firmware can provide ───────────────────────
extern const char* CAPABILITIES[];
extern const int   CAP_COUNT;
constexpr int      MAX_CAPABILITIES = 12;

// ── Config structs ─────────────────────────────────────────────────────────
struct AnalogChannel {
    bool    enabled  = false;
    int     pin      = 0;
    char    name[64] = {};
    char    unit[32] = {};
    float   rangeMin = 0.0f;
    float   rangeMax = 100.0f;
};

struct DeviceConfig {
    unsigned long intervalMs         = DEFAULT_INTERVAL_MS;
    char          apiHost[128]       = {};
    char          endpoint[200]      = {};
    bool          fields[MAX_CAPABILITIES] = {};   // indexed by CAPABILITIES order
    AnalogChannel analog[2];
    char          connectivityType[8] = "gsm"; // "gsm", "wifi", "none"
    char          wifiSsid[64]        = {};
    char          wifiPassword[64]    = {};
};

extern DeviceConfig cfg;

// ── Functions ──────────────────────────────────────────────────────────────
void applyJsonConfig(JsonObject obj);
void loadConfig();
void saveConfig(JsonObject obj);

// Runtime identity/provisioning (stored in NVS namespace: "provision")
void        loadProvisioning();
const char* getRuntimeSerialNumber();
const char* getRuntimeSessionId();
bool        setRuntimeSerialNumber(const char* serialNumber);
bool        setRuntimeSessionId(const char* sessionId);
bool        setRuntimeEndpoint(const char* endpoint);
void        clearProvisioning();
void        printProvisioningStatus(Stream& out);
bool        isProvisioningFromNvs();

