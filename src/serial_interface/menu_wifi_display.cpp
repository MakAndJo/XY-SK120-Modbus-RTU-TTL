#include "menu_wifi.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include "../wifi_interface/wifi_settings.h"

void scanWiFiNetworks() {
  Serial.println("Scanning for WiFi networks...");
  
  int networksFound = WiFi.scanNetworks();
  
  if (networksFound == 0) {
    Serial.println("No WiFi networks found");
  } else {
    Serial.print(networksFound);
    Serial.println(" WiFi networks found:");
    Serial.println("SSID                             | RSSI | Channel | Encryption");
    Serial.println("----------------------------------|------|---------|----------");
    
    for (int i = 0; i < networksFound; ++i) {
      // Print SSID (padded to 34 characters)
      String ssid = WiFi.SSID(i);
      Serial.print(ssid);
      for (int j = ssid.length(); j < 34; j++) {
        Serial.print(" ");
      }
      
      // Print RSSI
      Serial.print("| ");
      Serial.print(WiFi.RSSI(i));
      Serial.print("  ");
      
      // Print channel
      Serial.print("| ");
      Serial.print(WiFi.channel(i));
      for (int j = String(WiFi.channel(i)).length(); j < 9; j++) {
        Serial.print(" ");
      }
      
      // Print encryption type
      Serial.print("| ");
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN:
          Serial.println("Open");
          break;
        case WIFI_AUTH_WEP:
          Serial.println("WEP");
          break;
        case WIFI_AUTH_WPA_PSK:
          Serial.println("WPA-PSK");
          break;
        case WIFI_AUTH_WPA2_PSK:
          Serial.println("WPA2-PSK");
          break;
        case WIFI_AUTH_WPA_WPA2_PSK:
          Serial.println("WPA/WPA2-PSK");
          break;
        default:
          Serial.println("Unknown");
          break;
      }
      
      // Add a small delay to avoid overflowing the serial output
      delay(10);
    }
  }
  
  // Delete the scan result to free memory
  WiFi.scanDelete();
}

void displayWiFiStatus() {
  Serial.println("\n==== WiFi Status ====");
  
  if (WiFi.getMode() == WIFI_AP) {
    Serial.println("Mode: Access Point");
    Serial.print("AP SSID: ");
    Serial.println(WiFi.softAPSSID());
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("Connected clients: ");
    Serial.println(WiFi.softAPgetStationNum());
  } else {
    Serial.println("Mode: Station");
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Connected to: ");
      Serial.println(WiFi.SSID());
      Serial.print("Signal strength: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      Serial.print("MAC Address: ");
      Serial.println(WiFi.macAddress());
    } else {
      Serial.println("Not connected to any WiFi network");
    }
  }
}

// Function to display saved WiFi networks
void displaySavedWiFiNetworks() {
  Serial.println("\n==== Saved WiFi Networks ====");
  
  Preferences prefs;
  
  // Use the correct namespace and key defined in wifi_settings.h
  if (prefs.begin(WIFI_NAMESPACE, true)) { // Read-only mode
    // Get the JSON string containing all WiFi networks
    String wifiListJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
    
    // Log the raw JSON for debugging
    Serial.println("Raw saved data: " + wifiListJson);
    
    // Parse the JSON
    DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(doc, wifiListJson);
    
    if (error) {
      Serial.println("Error parsing saved WiFi networks: " + String(error.c_str()));
      prefs.end();
      return;
    }
    
    JsonArray networks = doc.as<JsonArray>();
    
    if (networks.size() == 0) {
      Serial.println("No WiFi networks have been saved yet.");
    } else {
      Serial.print(networks.size());
      Serial.println(" WiFi network(s) found:");
      Serial.println("----------------------------------------------");
      Serial.println("Priority | SSID                | Password");
      Serial.println("----------------------------------------------");
      
      // Sort networks by priority
      // Create a structure to hold network info
      struct NetworkInfo {
        String ssid;
        String password;
        int priority;
        bool isPlaceholder;
      };
      
      std::vector<NetworkInfo> sortedNetworks;
      
      for (JsonObject network : networks) {
        NetworkInfo info;
        info.ssid = network["ssid"].as<String>();
        info.password = network["password"].as<String>();
        info.priority = network["priority"] | 999; // Default high number if not set
        
        // Check if this is likely a placeholder password
        info.isPlaceholder = info.password.startsWith("temp_password_");
        
        sortedNetworks.push_back(info);
      }
      
      // Sort by priority (lower number = higher priority)
      std::sort(sortedNetworks.begin(), sortedNetworks.end(), 
        [](const NetworkInfo& a, const NetworkInfo& b) {
          return a.priority < b.priority;
        }
      );
      
      // Display the networks
      for (const auto& network : sortedNetworks) {
        // Format priority with padding
        String priorityStr = String(network.priority);
        while (priorityStr.length() < 8) priorityStr += " ";
        
        // Format SSID with padding
        String ssidStr = network.ssid;
        while (ssidStr.length() < 20) ssidStr += " ";
        
        // Don't show actual password for security
        String passwordStr = "";
        if (network.password.length() > 0) {
          if (network.isPlaceholder) {
            passwordStr = "[PLACEHOLDER - uses ESP32 internal credentials]";
          } else {
            passwordStr = "[Set - " + String(network.password.length()) + " chars]";
          }
        } else {
          passwordStr = "[Not set]";
        }
        
        Serial.println(priorityStr + "| " + ssidStr + "| " + passwordStr);
      }
      
      // Display a note about placeholder passwords
      bool hasPlaceholders = false;
      for (const auto& network : sortedNetworks) {
        if (network.isPlaceholder) {
          hasPlaceholders = true;
          break;
        }
      }
      
      if (hasPlaceholders) {
        Serial.println("\nNOTE: Networks marked as [PLACEHOLDER] will use ESP32's internal WiFi Manager");
        Serial.println("credentials when connecting. These have the HIGHEST connection priority.");
        Serial.println("You don't need to update them unless you want to use these credentials on");
        Serial.println("another device, in which case use 'syncwifi' to set the actual password.");
      }
    }
    
    // Additional info - show currently connected network
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nCurrently connected to: " + WiFi.SSID());
    }
    
    prefs.end();
  } else {
    Serial.println("Failed to access saved network information.");
  }
}
