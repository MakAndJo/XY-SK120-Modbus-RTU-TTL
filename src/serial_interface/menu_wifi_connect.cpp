#include "menu_wifi.h"
#include <WiFi.h>

bool exitAPMode() {
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    // Stop the AP
    bool success = WiFi.softAPdisconnect(true);
    // Set to station mode
    WiFi.mode(WIFI_STA);
    // Ensure any ongoing scans are stopped
    WiFi.scanDelete();
    delay(100);
    return success;
  }
  // Already in station mode or off
  WiFi.mode(WIFI_STA);
  return false;
}

bool connectToWiFi(const String& ssid, const String& password) {
  // Disconnect if already connected
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect();
    delay(100);
  }
  
  // Set to station mode
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // Connect to WiFi network
  WiFi.begin(ssid.c_str(), password.c_str());
  
  // Wait up to 20 seconds for connection
  Serial.print("Connecting to WiFi");
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 40) {
    delay(500);
    Serial.print(".");
    attempt++;
  }
  Serial.println();
  
  // Return true if connected
  return WiFi.status() == WL_CONNECTED;
}

bool setupWiFiAP(const String& ssid, const String& password) {
  // Disconnect if in station mode
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect();
    delay(100);
  }
  
  // Set to AP mode
  WiFi.mode(WIFI_AP);
  delay(100);
  
  // Configure and start the AP
  return WiFi.softAP(ssid.c_str(), password.c_str());
}
