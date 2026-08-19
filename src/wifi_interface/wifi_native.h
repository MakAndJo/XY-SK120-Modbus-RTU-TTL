#ifndef WIFI_NATIVE_H
#define WIFI_NATIVE_H

#include <Arduino.h>

// Try to connect to saved networks from NVS in priority order. Returns true
// when a connection is established, false when all saved networks failed
// (caller should fall back to AP/portal mode).
bool connectToSavedNetworks();

// Initialize WiFi in station mode with sane defaults for this hardware.
void initNativeWifiRadio();

// Reset all saved WiFi credentials.
void resetWiFiSettings();

// Clean exit from AP mode and back to station mode.
bool exitConfigPortal();

#endif // WIFI_NATIVE_H