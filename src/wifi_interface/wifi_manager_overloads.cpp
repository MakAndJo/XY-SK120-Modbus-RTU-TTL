#include "wifi_manager_overloads.h"
#include "wifi_manager_helper.h"
#include "wifi_manager_instance.h"

// External reference to the WiFiManager instance
extern WiFiManager wifiManager;

// Implementation of simplified helper functions that use the global WiFiManager instance

bool updateSavedWiFiPasswordFromWiFiManager(const String& ssid) {
    return updateSavedWiFiPasswordFromWiFiManager(ssid, wifiManager);
}

String getWiFiPasswordFromWiFiManager(const String& ssid) {
    return getWiFiPasswordFromWiFiManager(ssid, wifiManager);
}
