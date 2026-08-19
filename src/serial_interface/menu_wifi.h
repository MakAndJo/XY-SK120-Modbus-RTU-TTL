#pragma once

#include <Arduino.h>
#include "XY-SKxxx.h"
#include "../wifi_interface/wifi_settings.h"

// Display the WiFi settings menu
void displayWiFiMenu();

// Handle WiFi menu commands
void handleWiFiMenu(const String& input, XY_SKxxx* ps);

// Scan and display available WiFi networks
void scanWiFiNetworks();

// Display current WiFi status and settings
void displayWiFiStatus();

// Connect to a WiFi network
bool connectToWiFi(const String& ssid, const String& password);

// Set up WiFi in AP mode
bool setupWiFiAP(const String& ssid, const String& password);

// Display saved WiFi networks
void displaySavedWiFiNetworks();

// Helper function to extract quoted parameters from a command
bool extractQuotedParameters(const String& input, String& param1, String& param2, String& remaining);

// Exit AP mode and return to station mode
bool exitAPMode();

// Helper function to handle saving current WiFi
void handleSaveCurrentWiFi();

// Display IP information
void displayIPInfo();

// Helper function to manually sync current WiFi to NVS storage
void syncCurrentWiFi();

// Dedicated function to handle adding WiFi networks
void handleAddWifi(const String& input, String ssid, String password, int priority);

// External declaration of the function from menu_main.cpp
extern void displayMainMenu();
