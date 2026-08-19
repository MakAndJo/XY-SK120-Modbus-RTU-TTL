#include "menu_wifi.h"
#include "serial_core.h"
#include "serial_interface.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>  // Add this include for ArduinoJson functionality
#include "../wifi_interface/wifi_settings.h"

// Function declarations for functions defined in this file
void handleSaveCurrentWiFi();
void displayIPInfo();
void syncCurrentWiFi();
void handleAddWifi(const String& input, String ssid, String password, int priority);

// External declarations for functions in other files
extern bool extractQuotedParameters(const String& input, String& command, String& param1, String& param2);
extern bool exitConfigPortal();
extern String sanitizeString(const String& input);

// Helper function to handle saving current WiFi
void handleSaveCurrentWiFi() {
  // Check if connected to WiFi
  if (WiFi.status() == WL_CONNECTED) {
    String currentSSID = WiFi.SSID();
    
    Serial.print("Currently connected to: ");
    Serial.println(currentSSID);
    Serial.println("Saving this network to saved networks...");
    
    // Ask for password
    Serial.println("Current password cannot be retrieved from the ESP32.");
    Serial.println("Please enter the password for this network: ");
    
    // Wait for user to input password
    String userPassword = "";
    unsigned long startTime = millis();
    while ((millis() - startTime) < 30000) { // 30 second timeout
      if (Serial.available()) {
        userPassword = Serial.readStringUntil('\n');
        userPassword.trim();
        break;
      }
      delay(100);
    }
    
    if (userPassword.length() == 0) {
      Serial.println("No password entered or timeout occurred. Aborting save operation.");
      return;
    }
    
    // Save to NVS with highest priority (1)
    if (saveWiFiCredentialsToNVS(currentSSID, userPassword, 1)) {
      Serial.println("WiFi credentials saved successfully!");
      Serial.println("Network will be used with priority 1 (highest) for future connections.");
    } else {
      Serial.println("Failed to save WiFi credentials.");
    }
  } else {
    Serial.println("Not connected to any WiFi network. Cannot save.");
  }
}

// Display IP information
void displayIPInfo() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Subnet Mask: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
  } else if (WiFi.getMode() == WIFI_AP) {
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Not connected to WiFi or AP mode not active");
  }
}

// Enhanced syncCurrentWiFi function
void syncCurrentWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    String currentSSID = WiFi.SSID();
    
    Serial.print("Force syncing current WiFi (");
    Serial.print(currentSSID);
    Serial.println(") to saved networks...");
    
    // Get password from user
    Serial.println("Please enter the password for this network:");
    String password = "";
    unsigned long startTime = millis();
    while ((millis() - startTime) < 30000) { // 30 second timeout
      if (Serial.available()) {
        password = Serial.readStringUntil('\n');
        password.trim();
        break;
      }
      delay(100);
    }
    
    if (password.isEmpty()) {
      Serial.println("No password entered, aborting sync.");
      return;
    }
    
    // First, check if we have valid JSON in WiFi credentials
    Preferences prefs;
    bool needsReset = false;
    
    if (prefs.begin(WIFI_NAMESPACE, true)) {
      String existingJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
      prefs.end();
      
      // Test if it's valid JSON
      DynamicJsonDocument testDoc(WIFI_CREDENTIALS_JSON_SIZE);
      DeserializationError error = deserializeJson(testDoc, existingJson);
      
      if (error) {
        Serial.println("Found invalid JSON in saved WiFi credentials. Resetting.");
        needsReset = true;
      }
    }
    
    // If JSON is invalid, reset it
    if (needsReset) {
      if (prefs.begin(WIFI_NAMESPACE, false)) {
        prefs.putString(WIFI_CREDENTIALS_KEY, "[]");
        prefs.end();
        Serial.println("Reset WiFi credentials storage to empty array.");
      }
    }
    
    // Now explicitly create the proper JSON structure and save
    String wifiListJson = loadWiFiCredentialsFromNVS();
    DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
    
    if (wifiListJson == "[]") {
      // Create a new array
      JsonArray networks = doc.to<JsonArray>();
      
      // Add the network with priority 1
      JsonObject network = networks.createNestedObject();
      network["ssid"] = currentSSID;
      network["password"] = password;
      network["priority"] = 1;
    } else {
      // Parse existing JSON
      DeserializationError error = deserializeJson(doc, wifiListJson);
      if (!error) {
        // Get networks array
        JsonArray networks = doc.as<JsonArray>();
        
        // Check if this network already exists
        bool networkExists = false;
        for (JsonObject network : networks) {
          String savedSSID = network["ssid"].as<String>();
          if (savedSSID == currentSSID) {
            // Update the password and priority
            network["password"] = password;
            network["priority"] = 1;
            networkExists = true;
            break;
          }
        }
        
        // If network doesn't exist, add it with priority 1
        if (!networkExists) {
          // First increment all priorities
          for (JsonObject network : networks) {
            int priority = network["priority"];
            network["priority"] = priority + 1;
          }
          
          // Add the new network
          JsonObject network = networks.createNestedObject();
          network["ssid"] = currentSSID;
          network["password"] = password;
          network["priority"] = 1;
        }
      }
    }
    
    // Serialize and save
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    
    // Log the JSON for debugging
    Serial.print("New JSON to save: ");
    Serial.println(jsonOutput);
    
    // Save to NVS
    if (prefs.begin(WIFI_NAMESPACE, false)) {
      bool success = prefs.putString(WIFI_CREDENTIALS_KEY, jsonOutput);
      prefs.end();
      
      if (success) {
        Serial.println("Successfully saved WiFi to NVS with proper format!");
      } else {
        Serial.println("Failed to save WiFi credentials. Check NVS storage.");
      }
    }
    
    // Verify save
    if (prefs.begin(WIFI_NAMESPACE, true)) {
      String jsonAfter = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
      prefs.end();
      
      Serial.println("Current saved WiFi data: " + jsonAfter);
    }
  } else {
    Serial.println("Not connected to WiFi. Cannot sync.");
  }
}

