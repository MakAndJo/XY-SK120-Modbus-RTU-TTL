#ifndef WIFI_MANAGER_INSTANCE_H
#define WIFI_MANAGER_INSTANCE_H

#include <WiFiManager.h>

// Declare the WiFiManager instance as external
extern WiFiManager wifiManager;

// Function to get a reference to the WiFiManager instance
WiFiManager& getWiFiManager();

// Initialize the WiFiManager instance with default settings
void initWiFiManagerInstance();

#endif // WIFI_MANAGER_INSTANCE_H
