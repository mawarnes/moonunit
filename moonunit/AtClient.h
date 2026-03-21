#pragma once

#include <HardwareSerial.h>

extern HardwareSerial SerialAT;

void atCmd(const String& cmd, int timeout);
String atCmdCapture(const String& cmd, int timeout);
void waitForNetwork();
bool setupInternet();
void reconnectGPRS();
