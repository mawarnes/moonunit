#include <Arduino.h>
#include <Preferences.h>
#include "DeviceConfig.h"

// ── Globals ────────────────────────────────────────────────────────────────
const char* CAPABILITIES[] = {
    "latitude", "longitude", "altitude", "speed",
    "course", "satellites", "hdop", "rssi", "battery_voltage"
};
const int CAP_COUNT = sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]);

DeviceConfig cfg;

static Preferences prefs;

// ── Implementation ─────────────────────────────────────────────────────────

void applyJsonConfig(JsonObject obj) {
    unsigned long newInterval = obj["intervalMs"] | (long)cfg.intervalMs;
    cfg.intervalMs = constrain(newInterval, 10000UL, 3600000UL);

    const char* ep = obj["endpoint"];
    if (ep && strlen(ep) > 0)
        strlcpy(cfg.endpoint, ep, sizeof(cfg.endpoint));

    JsonArray fields = obj["fields"];
    if (fields) {
        for (int i = 0; i < CAP_COUNT; i++) cfg.fields[i] = false;
        for (JsonVariant f : fields) {
            const char* fname = f.as<const char*>();
            if (!fname) continue;
            for (int i = 0; i < CAP_COUNT; i++) {
                if (strcasecmp(fname, CAPABILITIES[i]) == 0) {
                    cfg.fields[i] = true;
                    break;
                }
            }
        }
    }

    JsonArray analog = obj["analogSensors"];
    if (analog) {
        for (JsonObject ch : analog) {
            int channel = ch["channel"] | 0;
            if (channel < 1 || channel > 2) continue;
            AnalogChannel& ac = cfg.analog[channel - 1];
            ac.enabled  = ch["enabled"]  | false;
            ac.pin      = (channel == 1) ? ANALOG_CH1_PIN : ANALOG_CH2_PIN;
            ac.rangeMin = ch["rangeMin"] | 0.0f;
            ac.rangeMax = ch["rangeMax"] | 100.0f;
            const char* n = ch["name"]; if (n) strlcpy(ac.name, n, sizeof(ac.name));
            const char* u = ch["unit"]; if (u) strlcpy(ac.unit, u, sizeof(ac.unit));
        }
    }
}

void loadConfig() {
    // Set defaults
    snprintf(cfg.endpoint, sizeof(cfg.endpoint),
             "/api/ingest/sensorlogger/%d", UNIT_ID);
    cfg.fields[0] = true;  // latitude
    cfg.fields[1] = true;  // longitude

    prefs.begin("devconfig", true);
    String json = prefs.getString("cfg", "");
    prefs.end();

    if (json.length() == 0) {
        Serial.println("[cfg] No stored config, using defaults");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        Serial.println("[cfg] Parse error, using defaults");
        return;
    }

    applyJsonConfig(doc.as<JsonObject>());
    Serial.printf("[cfg] Loaded — interval=%lums endpoint=%s\n",
                  cfg.intervalMs, cfg.endpoint);
}

void saveConfig(JsonObject obj) {
    String json;
    serializeJson(obj, json);
    prefs.begin("devconfig", false);
    prefs.putString("cfg", json);
    prefs.end();
    Serial.println("[cfg] Saved to NVS");
}
