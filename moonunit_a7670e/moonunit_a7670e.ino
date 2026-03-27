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

static unsigned long lastSend    = 0;
static unsigned long lastGpsPoll = 0;
static bool          netReady    = false;

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

    Serial.println("=== Metron View Telemetry Unit (A7670E) ===");
    Serial.printf("Unit ID      : %d\n",  UNIT_ID);
    Serial.printf("Interval     : %lu ms\n", cfg.intervalMs);
    Serial.printf("Endpoint     : %s\n",  cfg.endpoint);
    Serial.printf("Connectivity : %s\n",  cfg.connectivityType);

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
    // Poll GNSS every 5 s
    if (millis() - lastGpsPoll > 5000) {
        pollGnss();
        Serial.printf("[gnss] Fix=%s Sats=%d Lat=%.6f Lon=%.6f\n",
            gnss.valid ? "YES" : "NO",
            gnss.satellites, gnss.latitude, gnss.longitude);
        lastGpsPoll = millis();
    }

    if (netReady && (millis() - lastSend >= cfg.intervalMs)) {
        sendTelemetry();
        lastSend = millis();
    }

    if (SerialAT.available()) Serial.write(SerialAT.read());
    if (Serial.available())   SerialAT.write(Serial.read());
}