// New command to add WiFi without connecting
void handleAddWifi(const String& input, String ssid, String password, int priority) {
  // Sanitize inputs
  String cleanSsid = sanitizeString(ssid);
  String cleanPassword = sanitizeString(password);
  
  // Alert if there was sanitization
  if (ssid != cleanSsid || password != cleanPassword) {
    Serial.println("Input sanitized: Control characters removed from WiFi credentials");
    
    // Update to use clean versions
    ssid = cleanSsid;
    password = cleanPassword;
  }
  
  Serial.print("Adding WiFi network to saved list: ");
  Serial.print(ssid);
  Serial.print(" with priority: ");
  Serial.println(priority);
  
  // Get saved networks JSON
  Preferences prefs;
  String wifiListJson;
  
  if (prefs.begin(WIFI_NAMESPACE, true)) { // Read-only mode first
    wifiListJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
    prefs.end();
  } else {
    Serial.println("Failed to access saved WiFi information.");
    return;
  }
  
  // Parse existing JSON
  DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
  DeserializationError error = deserializeJson(doc, wifiListJson);
  
  if (error) {
    Serial.println("Error parsing saved WiFi networks. Creating new list.");
    doc.clear();
    doc = JsonArray(); // Ensure it's initialized as an array
  }
  
  // Check if this network already exists
  JsonArray networks = doc.as<JsonArray>();
  bool networkExists = false;
  
  for (JsonObject network : networks) {
    String savedSSID = network["ssid"].as<String>();
    if (savedSSID == ssid) {
      // Update existing network
      network["password"] = password;
      network["priority"] = priority;
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
  serializeJson(doc, updatedJson);
  
  // Debug output
  Serial.print("WiFi credentials JSON size: ");
  Serial.println(updatedJson.length());
  Serial.print("WiFi credentials JSON content: ");
  Serial.println(updatedJson);
  
  // Save back to NVS
  if (prefs.begin(WIFI_NAMESPACE, false)) { // Write mode
    bool success = prefs.putString(WIFI_CREDENTIALS_KEY, updatedJson);
    prefs.end();
    
    if (success) {
      Serial.println("WiFi credentials saved successfully!");
      Serial.println("Network will be automatically tried when in range.");
    } else {
      Serial.println("Failed to save WiFi credentials.");
    }
  } else {
    Serial.println("Failed to access WiFi storage for writing.");
  }
}
