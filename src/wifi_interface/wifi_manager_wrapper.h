#ifndef WIFI_MANAGER_WRAPPER_H
#define WIFI_MANAGER_WRAPPER_H

#include <Arduino.h>
#include <String>

// Include wifi_settings.h first to get the shared constants
#include "wifi_settings.h"

// Initialize WiFi Manager with AP name
bool initWiFiManager(const char* apName);

// Initialize WiFi Manager with AP name and password
bool initWiFiManager(const char* apName, const char* apPassword);

// Connect to saved networks in priority order
bool connectToSavedNetworks();

// Reset WiFi settings
void resetWiFiSettings();

// Check if WiFi is connected
bool isWiFiConnected();

// Get WiFi SSID
String getWiFiSSID();

// Get WiFi IP address
String getWiFiIP();

// Get WiFi RSSI
int getWiFiRSSI();

// Get WiFi MAC address
String getWiFiMAC();

// Exit the config portal properly
bool exitConfigPortal();

// Process WiFiManager credentials to our storage system
void processWiFiManagerCredentials();

// Sync current WiFi connection to our storage
void syncCurrentWiFiToStorage();

// WiFiManager save credentials callback
void saveWifiCallback();

// Legacy functions that now call the NVS versions
bool saveWiFiCredentials(const String& ssid, const String& password);
String loadWiFiCredentials();

#endif // WIFI_MANAGER_WRAPPER_H
