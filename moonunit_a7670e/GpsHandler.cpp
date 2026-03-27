#include <Arduino.h>
#include "GpsHandler.h"
#include "AtClient.h"
#include "DeviceConfig.h"

GnssData gnss = {};

void initGnss() {
    // Flush any pending boot URCs (ATREADY, CPIN, SMS DONE) before issuing GNSS commands.
    // These can arrive late after fast LTE re-registration following a sleep cycle.
    delay(2000);
    while (SerialAT.available()) SerialAT.read();

    atCmd("AT+CGNSSPWR=1", 3000);

    // Wait for GNSS subsystem to start and flush any startup URCs it emits
    delay(3000);
    while (SerialAT.available()) SerialAT.read();

    Serial.println("[gnss] GNSS powered on");
}

bool pollGnss() {
    String resp = atCmdCapture("AT+CGNSSINFO", 2000);

    int idx = resp.indexOf("+CGNSSINFO:");
    if (idx < 0) { gnss.valid = false; return false; }

    // A7670E field layout (decimal degrees, not NMEA):
    // 0:mode, 1:GPS_sats, 2:GLO_sats, 3:BEI_sats, 4:GAL_sats,
    // 5:lat, 6:N/S, 7:lon, 8:E/W, 9:date, 10:time,
    // 11:alt, 12:speed, 13:course, 14:PDOP, 15:HDOP, 16:VDOP
    String line = resp.substring(idx + 12);
    line.trim();

    String fields[17];
    int count = 0, start = 0;
    for (int i = 0; i <= (int)line.length() && count < 17; i++) {
        if (i == (int)line.length() || line[i] == ',') {
            fields[count++] = line.substring(start, i);
            start = i + 1;
        }
    }

    // lat field (index 5) is empty when there is no fix
    if (count < 14 || fields[5].length() == 0) {
        gnss.valid = false;
        return false;
    }

    // Coordinates are already decimal degrees
    double lat = fields[5].toDouble();
    if (fields[6] == "S") lat = -lat;
    double lon = fields[7].toDouble();
    if (fields[8] == "W") lon = -lon;

    gnss.valid      = true;
    gnss.latitude   = lat;
    gnss.longitude  = lon;
    gnss.altitude   = fields[11].toDouble();
    gnss.speed      = fields[12].toDouble();
    gnss.course     = fields[13].toDouble();
    gnss.satellites = fields[1].toInt() + fields[2].toInt() + fields[3].toInt() + fields[4].toInt();
    gnss.hdop       = (count >= 16) ? fields[15].toDouble() : 0.0;

    return true;
}

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
