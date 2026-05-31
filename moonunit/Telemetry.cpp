#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Telemetry.h"
#include "DeviceConfig.h"
#include "AtClient.h"
#include "GpsHandler.h"
#include "config.h"

int msgCounter = 0;
int failCount  = 0;

// ── Connectivity init ──────────────────────────────────────────────────────

bool initConnectivity() {
    // Keep WiFi radio off when using GSM — it can interfere with SIM800L
    if (strcmp(cfg.connectivityType, "gsm") == 0 || strcmp(cfg.connectivityType, "none") == 0) {
        WiFi.mode(WIFI_OFF);
        btStop();
    }

    if (strcmp(cfg.connectivityType, "wifi") == 0) {
        if (strlen(cfg.wifiSsid) == 0) {
            Serial.println("[wifi] No SSID configured");
            return false;
        }
        Serial.printf("[wifi] Connecting to %s...\n", cfg.wifiSsid);
        WiFi.begin(cfg.wifiSsid, cfg.wifiPassword);
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
            delay(500);
            Serial.print(".");
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("\n[wifi] Connection failed");
            return false;
        }
        Serial.printf("\n[wifi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else if (strcmp(cfg.connectivityType, "gsm") == 0) {
        atCmd("AT", 1000);
        waitForNetwork();
        return setupInternet();
    }
    Serial.println("[net] Connectivity type is 'none', skipping");
    return false;
}

void disconnectWifi() {
    if (strcmp(cfg.connectivityType, "wifi") == 0) {
        WiFi.disconnect(true);
    }
}

void handleResponse(const String& raw) {
    // Print first line of HTTP response for debugging
    int firstLine = raw.indexOf("\r\n");
    if (firstLine > 0)
        Serial.printf("[http] %s\n", raw.substring(0, firstLine).c_str());

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

// Build the JSON payload string — shared by both transports
static String buildPayload() {
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

    if (payload.size() == 0) return "";

    String json;
    serializeJson(doc, json);
    return json;
}

static void sendViaGsm(const String& json) {
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

    delay(4000);
    atCmd("AT+CIPSEND=" + String(req.length()), 3000);
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

static void sendViaWifi(const String& json) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] Not connected, reconnecting...");
        WiFi.reconnect();
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(500);
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[wifi] Reconnect failed");
            failCount++;
            return;
        }
    }

    String url = "http://" + String(API_HOST) + String(cfg.endpoint);
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(json);

    if (code == 200) {
        String body = http.getString();
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            JsonObject configObj = doc["config"];
            if (!configObj.isNull()) {
                applyJsonConfig(configObj);
                saveConfig(configObj);
                Serial.printf("[cfg] Updated from server — interval=%lums\n", cfg.intervalMs);
            }
        }
        failCount = 0;
        Serial.println("[wifi] Sent successfully");
    } else {
        Serial.printf("[wifi] HTTP error: %d\n", code);
        failCount++;
    }
    http.end();
}

void sendTelemetry() {
    if (strcmp(cfg.connectivityType, "none") == 0) {
        Serial.println("[send] Connectivity is none, skipping");
        return;
    }

    if (!gps.location.isValid()) {
        Serial.println("[gps] No fix, skipping send");
        return;
    }

    String json = buildPayload();
    if (json.isEmpty()) {
        Serial.println("[send] No enabled fields, skipping");
        return;
    }

    if (strcmp(cfg.connectivityType, "wifi") == 0) {
        sendViaWifi(json);
    } else if (strcmp(cfg.connectivityType, "gsm") == 0) {
        sendViaGsm(json);
    }
}
