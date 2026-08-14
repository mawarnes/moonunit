#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
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

// Apply a JSON body returned by the server (shared by both transports)
static void handleJsonBody(const String& body) {
    Serial.printf("[cfg] handleJsonBody raw: %s\n", body.c_str());
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return;
    JsonObject configObj = doc["config"];
    if (configObj.isNull()) return;
    applyJsonConfig(configObj);
    saveConfig(configObj);
    Serial.printf("[cfg] Updated from server — interval=%lums\n", cfg.intervalMs);
}

static bool readModemTemperatureC(double& tempC) {
    // SIMCom modules typically return: +CPMUTEMP: <tempC>
    String resp = atCmdCapture("AT+CPMUTEMP", 2000);
    int idx = resp.indexOf("+CPMUTEMP:");
    if (idx < 0) return false;

    String tail = resp.substring(idx + 10);
    tail.trim();

    int end = 0;
    while (end < (int)tail.length()) {
        char c = tail[end];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            end++;
            continue;
        }
        break;
    }
    if (end == 0) return false;

    double t = tail.substring(0, end).toDouble();
    if (!isnan(t) && t > -100.0 && t < 150.0) {
        tempC = t;
        return true;
    }
    return false;
}

static bool readModemBattery(double& chargeState, double& chargePct, double& voltageV) {
    // SIMCom modules typically return: +CBC: <bcs>,<bcl>,<mV>
    String resp = atCmdCapture("AT+CBC", 2000);
    int idx = resp.indexOf("+CBC:");
    if (idx < 0) return false;

    String line = resp.substring(idx + 5);
    line.trim();

    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) return false;

    int bcs = line.substring(0, c1).toInt();
    int bcl = line.substring(c1 + 1, c2).toInt();
    int mV  = line.substring(c2 + 1).toInt();
    if (bcs < 0 || bcs > 2 || bcl < 0 || bcl > 100 || mV < 2500 || mV > 5000) return false;

    chargeState = (double)bcs;
    chargePct = (double)bcl;
    voltageV  = (double)mV / 1000.0;
    return true;
}

static bool readBatteryVoltageAdcV(double& voltageV) {
#if USE_ADC_BATTERY_VOLTAGE
    static bool adcConfigured = false;
    if (!adcConfigured) {
        analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
        adcConfigured = true;
    }

    // Hard guard: never sample battery ADC on a pin currently used by an enabled analog channel.
    static bool conflictWarned = false;
    for (int i = 0; i < 2; i++) {
        if (cfg.analog[i].enabled && cfg.analog[i].pin == BAT_ADC_PIN) {
            if (!conflictWarned) {
                Serial.printf("[telemetry] battery voltage ADC disabled: BAT_ADC_PIN (%d) overlaps analog sensor Ch%d\n", BAT_ADC_PIN, i + 1);
                conflictWarned = true;
            }
            return false;
        }
    }
    conflictWarned = false;

    uint32_t rawMv = analogReadMilliVolts(BAT_ADC_PIN);
    if (rawMv == 0) return false;

    double v = ((double)rawMv / 1000.0) * (double)BAT_ADC_DIVIDER;
    if (v < 2.5 || v > 5.2) return false;

    voltageV = v;
    return true;
#else
    (void)voltageV;
    return false;
#endif
}

