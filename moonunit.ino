/*
 * Metron View ESP32 Telemetry Unit — "Moon Unit"
 *
 * GPS + SIM800L cellular telemetry with remote configuration.
 * Config is returned by the API on each successful send and persisted
 * in NVS so settings survive reboots.
 *
 * Hardware:
 *   AM-036 ESP32 dev board
 *   Neo 6M GPS     : TX→GPIO32, RX→GPIO33, VCC→5V, GND→GND
 *   SIM800L modem  : RX→GPIO26, TX→GPIO27 (plus RST/PWKEY/PWR pins)
 *   4-20mA Ch1     : GPIO34 via 165Ω shunt resistor to GND
 *   4-20mA Ch2     : GPIO35 via 165Ω shunt resistor to GND
 *
 * Libraries required (install via Tools → Manage Libraries):
 *   - TinyGPSPlus by Mikal Hart
 *   - ArduinoJson by Benoit Blanchon
 *
 * NOTE: SIM800L does not support HTTPS. Ensure the Azure Function App
 * has "HTTPS Only" disabled, or place a reverse proxy in front.
 */

#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"

// ── Serial interfaces ──────────────────────────────────────────────────────
HardwareSerial SerialAT(1);   // SIM800L on UART1
HardwareSerial gpsSerial(2);  // GPS on UART2
TinyGPSPlus gps;
Preferences prefs;

// ── Capabilities — fields this firmware can provide ───────────────────────
static const char* CAPABILITIES[] = {
    "latitude", "longitude", "altitude", "speed",
    "course", "satellites", "hdop", "rssi", "battery_voltage"
};
static const int CAP_COUNT = sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]);

// ── Runtime config ─────────────────────────────────────────────────────────
struct AnalogChannel {
    bool    enabled  = false;
    int     pin      = 0;
    char    name[64] = {};
    char    unit[32] = {};
    float   rangeMin = 0.0f;
    float   rangeMax = 100.0f;
};

struct DeviceConfig {
    unsigned long intervalMs = DEFAULT_INTERVAL_MS;
    char          endpoint[200] = {};
    bool          fields[9]     = {};
    AnalogChannel analog[2];
} cfg;

// ── State ──────────────────────────────────────────────────────────────────
unsigned long lastSend   = 0;
int           msgCounter = 0;
bool          netReady   = false;
int           failCount  = 0;


// ══════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════

bool isFieldEnabled(const char* name) {
    for (int i = 0; i < CAP_COUNT; i++) {
        if (strcasecmp(name, CAPABILITIES[i]) == 0)
            return cfg.fields[i];
    }
    return false;
}

// Read a 4-20 mA sensor via a 165 Ω shunt resistor.
// 4 mA → 0.66 V → ~819 ADC   (sensor min → rangeMin)
// 20 mA → 3.3 V → 4095 ADC   (sensor max → rangeMax)
double readAnalog4to20(int pin, float rangeMin, float rangeMax) {
    int   raw = analogRead(pin);
    float mA  = (raw / 4095.0f) * 20.0f;
    mA = constrain(mA, 4.0f, 20.0f);
    return rangeMin + ((mA - 4.0f) / 16.0f) * (rangeMax - rangeMin);
}


// ══════════════════════════════════════════════════════════════════════════
// Config — NVS persistence
// ══════════════════════════════════════════════════════════════════════════

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


// ══════════════════════════════════════════════════════════════════════════
// AT command helpers
// ══════════════════════════════════════════════════════════════════════════

void atCmd(const String& cmd, int timeout) {
    Serial.print(">> "); Serial.println(cmd);
    SerialAT.println(cmd);
    unsigned long t = millis();
    while (millis() - t < (unsigned long)timeout) {
        if (SerialAT.available()) Serial.write(SerialAT.read());
    }
    Serial.println();
}

String atCmdCapture(const String& cmd, int timeout) {
    Serial.print(">> "); Serial.println(cmd);
    SerialAT.println(cmd);
    String resp;
    unsigned long t = millis();
    while (millis() - t < (unsigned long)timeout) {
        if (SerialAT.available()) {
            char c = SerialAT.read();
            resp += c;
            Serial.write(c);
        }
    }
    Serial.println();
    return resp;
}

void waitForNetwork() {
    for (int i = 0; i < 20; i++) {
        SerialAT.println("AT+CREG?");
        String r;
        unsigned long t = millis();
        while (millis() - t < 3000) {
            if (SerialAT.available()) r += (char)SerialAT.read();
        }
        if (r.indexOf("+CREG: 0,1") >= 0 || r.indexOf("+CREG: 0,5") >= 0 ||
            r.indexOf("+CREG:0,1")  >= 0 || r.indexOf("+CREG:0,5")  >= 0) {
            Serial.println("[gsm] Network registered");
            return;
        }
        Serial.printf("[gsm] Waiting for network (%d/20)...\n", i + 1);
        delay(3000);
    }
    Serial.println("[gsm] Network timeout — continuing anyway");
}

bool setupInternet() {
    atCmd("AT+CIPSHUT", 2000);
    atCmd("AT+CSTT=\"" + String(APN) + "\",\"\",\"\"", 2000);
    atCmd("AT+CIICR", 15000);
    atCmd("AT+CIFSR", 3000);
    return true;
}

void reconnectGPRS() {
    atCmd("AT+CIPSHUT", 5000);
    atCmd("AT+CSTT=\"" + String(APN) + "\",\"\",\"\"", 2000);
    atCmd("AT+CIICR", 15000);
    atCmd("AT+CIFSR", 3000);
    Serial.println("[gsm] GPRS reconnected");
}


