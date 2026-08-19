#include "wifi_manager_helper.h"
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// Constants for NVS
#define WIFI_NAMESPACE "wificonfig"
#define WIFI_CREDENTIALS_KEY "credentials"
#define WIFI_CREDENTIALS_JSON_SIZE 2048

bool updateSavedWiFiPasswordFromWiFiManager(const String& ssid, WiFiManager& wm) {
    Serial.print("Updating saved WiFi credentials from WiFiManager for SSID: ");
    Serial.println(ssid);
    
    // First try to get the actual connected SSID - that's most reliable
    String connectedSSID = WiFi.SSID();
    String realPassword = "";
    
    if (connectedSSID == ssid) {
        // We're connected to this network - try to get the password from WiFiManager
        realPassword = wm.getWiFiPass(true);
        Serial.println("Using WiFiManager's stored password for currently connected network");
    } else {
        // We're not connected to this network, see if WiFiManager has it stored
        String storedSSID = wm.getWiFiSSID(true);
        
        if (storedSSID == ssid) {
            realPassword = wm.getWiFiPass(true);
            Serial.println("Using WiFiManager's stored password for saved network");
        } else {
            Serial.print("WiFiManager SSID (");
            Serial.print(storedSSID);
            Serial.print(") doesn't match requested SSID (");
            Serial.print(ssid);
            Serial.println(")");
            return false;
        }
    }
    
    // Verify that we got a password
    if (realPassword.length() == 0) {
        Serial.println("No password found in WiFiManager");
        return false;
    }
    
    Serial.println("Found valid password in WiFiManager, updating NVS storage");
    
    // Open NVS
    Preferences prefs;
    if (!prefs.begin(WIFI_NAMESPACE, false)) {
        Serial.println("Failed to open NVS for WiFi credentials update");
        return false;
    }
    
    // Retrieve the stored networks
    String networksJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
    prefs.end();
    
    // Parse the JSON
    DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(doc, networksJson);
    
    if (error) {
        Serial.print("Failed to parse WiFi credentials JSON: ");
        Serial.println(error.c_str());
        
        // Create a new JSON array with just this network
        doc.clear();
        JsonArray networks = doc.to<JsonArray>();
        JsonObject newNetwork = networks.createNestedObject();
        newNetwork["ssid"] = ssid;
        newNetwork["password"] = realPassword;
        newNetwork["priority"] = 1;
        
        // Save the new JSON
        String newJson;
        serializeJson(doc, newJson);
        
        if (prefs.begin(WIFI_NAMESPACE, false)) {
            bool success = prefs.putString(WIFI_CREDENTIALS_KEY, newJson);
            prefs.end();
            
            if (success) {
                Serial.println("Created new WiFi credentials JSON with current network");
                return true;
            } else {
                Serial.println("Failed to save new WiFi credentials JSON");
                return false;
            }
        }
        return false;
    }
    
    // Find and update the network with matching SSID
    bool found = false;
    JsonArray networks = doc.as<JsonArray>();
    
    for (JsonObject network : networks) {
        if (network["ssid"] == ssid) {
            network["password"] = realPassword;
            found = true;
            break;
        }
    }
    
    // If network wasn't found, add it
    if (!found) {
        JsonObject newNetwork = networks.createNestedObject();
        newNetwork["ssid"] = ssid;
        newNetwork["password"] = realPassword;
        newNetwork["priority"] = networks.size() > 0 ? networks.size() + 1 : 1; // Add with lowest priority
    }
    
    // Serialize and save back to NVS
    String updatedJson;
    serializeJson(doc, updatedJson);
    
    if (prefs.begin(WIFI_NAMESPACE, false)) {
        bool success = prefs.putString(WIFI_CREDENTIALS_KEY, updatedJson);
        prefs.end();
        
        if (success) {
            Serial.println("Successfully updated WiFi credentials in NVS with real password");
            return true;
        } else {
            Serial.println("Failed to save updated WiFi credentials to NVS");
            return false;
        }
    } else {
        Serial.println("Failed to open NVS for saving WiFi credentials");
        return false;
    }
    
    return false;
}

String getWiFiPasswordFromWiFiManager(const String& ssid, WiFiManager& wm) {
    // First check if this is the currently connected SSID
    String currentSSID = wm.getWiFiSSID(true);
    if (currentSSID == ssid) {
        return wm.getWiFiPass(true);
    }
    
    // Otherwise, we can't directly access other stored passwords from WiFiManager
    // Just return empty string
    return "";
}
