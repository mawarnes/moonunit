/*
 * Metron View ESP32 Telemetry Unit - Moon Unit
 *
 * Hardware:
 *   AM-036 ESP32 dev board
 *   Neo 6M GPS     : TX->GPIO32, RX->GPIO33, VCC->5V, GND->GND
 *   SIM800L modem  : RX->GPIO26, TX->GPIO27 (plus RST/PWKEY/PWR pins)
 *   4-20mA Ch1     : GPIO34 via 165R shunt resistor to GND
 *   4-20mA Ch2     : GPIO35 via 165R shunt resistor to GND
 *
 * Libraries (Tools -> Manage Libraries):
 *   - TinyGPSPlus by Mikal Hart
 *   - ArduinoJson by Benoit Blanchon
 */

#include "DeviceConfig.h"
#include "AtClient.h"
#include "GpsHandler.h"
#include "Telemetry.h"
#include "config.h"

static unsigned long lastSend = 0;
static bool          netReady = false;

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
    Serial.printf("Unit ID       : %d\n", UNIT_ID);
    Serial.printf("Interval      : %lu ms\n", cfg.intervalMs);
    Serial.printf("Endpoint      : %s\n", cfg.endpoint);
    Serial.printf("Connectivity  : %s\n", cfg.connectivityType);

    // Only power up the modem if GSM is the active transport
    if (strcmp(cfg.connectivityType, "gsm") == 0) {
        SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
        delay(3000);
        digitalWrite(MODEM_RST, LOW);  delay(100);
        digitalWrite(MODEM_RST, HIGH); delay(3000);
    } else {
        // Keep modem powered off
        digitalWrite(MODEM_POWER_ON, LOW);
    }

    if (initConnectivity()) {
        netReady = true;
        sendTelemetry();
        lastSend = millis();
    }
}

void loop() {

    // Temporary GPS debug — add to loop()
    static unsigned long lastGpsPrint = 0;
    if (millis() - lastGpsPrint > 5000) {
        Serial.printf("[gps] Chars=%lu Sentences=%lu Fix=%s Sats=%d\n",
            gps.charsProcessed(),
            gps.sentencesWithFix(),
            gps.location.isValid() ? "YES" : "NO",
            gps.satellites.value());
        lastGpsPrint = millis();
    }

    while (gpsSerial.available())
        gps.encode(gpsSerial.read());

    if (netReady && (millis() - lastSend >= cfg.intervalMs)) {
        sendTelemetry();
        lastSend = millis();
    }

    if (SerialAT.available()) Serial.write(SerialAT.read());
    if (Serial.available())   SerialAT.write(Serial.read());
}
