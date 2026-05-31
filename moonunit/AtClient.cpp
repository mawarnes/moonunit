#include <Arduino.h>
#include "AtClient.h"
#include "config.h"

HardwareSerial SerialAT(1);  // UART1

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
