#pragma once

#include <ArduinoJson.h>
#include "config.h"

// ── Capabilities — fields this firmware can provide ───────────────────────
extern const char* CAPABILITIES[];
extern const int   CAP_COUNT;

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
    unsigned long intervalMs    = DEFAULT_INTERVAL_MS;
    char          endpoint[200] = {};
    bool          fields[9]     = {};   // indexed by CAPABILITIES order
    AnalogChannel analog[2];
};

extern DeviceConfig cfg;

// ── Functions ──────────────────────────────────────────────────────────────
void applyJsonConfig(JsonObject obj);
void loadConfig();
void saveConfig(JsonObject obj);
