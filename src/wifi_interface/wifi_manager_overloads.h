#ifndef WIFI_MANAGER_OVERLOADS_H
#define WIFI_MANAGER_OVERLOADS_H

#include <Arduino.h>

// Simplified versions of the helper functions that use the global wifiManager

/**
 * Updates the password for the given SSID in NVS storage with the actual password from WiFiManager
 * Uses the global wifiManager instance
 * 
 * @param ssid The SSID to update
 * @return true if updated successfully, false otherwise
 */
bool updateSavedWiFiPasswordFromWiFiManager(const String& ssid);

/**
 * Attempts to retrieve a password for the given SSID from WiFiManager
 * Uses the global wifiManager instance
 * 
 * @param ssid The SSID to get the password for
 * @return The password, or empty string if not found/available
 */
String getWiFiPasswordFromWiFiManager(const String& ssid);

#endif // WIFI_MANAGER_OVERLOADS_H
