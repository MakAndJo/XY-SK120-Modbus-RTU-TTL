#include "wifi_manager_instance.h"

// Define the WiFiManager instance only in this file
WiFiManager wifiManager;

// Function to get a reference to the WiFiManager instance
WiFiManager& getWiFiManager() {
    return wifiManager;
}

// Initialize the WiFiManager instance with any default settings
void initWiFiManagerInstance() {
    // Set any default settings for WiFiManager here
    wifiManager.setDebugOutput(true);
}
