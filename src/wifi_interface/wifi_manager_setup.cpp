#include "wifi_manager_helper.h"
#include "wifi_manager_instance.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiManager.h>

// Define necessary constants if not already defined
#define WIFI_NAMESPACE "wificonfig"
#define WIFI_CREDENTIALS_KEY "credentials"
#define WIFI_CREDENTIALS_JSON_SIZE 2048

// Reference the global wifiManager instance
extern WiFiManager wifiManager;

// Function declaration for saveWifiCredentialsToNVS
void saveWifiCredentialsToNVS(const String& ssid, const String& password, int priority = 1);

// Add helper function to save WiFi credentials directly to NVS
void saveWifiCredentialsToNVS(const String& ssid, const String& password, int priority) {
    Preferences prefs;
    
    // Open preferences in read mode first to get existing networks
    if (!prefs.begin(WIFI_NAMESPACE, true)) {
        Serial.println("Failed to open NVS for reading WiFi credentials");
        return;
    }
    
    String wifiListJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
    prefs.end();
    
    // Parse existing JSON
    DynamicJsonDocument wifiDoc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(wifiDoc, wifiListJson);
    
    if (error) {
        Serial.println("Error parsing saved WiFi networks. Creating new list.");
        wifiDoc.clear();
        wifiDoc = JsonArray();
    }
    
    // Check if this network already exists
    JsonArray networks = wifiDoc.as<JsonArray>();
    bool networkExists = false;
    
    for (JsonObject network : networks) {
        if (network["ssid"] == ssid) {
            // Update existing network
            network["password"] = password;
            if (priority > 0) {
                network["priority"] = priority;
            }
            networkExists = true;
            break;
        }
    }
    
    // If network doesn't exist, add it
    if (!networkExists) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = ssid;
        network["password"] = password;
        network["priority"] = priority;
    }
    
    // Serialize back to string
    String updatedJson;
    serializeJson(wifiDoc, updatedJson);
    
    // Debug output (hide actual password in logs)
    Serial.print("WiFi credentials JSON size: ");
    Serial.println(updatedJson.length());
    
    // Save back to NVS
    if (prefs.begin(WIFI_NAMESPACE, false)) { // Write mode
        bool success = prefs.putString(WIFI_CREDENTIALS_KEY, updatedJson);
        prefs.end();
        
        if (success) {
            Serial.println("Successfully saved WiFi credentials to NVS");
        } else {
            Serial.println("Failed to save WiFi credentials to NVS");
        }
    } else {
        Serial.println("Failed to open NVS for writing WiFi credentials");
    }
}

void setupWifiManager() {
    // Add callbacks for when WiFi is saved via the portal
    wifiManager.setSaveConfigCallback([]() {
        Serial.println("WiFiManager: Credentials Saved");
        
        // Wait a short moment to ensure the connection is established
        delay(500);
        
        // Get the SSID that was just saved
        String savedSSID = wifiManager.getWiFiSSID(true);
        String savedPassword = wifiManager.getWiFiPass(true);
        
        Serial.print("WiFiManager saved network: ");
        Serial.println(savedSSID);
        
        if (savedSSID.length() > 0) {
            Serial.println("Attempting to synchronize WiFiManager password to NVS...");
            
            if (updateSavedWiFiPasswordFromWiFiManager(savedSSID, wifiManager)) {
                Serial.println("Successfully synchronized WiFiManager password to NVS storage");
            } else {
                Serial.println("Failed to synchronize WiFiManager password to NVS storage");
                
                // Fallback: Since updateSavedWiFiPasswordFromWiFiManager failed,
                // manually save the network with the password we got
                if (savedPassword.length() > 0) {
                    Serial.println("Using manual fallback to save credentials...");
                    saveWifiCredentialsToNVS(savedSSID, savedPassword);
                }
            }
        }
    });
    
    // Also add an event callback for when connection is successful
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            Serial.println("WiFi connected with IP: " + WiFi.localIP().toString());
            
            // Get current network info
            String connectedSSID = WiFi.SSID();
            
            // Try to synchronize the password for this network
            Serial.print("Synchronizing password for connected network: ");
            Serial.println(connectedSSID);
            
            updateSavedWiFiPasswordFromWiFiManager(connectedSSID, wifiManager);
        }
    });
}