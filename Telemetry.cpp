#include <Arduino.h>
#include <ArduinoJson.h>
#include "Telemetry.h"
#include "DeviceConfig.h"
#include "AtClient.h"
#include "GpsHandler.h"
#include "config.h"

int msgCounter = 0;
int failCount  = 0;

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

    // Build payload
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

    // 4-20 mA analog channels
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

    // Open TCP connection
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
