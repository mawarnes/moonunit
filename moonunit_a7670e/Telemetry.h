#pragma once

extern int msgCounter;
extern int failCount;

void handleResponse(const String& raw);
void sendTelemetry();
bool initConnectivity();
void disconnectWifi();
