#include <Arduino.h>
#include <Preferences.h>
#include "DeviceConfig.h"

// ── Globals ────────────────────────────────────────────────────────────────
const char* CAPABILITIES[] = {
    "latitude", "longitude", "altitude", "speed",
    "course", "satellites", "hdop", "rssi", "battery_voltage", "battery_charge", "battery_charge_state", "temperature"
};
const int CAP_COUNT = sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]);

DeviceConfig cfg;

static Preferences prefs;
static Preferences provPrefs;

static char runtimeSerialNumber[64] = SERIAL_NUMBER;
static char runtimeSessionId[64]  = DEVICE_SESSION;
static char endpointOverride[200] = {};
static bool provisioningFromNvs   = false;

static bool isValidSerialNumber(const char* serial) {
    if (!serial || serial[0] == '\0') return false;
    for (size_t i = 0; serial[i] != '\0'; i++) {
        char c = serial[i];
        bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

static void applyProvisionedEndpointIfNeeded() {
    if (strlen(endpointOverride) > 0) {
        strlcpy(cfg.endpoint, endpointOverride, sizeof(cfg.endpoint));
    }
}

// ── Implementation ─────────────────────────────────────────────────────────

void applyJsonConfig(JsonObject obj) {
    unsigned long newInterval = obj["intervalMs"] | (long)cfg.intervalMs;
    cfg.intervalMs = constrain(newInterval, 10000UL, 3600000UL);

    const char* ep = obj["endpoint"];
    if (ep && strlen(ep) > 0)
        strlcpy(cfg.endpoint, ep, sizeof(cfg.endpoint));

    const char* host = obj["apiHost"];
    if (host && strlen(host) > 0)
        strlcpy(cfg.apiHost, host, sizeof(cfg.apiHost));

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

    JsonObject conn = obj["connectivity"];
    if (!conn.isNull()) {
        const char* type = conn["type"];
        if (type) strlcpy(cfg.connectivityType, type, sizeof(cfg.connectivityType));
        const char* ssid = conn["wifiSsid"];
        if (ssid) strlcpy(cfg.wifiSsid, ssid, sizeof(cfg.wifiSsid));
        const char* pass = conn["wifiPassword"];
        if (pass) strlcpy(cfg.wifiPassword, pass, sizeof(cfg.wifiPassword));
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
    strlcpy(cfg.apiHost, API_HOST, sizeof(cfg.apiHost));
    strlcpy(cfg.endpoint, API_ENDPOINT, sizeof(cfg.endpoint));
    cfg.fields[0] = true;  // latitude
    cfg.fields[1] = true;  // longitude
    strlcpy(cfg.connectivityType, "gsm", sizeof(cfg.connectivityType));

    prefs.begin("devconfig", true);
    String json = prefs.getString("cfg", "");
    prefs.end();

    if (json.length() == 0) {
        Serial.println("[cfg] No stored config, using defaults");
        return;
    }

    Serial.printf("[cfg] Retrieved JSON: %s\n", json.c_str());

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        Serial.println("[cfg] Parse error, using defaults");
        return;
    }

    applyJsonConfig(doc.as<JsonObject>());
    Serial.printf("[cfg] Loaded — interval=%lums host=%s endpointBase=%s serial=%s connectivity=%s\n",
                  cfg.intervalMs, cfg.apiHost, cfg.endpoint, runtimeSerialNumber, cfg.connectivityType);
    Serial.println("[cfg] Enabled fields:");
    for (int i = 0; i < CAP_COUNT; i++) {
        if (cfg.fields[i]) Serial.printf("  - %s\n", CAPABILITIES[i]);
    }
}

void saveConfig(JsonObject obj) {
    String json;
    serializeJson(obj, json);
    prefs.begin("devconfig", false);
    prefs.putString("cfg", json);
    prefs.end();
    Serial.println("[cfg] Saved to NVS");
}

void loadProvisioning() {
    strlcpy(runtimeSerialNumber, SERIAL_NUMBER, sizeof(runtimeSerialNumber));
    strlcpy(runtimeSessionId, DEVICE_SESSION, sizeof(runtimeSessionId));
    endpointOverride[0] = '\0';

    provPrefs.begin("provision", true);
    provisioningFromNvs =
        provPrefs.isKey("serialNumber") ||
        provPrefs.isKey("sessionId") ||
        provPrefs.isKey("endpoint");

    String serial = provPrefs.getString("serialNumber", SERIAL_NUMBER);
    if (serial.length() > 0) strlcpy(runtimeSerialNumber, serial.c_str(), sizeof(runtimeSerialNumber));

    if (!isValidSerialNumber(runtimeSerialNumber)) {
        Serial.printf("[prov] warning: invalid serial-number '%s', falling back to '%s'\n",
                      runtimeSerialNumber, SERIAL_NUMBER);
        strlcpy(runtimeSerialNumber, SERIAL_NUMBER, sizeof(runtimeSerialNumber));
    }

    String ses = provPrefs.getString("sessionId", DEVICE_SESSION);
    if (ses.length() > 0) strlcpy(runtimeSessionId, ses.c_str(), sizeof(runtimeSessionId));

    String ep = provPrefs.getString("endpoint", "");
    if (ep.length() > 0) strlcpy(endpointOverride, ep.c_str(), sizeof(endpointOverride));
    provPrefs.end();

    applyProvisionedEndpointIfNeeded();

}

const char* getRuntimeSerialNumber() {
    return runtimeSerialNumber;
}

const char* getRuntimeSessionId() {
    return runtimeSessionId;
}

bool setRuntimeSerialNumber(const char* serialNumber) {
    if (!isValidSerialNumber(serialNumber)) return false;
    strlcpy(runtimeSerialNumber, serialNumber, sizeof(runtimeSerialNumber));

    provPrefs.begin("provision", false);
    provPrefs.putString("serialNumber", runtimeSerialNumber);
    provPrefs.end();

    provisioningFromNvs = true;

    applyProvisionedEndpointIfNeeded();
    return true;
}

bool setRuntimeSessionId(const char* sessionId) {
    if (!sessionId || strlen(sessionId) == 0) return false;
    strlcpy(runtimeSessionId, sessionId, sizeof(runtimeSessionId));

    provPrefs.begin("provision", false);
    provPrefs.putString("sessionId", runtimeSessionId);
    provPrefs.end();
    provisioningFromNvs = true;
    return true;
}

bool setRuntimeEndpoint(const char* endpoint) {
    if (!endpoint || strlen(endpoint) == 0) return false;
    strlcpy(endpointOverride, endpoint, sizeof(endpointOverride));
    strlcpy(cfg.endpoint, endpointOverride, sizeof(cfg.endpoint));

    provPrefs.begin("provision", false);
    provPrefs.putString("endpoint", endpointOverride);
    provPrefs.end();
    provisioningFromNvs = true;
    return true;
}

void clearProvisioning() {
    provPrefs.begin("provision", false);
    provPrefs.remove("serialNumber");
    provPrefs.remove("sessionId");
    provPrefs.remove("endpoint");
    provPrefs.end();

    strlcpy(runtimeSerialNumber, SERIAL_NUMBER, sizeof(runtimeSerialNumber));
    strlcpy(runtimeSessionId, DEVICE_SESSION, sizeof(runtimeSessionId));
    endpointOverride[0] = '\0';
    provisioningFromNvs = false;
    strlcpy(cfg.endpoint, API_ENDPOINT, sizeof(cfg.endpoint));
}

void printProvisioningStatus(Stream& out) {
    out.println("--- Provisioning ---");
    out.printf("Serial Number: %s\n", runtimeSerialNumber);
    out.printf("Session      : %s\n", runtimeSessionId);
    out.printf("Endpoint base: %s\n", cfg.endpoint);
    out.printf("Endpoint src : %s\n", strlen(endpointOverride) > 0 ? "manual override" : "default/server");
}

bool isProvisioningFromNvs() {
    return provisioningFromNvs;
}

