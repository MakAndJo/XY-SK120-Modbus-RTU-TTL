#ifndef WIFI_SETTINGS_H
#define WIFI_SETTINGS_H

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <String>
#include <vector>
#include <algorithm>

// Constants
#define WIFI_NAMESPACE "wificonfig"
#define WIFI_CREDENTIALS_KEY "credentials" 
#define WIFI_CREDENTIALS_JSON_SIZE 2048
#define MAX_SAVED_NETWORKS 10

// Function declarations
String loadWiFiCredentialsFromNVS();
bool saveWiFiCredentialsToNVS(const String& json);
String sanitizeString(const String& input);
bool repairWiFiCredentials();

// Function to save WiFi credentials to NVS
bool saveWiFiCredentialsToNVS(const String& ssid, const String& password, int priority = -1);

// Function to remove a WiFi credential by index
bool removeWiFiCredentialByIndex(int index);

// Function to update priority of a WiFi network
bool updateWiFiNetworkPriority(int index, int newPriority);

// Function to reset WiFi settings
bool resetWiFi();

// Function to get WiFi status as a JSON string
String getWifiStatus();

// Forward declaration of function in wifi_manager_wrapper.cpp
bool connectToSavedNetworks();

#endif // WIFI_SETTINGS_H
