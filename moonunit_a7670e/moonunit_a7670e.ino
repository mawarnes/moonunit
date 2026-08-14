/*
 * Metron View ESP32 Telemetry Unit - Moon Unit (A7670E)
 *
 * Hardware:
 *   LILYGO T-SIM A7670E SA R2 (ESP32 + 4G LTE CAT1 modem)
 *   A7670E modem   : RX->GPIO26 (MODEM_TX), TX->GPIO27 (MODEM_RX)
 *                    PWRKEY->GPIO4, POWER_ON->GPIO12, RST->GPIO5
 *   4-20mA Ch1     : GPIO34 via 165R shunt resistor to GND
 *   4-20mA Ch2     : GPIO35 via 165R shunt resistor to GND
 *
 * Board (Arduino IDE): ESP32 Dev Module
 *
 * Libraries (Tools -> Manage Libraries):
 *   - ArduinoJson by Benoit Blanchon
 */

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "DeviceConfig.h"
#include "AtClient.h"
#include "GpsHandler.h"
#include "Telemetry.h"
#include "config.h"

static unsigned long lastSend = 0;
static bool          netReady = false;

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // LTE registration draws surge current — disable brownout
    Serial.begin(115200);

    pinMode(MODEM_PWKEY,    OUTPUT);
    pinMode(MODEM_RST,      OUTPUT);
    pinMode(MODEM_POWER_ON, OUTPUT);

    digitalWrite(MODEM_POWER_ON, HIGH);
    digitalWrite(MODEM_PWKEY,    LOW);
    digitalWrite(MODEM_RST,      HIGH);

    loadConfig();

    Serial.println("\n========================================");
    Serial.println(" Metron View Telemetry Unit (A7670E)");
    Serial.println("========================================");
    Serial.printf("Unit ID      : %d\n",    UNIT_ID);
    Serial.printf("Device ID    : %s\n",    DEVICE_ID);
    Serial.printf("Session      : %s\n",    DEVICE_SESSION);
    Serial.println("--- Network ---");
    Serial.printf("Connectivity : %s\n",    cfg.connectivityType);
    if (strcmp(cfg.connectivityType, "gsm") == 0)
        Serial.printf("APN          : %s\n", APN);
    if (strcmp(cfg.connectivityType, "wifi") == 0)
        Serial.printf("WiFi SSID    : %s\n", cfg.wifiSsid);
    Serial.println("--- Telemetry ---");
    Serial.printf("Endpoint     : %s\n",    cfg.endpoint);
    Serial.printf("Interval     : %lu ms\n", cfg.intervalMs);
    Serial.println("--- Enabled Fields ---");
    for (int i = 0; i < CAP_COUNT; i++)
        Serial.printf("  %-20s %s\n", CAPABILITIES[i], cfg.fields[i] ? "ON" : "off");
    Serial.println("--- Analog Channels ---");
    for (int i = 0; i < 2; i++) {
        AnalogChannel& ch = cfg.analog[i];
        if (ch.enabled)
            Serial.printf("  Ch%d: %-16s %.1f-%.1f (pin %d)\n",
                i + 1, ch.name, ch.rangeMin, ch.rangeMax, ch.pin);
        else
            Serial.printf("  Ch%d: disabled\n", i + 1);
    }
    Serial.println("========================================");

    if (strcmp(cfg.connectivityType, "gsm") == 0) {
        SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
        // Wait for any auto-start boot to COMPLETE before sending PWRKEY.
        // After a USB replug the modem auto-starts; a 1.5 s PWRKEY mid-boot disrupts it.
        // After a code-deploy reset the modem is briefly off and needs PWRKEY to start.
        // If we wait until after any auto-boot has finished, the 1.5 s PWRKEY is either:
        //   - ignored (modem already running — needs >3 s to power off), or
        //   - starts the modem (it never auto-started after the brief power cut)
        delay(12000);
        digitalWrite(MODEM_PWKEY, HIGH); delay(1500);
        digitalWrite(MODEM_PWKEY, LOW);
        delay(5000); // extra wait if modem needed booting from PWRKEY
    } else {
        digitalWrite(MODEM_POWER_ON, LOW);
    }

    if (initConnectivity()) {
        netReady = true;
        initGnss();  // modem fully up by this point — safe to enable GNSS
        sendTelemetry();
        lastSend = millis();
    }
}

void loop() {
    if (netReady && (millis() - lastSend >= cfg.intervalMs)) {
        pollGnss();
        Serial.printf("[gnss] Fix=%s Sats=%d Lat=%.6f Lon=%.6f\n",
            gnss.valid ? "YES" : "NO",
            gnss.satellites, gnss.latitude, gnss.longitude);
        sendTelemetry();
        lastSend = millis();
    }

    if (MODEM_TRAFFIC_DEBUG) {
        if (SerialAT.available()) Serial.write(SerialAT.read());
        if (Serial.available())   SerialAT.write(Serial.read());
    }
}
