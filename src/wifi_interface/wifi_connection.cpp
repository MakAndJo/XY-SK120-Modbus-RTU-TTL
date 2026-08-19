#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "wifi_manager_helper.h"
#include "wifi_manager_instance.h"

// External reference to global WiFiManager instance
extern WiFiManager wifiManager;

// Define constants for NVS
#define WIFI_NAMESPACE "wificonfig"
#define WIFI_CREDENTIALS_KEY "credentials"
#define WIFI_CREDENTIALS_JSON_SIZE 2048

// Helper function to directly check and update placeholder passwords
bool updatePlaceholderPassword(const String& ssid) {
    Serial.println("Checking for placeholder password in NVS for: " + ssid);
    
    // Get current networks from NVS
    Preferences prefs;
    if (!prefs.begin(WIFI_NAMESPACE, true)) {
        Serial.println("Failed to open NVS for reading WiFi credentials");
        return false;
    }
    
    String networksJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
    prefs.end();
    
    // Parse the JSON
    DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(doc, networksJson);
    
    if (error) {
        Serial.println("Failed to parse WiFi networks JSON");
        return false;
    }
    
    JsonArray networks = doc.as<JsonArray>();
    bool found = false;
    bool isPlaceholder = false;
    
    // Find the network and check if password is a placeholder
    for (JsonObject network : networks) {
        if (network["ssid"] == ssid) {
            found = true;
            String storedPassword = network["password"].as<String>();
            
            // Check if this is a placeholder password
            if (storedPassword.startsWith("temp_password_") || 
                storedPassword.startsWith("placeholder_") ||
                storedPassword == "temp_password" ||
                storedPassword.length() < 1) {
                
                isPlaceholder = true;
                
                // Try to get the real password from WiFiManager
                Serial.println("Found placeholder password for " + ssid + ", checking WiFiManager");
                String wifiManagerPassword = wifiManager.getWiFiPass(true);
                String wifiManagerSSID = wifiManager.getWiFiSSID(true);
                
                if (wifiManagerSSID == ssid && wifiManagerPassword.length() > 0) {
                    // Update with WiFiManager password
                    Serial.println("Found valid password in WiFiManager, updating NVS");
                    network["password"] = wifiManagerPassword;
                }
            }
            break;
        }
    }
    
    // If we found a placeholder and potentially updated it, save back to NVS
    if (found && isPlaceholder) {
        String updatedJson;
        serializeJson(doc, updatedJson);
        
        if (prefs.begin(WIFI_NAMESPACE, false)) {
            bool success = prefs.putString(WIFI_CREDENTIALS_KEY, updatedJson);
            prefs.end();
            
            if (success) {
                Serial.println("Successfully updated placeholder password in NVS");
                return true;
            } else {
                Serial.println("Failed to save updated WiFi credentials to NVS");
            }
        } else {
            Serial.println("Failed to open NVS for writing");
        }
    } else if (!found) {
        Serial.println("Network not found in NVS: " + ssid);
    } else {
        Serial.println("Network already has a valid password in NVS");
    }
    
    return false;
}

// Connect to a saved WiFi network with better placeholder handling
bool connectToSavedWifiNetwork(JsonObject network) {
    String ssid = network["ssid"].as<String>();
    String password = network["password"].as<String>();
    
    // Immediately check if this is a placeholder password and update if needed
    bool isPlaceholder = password.startsWith("temp_password_") || 
                        password.startsWith("placeholder_") ||
                        password == "temp_password" ||
                        password.length() < 1;
    
    if (isPlaceholder) {
        Serial.print("Found placeholder password for: ");
        Serial.println(ssid);
        
        // First try to update it directly using the helper function
        if (updatePlaceholderPassword(ssid)) {
            // If we updated it, reload the password
            Preferences prefs;
            if (prefs.begin(WIFI_NAMESPACE, true)) {
                String updatedJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
                prefs.end();
                
                DynamicJsonDocument updatedDoc(WIFI_CREDENTIALS_JSON_SIZE);
                if (deserializeJson(updatedDoc, updatedJson) == DeserializationError::Ok) {
                    JsonArray updatedNetworks = updatedDoc.as<JsonArray>();
                    
                    for (JsonObject updatedNetwork : updatedNetworks) {
                        if (updatedNetwork["ssid"] == ssid) {
                            password = updatedNetwork["password"].as<String>();
                            
                            if (!password.startsWith("temp_password_") && 
                                !password.startsWith("placeholder_") &&
                                password != "temp_password" &&
                                password.length() > 0) {
                                
                                Serial.println("Using updated password from NVS");
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        // If we still have a placeholder, try a direct attempt using WiFiManager
        if (password.startsWith("temp_password_") || 
            password.startsWith("placeholder_") ||
            password == "temp_password" ||
            password.length() < 1) {
            
            Serial.println("Still have placeholder password, trying WiFiManager's internal credentials");
            
            // Let WiFiManager try to connect using its stored credentials
            wifiManager.setConfigPortalTimeout(0);  // Don't launch the portal
            bool connected = wifiManager.autoConnect();
            
            if (connected && WiFi.SSID() == ssid) {
                Serial.println("Successfully connected using WiFiManager's credentials");
                
                // Try to update our NVS with the correct password
                updateSavedWiFiPasswordFromWiFiManager(ssid, wifiManager);
                return true;
            }
            
            // If we're here, WiFiManager didn't connect to this network
            Serial.println("WiFiManager couldn't connect to network");
            return false;
        }
    }
    
    // Normal connection with the password we have
    Serial.print("Attempting to connect to network: ");
    Serial.print(ssid);
    Serial.print(" with password length: ");
    Serial.println(password.length());
    
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // Wait for connection with timeout
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected successfully!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        
        // If this was a connection with a placeholder that worked, update NVS
        if (isPlaceholder) {
            updateSavedWiFiPasswordFromWiFiManager(ssid, wifiManager);
        }
        
        return true;
    } else {
        Serial.println("\nFailed to connect to " + ssid);
        return false;
    }
}