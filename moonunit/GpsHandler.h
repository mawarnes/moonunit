#pragma once

#include <HardwareSerial.h>
#include <TinyGPS++.h>

extern HardwareSerial gpsSerial;
extern TinyGPSPlus    gps;

bool   isFieldEnabled(const char* name);
double readAnalog4to20(int pin, float rangeMin, float rangeMax);
