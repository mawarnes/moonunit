#pragma once

// GPS data populated by pollGnss() via AT+CGNSSINFO from the A7670E modem
struct GnssData {
    bool   valid;
    double latitude;
    double longitude;
    double altitude;    // metres
    double speed;       // km/h
    double course;      // degrees
    int    satellites;
    double hdop;
};

extern GnssData gnss;

void   initGnss();                                         // power on GNSS, call once after modem boot
bool   pollGnss();                                         // query modem, returns true if fix valid
bool   isFieldEnabled(const char* name);
double readAnalog4to20(int pin, float rangeMin, float rangeMax);