// Build the JSON payload string
static String buildPayload() {
    JsonDocument doc;
    doc["messageId"] = msgCounter++;
    doc["sessionId"] = DEVICE_SESSION;
    doc["deviceId"]  = DEVICE_ID;
    JsonArray payload = doc["payload"].to<JsonArray>();

    // Always include GNSS status so the device can check in even before a fix,
    // allowing the server to respond with field config and track fix acquisition.
    { JsonObject o = payload.add<JsonObject>(); o["name"] = "gnssValid";   o["time"] = 0; o["value"] = gnss.valid ? 1.0 : 0.0; }
    { JsonObject o = payload.add<JsonObject>(); o["name"] = "satellites";  o["time"] = 0; o["value"] = (double)gnss.satellites; }

    auto addField = [&](const char* name, double value) {
        if (isFieldEnabled(name)) {
            JsonObject item = payload.add<JsonObject>();
            item["name"]  = name;
            item["time"]  = 0;
            item["value"] = value;
        }
    };

    bool wantLatitude  = isFieldEnabled("latitude");
    bool wantLongitude = isFieldEnabled("longitude");

    if (gnss.valid) {
        if (wantLatitude && wantLongitude) {
            Serial.printf("[telemetry] GNSS read ok: lat=%.6f lon=%.6f\n", gnss.latitude, gnss.longitude);
        } else if (wantLatitude) {
            Serial.printf("[telemetry] GNSS read ok: lat=%.6f (longitude disabled)\n", gnss.latitude);
        } else if (wantLongitude) {
            Serial.printf("[telemetry] GNSS read ok: lon=%.6f (latitude disabled)\n", gnss.longitude);
        }
        addField("latitude",  gnss.latitude);
        addField("longitude", gnss.longitude);
        addField("altitude",  gnss.altitude);
        addField("speed",     gnss.speed);
        addField("course",    gnss.course);
        addField("hdop",      gnss.hdop);
    } else if (wantLatitude || wantLongitude) {
        Serial.println("[telemetry] GNSS read failed: no valid fix for latitude/longitude");
    }

    bool isGsm = (strcmp(cfg.connectivityType, "gsm") == 0);

    bool wantTemperature = isFieldEnabled("temperature");
    if (isGsm && wantTemperature) {
        double modemTempC = 0.0;
        if (readModemTemperatureC(modemTempC)) {
            Serial.printf("[telemetry] modem temperature read ok: %.2fC\n", modemTempC);
            addField("temperature", modemTempC);
        } else {
            Serial.println("[telemetry] modem temperature read failed");
        }
    }

    bool wantBatteryChargeState = isFieldEnabled("battery_charge_state");
    bool wantBatteryCharge      = isFieldEnabled("battery_charge");
    bool wantBatteryVoltage     = isFieldEnabled("battery_voltage");

    double batteryVoltageV = 0.0;
    bool   haveBatteryVoltage = false;
    if (wantBatteryVoltage) {
        if (readBatteryVoltageAdcV(batteryVoltageV)) {
            haveBatteryVoltage = true;
            Serial.printf("[telemetry] battery voltage (ADC) ok: %.3fV\n", batteryVoltageV);
            addField("battery_voltage", batteryVoltageV);
        } else {
            Serial.println("[telemetry] battery voltage (ADC) failed");
        }
    }

    bool needModemBattery = isGsm &&
        (wantBatteryChargeState || wantBatteryCharge || (wantBatteryVoltage && !haveBatteryVoltage));

    if (needModemBattery) {
        double batteryChargeState = 0.0;
        double batteryChargePct   = 0.0;
        double modemBatteryVoltageV = 0.0;
        if (readModemBattery(batteryChargeState, batteryChargePct, modemBatteryVoltageV)) {
            Serial.printf("[telemetry] modem battery read ok: state=%.0f charge=%.0f%% voltage=%.3fV\n",
                          batteryChargeState, batteryChargePct, modemBatteryVoltageV);
            if (wantBatteryChargeState) addField("battery_charge_state", batteryChargeState);
            if (wantBatteryCharge) addField("battery_charge", batteryChargePct);
            if (wantBatteryVoltage && !haveBatteryVoltage) {
                addField("battery_voltage", modemBatteryVoltageV);
                Serial.printf("[telemetry] battery voltage fallback (modem): %.3fV\n", modemBatteryVoltageV);
            }
        } else {
            Serial.println("[telemetry] modem battery read failed");
        }
    }

    for (int i = 0; i < 2; i++) {
        AnalogChannel& ch = cfg.analog[i];
        if (ch.enabled && strlen(ch.name) > 0) {
            JsonObject item = payload.add<JsonObject>();
            item["name"]  = ch.name;
            item["time"]  = 0;
            item["value"] = readAnalog4to20(ch.pin, ch.rangeMin, ch.rangeMax);
        }
    }

    String json;
    serializeJson(doc, json);
    return json;
}

