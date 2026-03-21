#include <Arduino.h>
#include "GpsHandler.h"
#include "DeviceConfig.h"

HardwareSerial gpsSerial(2);  // UART2
TinyGPSPlus    gps;

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
