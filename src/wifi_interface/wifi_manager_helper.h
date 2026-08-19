#ifndef WIFI_MANAGER_HELPER_H
#define WIFI_MANAGER_HELPER_H

#include <Arduino.h>

// Forward declare WiFiManager class to avoid header inclusion conflicts
class WiFiManager;

/**
 * Updates the password for the given SSID in NVS storage with the actual password from WiFiManager
 * 
 * @param ssid The SSID to update
 * @param wm Reference to the WiFiManager instance
 * @return true if updated successfully, false otherwise
 */
bool updateSavedWiFiPasswordFromWiFiManager(const String& ssid, WiFiManager& wm);

/**
 * Attempts to retrieve a password for the given SSID from WiFiManager
 * Note: WiFiManager can only directly provide the password for the currently connected network
 * 
 * @param ssid The SSID to get the password for
 * @param wm Reference to the WiFiManager instance
 * @return The password, or empty string if not found/available
 */
String getWiFiPasswordFromWiFiManager(const String& ssid, WiFiManager& wm);

#endif // WIFI_MANAGER_HELPER_H
