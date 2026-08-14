#include <Arduino.h>
#include "AtClient.h"
#include "config.h"

HardwareSerial SerialAT(1);  // UART1

void atCmd(const String& cmd, int timeout) {
    if (MODEM_TRAFFIC_DEBUG) {
        Serial.print(">> ");
        Serial.println(cmd);
    }
    SerialAT.println(cmd);
    unsigned long t = millis();
    while (millis() - t < (unsigned long)timeout) {
        if (SerialAT.available()) {
            char c = (char)SerialAT.read();
            if (MODEM_TRAFFIC_DEBUG) Serial.write(c);
        }
    }
    if (MODEM_TRAFFIC_DEBUG) Serial.println();
}

String atCmdCapture(const String& cmd, int timeout) {
    if (MODEM_TRAFFIC_DEBUG) {
        Serial.print(">> ");
        Serial.println(cmd);
    }
    SerialAT.println(cmd);
    String resp;
    unsigned long t = millis();
    while (millis() - t < (unsigned long)timeout) {
        if (SerialAT.available()) {
            char c = SerialAT.read();
            resp += c;
            if (MODEM_TRAFFIC_DEBUG) Serial.write(c);
        }
    }
    if (MODEM_TRAFFIC_DEBUG) Serial.println();
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
        r.trim();
        Serial.printf("[gsm] CREG response: %s\n", r.c_str());
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
    // A7670E: configure PDP context — HTTP service manages its own bearer activation
    atCmd("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 3000);
    return true;
}

void reconnectGPRS() {
    // Tear down any open HTTP session, re-apply APN, and let the next send reinitialise
    atCmd("AT+HTTPTERM", 3000);
    atCmd("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 3000);
    Serial.println("[gsm] Network reset");
}