static void sendViaGsm(const String& json) {
    // A7670E built-in HTTP client — handles TLS natively when URL starts with https://
    String url = "https://" + String(API_HOST) + String(cfg.endpoint);
    Serial.printf("[gsm] POST %s\n", url.c_str());
    Serial.printf("[gsm] Body: %s\n", json.c_str());

    atCmd("AT+HTTPTERM", 1000);  // clean up any previous session
    atCmd("AT+HTTPINIT", 2000);
    atCmd("AT+HTTPPARA=\"CID\",1", 1000);
    atCmd("AT+HTTPPARA=\"URL\",\"" + url + "\"", 1000);
    atCmd("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 1000);

    // Upload the POST body — modem replies "DOWNLOAD" then accepts the bytes
    String dataResp = atCmdCapture("AT+HTTPDATA=" + String(json.length()) + ",10000", 3000);
    if (dataResp.indexOf("DOWNLOAD") < 0) {
        Serial.println("[gsm] HTTPDATA prompt not received");
        atCmd("AT+HTTPTERM", 1000);
        if (++failCount >= 5) { reconnectGPRS(); failCount = 0; }
        return;
    }
    SerialAT.print(json);
    delay(500);

    // Execute POST — wait up to 15 s for +HTTPACTION URC
    String actionResp = atCmdCapture("AT+HTTPACTION=1", 15000);
    int actionIdx = actionResp.indexOf("+HTTPACTION:");
    if (actionIdx < 0) {
        Serial.println("[gsm] No HTTPACTION response");
        atCmd("AT+HTTPTERM", 1000);
        if (++failCount >= 5) { reconnectGPRS(); failCount = 0; }
        return;
    }

    // Parse: +HTTPACTION: 1,<statusCode>,<bodyLen>
    String al = actionResp.substring(actionIdx + 13);
    int c1 = al.indexOf(',');
    int c2 = al.indexOf(',', c1 + 1);
    int statusCode = al.substring(c1 + 1, c2).toInt();
    int bodyLen    = al.substring(c2 + 1).toInt();
    Serial.printf("[http] %d (body=%d bytes)\n", statusCode, bodyLen);

    if (statusCode == 200 && bodyLen > 0) {
        String readResp = atCmdCapture("AT+HTTPREAD=0," + String(bodyLen), 5000);
        // Response format: +HTTPREAD: 0,<len>\r\n<data>\r\nOK
        int bodyStart = readResp.indexOf("\r\n", readResp.indexOf("+HTTPREAD:")) + 2;
        String responseBody = readResp.substring(bodyStart);
        Serial.printf("[gsm] Response: %s\n", responseBody.c_str());
        handleJsonBody(responseBody);
        failCount = 0;
    } else if (statusCode != 200) {
        if (++failCount >= 5) {
            Serial.println("[gsm] Too many failures — power cycling modem");
            digitalWrite(MODEM_POWER_ON, LOW);  delay(3000);
            digitalWrite(MODEM_POWER_ON, HIGH);
            digitalWrite(MODEM_PWKEY, HIGH);    delay(1500);
            digitalWrite(MODEM_PWKEY, LOW);
            delay(5000);
            atCmd("AT", 1000);
            waitForNetwork();
            setupInternet();
            failCount = 0;
        }
    }

    atCmd("AT+HTTPTERM", 1000);
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

    String url = "https://" + String(API_HOST) + String(cfg.endpoint);
    Serial.printf("[wifi] POST %s\n", url.c_str());
    Serial.printf("[wifi] Body: %s\n", json.c_str());
    WiFiClientSecure client;
    client.setInsecure();  // Azure uses a well-known CA; set a root cert here if stricter validation is needed
    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(json);

    if (code == 200) {
        String responseBody = http.getString();
        Serial.printf("[wifi] Response: %s\n", responseBody.c_str());
        handleJsonBody(responseBody);
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