// ══════════════════════════════════════════════════════════════════════════
// Telemetry
// ══════════════════════════════════════════════════════════════════════════

void handleResponse(const String& raw) {
    if (raw.indexOf("200 OK") < 0) {
        Serial.println("[http] Non-200 response");
        failCount++;
        return;
    }

    failCount = 0;

    int bodyIdx = raw.indexOf("\r\n\r\n");
    if (bodyIdx < 0) return;
    String body = raw.substring(bodyIdx + 4);

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[http] Response JSON parse error");
        return;
    }

    JsonObject configObj = doc["config"];
    if (configObj.isNull()) return;

    applyJsonConfig(configObj);
    saveConfig(configObj);
    Serial.printf("[cfg] Updated from server — interval=%lums\n", cfg.intervalMs);
}

void sendTelemetry() {
    if (!gps.location.isValid()) {
        Serial.println("[gps] No fix, skipping send");
        return;
    }

    JsonDocument doc;
    doc["messageId"] = msgCounter++;
    doc["sessionId"] = DEVICE_SESSION;
    doc["deviceId"]  = DEVICE_ID;
    JsonArray payload = doc["payload"].to<JsonArray>();

    auto addField = [&](const char* name, double value) {
        if (isFieldEnabled(name)) {
            JsonObject item = payload.add<JsonObject>();
            item["name"]  = name;
            item["time"]  = 0;
            item["value"] = value;
        }
    };

    addField("latitude",   gps.location.lat());
    addField("longitude",  gps.location.lng());
    addField("altitude",   gps.altitude.meters());
    addField("speed",      gps.speed.kmph());
    addField("course",     gps.course.deg());
    addField("satellites", (double)gps.satellites.value());
    addField("hdop",       gps.hdop.hdop());

    for (int i = 0; i < 2; i++) {
        AnalogChannel& ch = cfg.analog[i];
        if (ch.enabled && strlen(ch.name) > 0) {
            JsonObject item = payload.add<JsonObject>();
            item["name"]  = ch.name;
            item["time"]  = 0;
            item["value"] = readAnalog4to20(ch.pin, ch.rangeMin, ch.rangeMax);
        }
    }

    if (payload.size() == 0) {
        Serial.println("[send] No enabled fields, skipping");
        return;
    }

    String json;
    serializeJson(doc, json);

    String req = "POST " + String(cfg.endpoint) + " HTTP/1.1\r\n";
    req += "Host: " + String(API_HOST) + "\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Content-Length: " + String(json.length()) + "\r\n";
    req += "Connection: close\r\n\r\n";
    req += json;

    String cipCmd  = "AT+CIPSTART=\"TCP\",\"" + String(API_HOST) + "\"," + String(API_PORT);
    String cipResp = atCmdCapture(cipCmd, 10000);

    if (cipResp.indexOf("ERROR") >= 0 || cipResp.indexOf("CLOSED") >= 0) {
        Serial.println("[gsm] TCP failed, reconnecting GPRS...");
        reconnectGPRS();
        cipResp = atCmdCapture(cipCmd, 10000);
        if (cipResp.indexOf("ERROR") >= 0) {
            Serial.println("[gsm] Retry failed");
            if (++failCount >= 5) {
                Serial.println("[gsm] Too many failures — resetting modem");
                digitalWrite(MODEM_RST, LOW); delay(100); digitalWrite(MODEM_RST, HIGH);
                delay(5000);
                atCmd("AT", 1000);
                waitForNetwork();
                setupInternet();
                failCount = 0;
            }
            return;
        }
    }

    delay(2000);
    atCmd("AT+CIPSEND=" + String(req.length()), 2000);
    delay(1000);
    SerialAT.print(req);

    String response;
    unsigned long t = millis();
    while (millis() - t < 10000) {
        if (SerialAT.available()) {
            char c = SerialAT.read();
            response += c;
            Serial.write(c);
        }
    }
    Serial.println();

    handleResponse(response);
    atCmd("AT+CIPCLOSE", 2000);
}


// ══════════════════════════════════════════════════════════════════════════
// Setup & Loop
// ══════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

    pinMode(MODEM_PWKEY,    OUTPUT);
    pinMode(MODEM_RST,      OUTPUT);
    pinMode(MODEM_POWER_ON, OUTPUT);
    digitalWrite(MODEM_PWKEY,    LOW);
    digitalWrite(MODEM_RST,      HIGH);
    digitalWrite(MODEM_POWER_ON, HIGH);

    loadConfig();

    Serial.println("=== Metron View Telemetry Unit ===");
    Serial.printf("Unit ID  : %d\n", UNIT_ID);
    Serial.printf("Interval : %lu ms\n", cfg.intervalMs);
    Serial.printf("Endpoint : %s\n", cfg.endpoint);

    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
    delay(3000);

    digitalWrite(MODEM_RST, LOW);  delay(100);
    digitalWrite(MODEM_RST, HIGH); delay(3000);

    atCmd("AT", 1000);
    waitForNetwork();

    if (setupInternet()) {
        netReady = true;
        sendTelemetry();
        lastSend = millis();
    }
}

void loop() {
    while (gpsSerial.available())
        gps.encode(gpsSerial.read());

    if (netReady && (millis() - lastSend >= cfg.intervalMs)) {
        sendTelemetry();
        lastSend = millis();
    }

    if (SerialAT.available()) Serial.write(SerialAT.read());
    if (Serial.available())   SerialAT.write(Serial.read());
}
