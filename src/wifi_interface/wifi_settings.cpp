#include "wifi_settings.h"
#include "wifi_manager_wrapper.h" // Include wifi_manager_wrapper for resetWiFiSettings
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>

// Function to get WiFi status as a JSON string
String getWifiStatus() {
  DynamicJsonDocument doc(512);
  doc["status"] = isWiFiConnected() ? "connected" : "disconnected";
  doc["ssid"] = getWiFiSSID();
  doc["ip"] = getWiFiIP();
  doc["rssi"] = getWiFiRSSI();
  doc["mac"] = getWiFiMAC();

  Serial.print("WiFi Status: "); // Debug print
  Serial.print("Status: "); Serial.println(doc["status"].as<String>()); // Debug print
  Serial.print("SSID: "); Serial.println(doc["ssid"].as<String>()); // Debug print
  Serial.print("IP: "); Serial.println(doc["ip"].as<String>()); // Debug print
  Serial.print("RSSI: "); Serial.println(doc["rssi"].as<int>()); // Debug print
  Serial.print("MAC: "); Serial.println(doc["mac"].as<String>()); // Debug print

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

// Add this helper function to sanitize input strings
String sanitizeString(const String& input) {
    String result = "";
    for (size_t i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        // Only allow printable ASCII characters, skip control chars
        if (c >= 32 && c <= 126) {
            result += c;
        }
    }
    return result;
}

// Function to repair corrupted WiFi credentials
bool repairWiFiCredentials() {
    Preferences prefs;
    if (!prefs.begin(WIFI_NAMESPACE, true)) {
        Serial.println("Failed to access WiFi settings");
        return false;
    }
    
    String wifiListJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
    prefs.end();
    
    // Parse existing JSON
    DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(doc, wifiListJson);
    
    if (error) {
        Serial.println("Error parsing WiFi credentials, resetting to empty array");
        if (prefs.begin(WIFI_NAMESPACE, false)) {
            prefs.putString(WIFI_CREDENTIALS_KEY, "[]");
            prefs.end();
        }
        return true;
    }
    
    // Check if it's an array
    if (!doc.is<JsonArray>()) {
        Serial.println("WiFi credentials not stored as array, resetting");
        if (prefs.begin(WIFI_NAMESPACE, false)) {
            prefs.putString(WIFI_CREDENTIALS_KEY, "[]");
            prefs.end();
        }
        return true;
    }
    
    // Process each network entry to sanitize
    bool needsUpdate = false;
    JsonArray networks = doc.as<JsonArray>();
    
    for (JsonObject network : networks) {
        if (network.containsKey("ssid") && network.containsKey("password")) {
            String originalSsid = network["ssid"].as<String>();
            String originalPassword = network["password"].as<String>();
            
            String cleanSsid = sanitizeString(originalSsid);
            String cleanPassword = sanitizeString(originalPassword);
            
            // Check if anything changed
            if (originalSsid != cleanSsid || originalPassword != cleanPassword) {
                network["ssid"] = cleanSsid;
                network["password"] = cleanPassword;
                needsUpdate = true;
            }
        }
    }
    
    // If we sanitized anything, save the updated data
    if (needsUpdate) {
        String cleanJson;
        serializeJson(doc, cleanJson);
        
        if (prefs.begin(WIFI_NAMESPACE, false)) {
            bool success = prefs.putString(WIFI_CREDENTIALS_KEY, cleanJson);
            prefs.end();
            
            Serial.println("Sanitized WiFi credentials saved");
            return success;
        }
    }
    
    return true;
}

bool saveWiFiCredentialsToNVS(const String& ssid, const String& password, int priority) {
    Preferences prefs;
    prefs.begin(WIFI_NAMESPACE, false); // Read-write mode

    // Load existing credentials
    String wifiListJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");

    DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(doc, wifiListJson);
    if (error) {
        Serial.println("Failed to parse wifi list: " + String(error.c_str()));
        prefs.end();
        return false;
    }

    JsonArray wifiList = doc.to<JsonArray>();
    
    // If priority is -1 (default), set it to the next available priority
    if (priority == -1) {
        priority = wifiList.size() + 1;
    } else {
        // Check if we need to update priorities of other networks
        for (JsonObject network : wifiList) {
            if (network["priority"].as<int>() >= priority) {
                // Increment priority of networks with priority >= the new priority
                network["priority"] = network["priority"].as<int>() + 1;
            }
        }
    }

    // Check if this SSID already exists
    bool ssidExists = false;
    for (JsonObject network : wifiList) {
        if (network["ssid"].as<String>() == ssid) {
            // Update existing network
            network["password"] = password;
            network["priority"] = priority;
            ssidExists = true;
            break;
        }
    }

    // If SSID doesn't exist, add a new entry
    if (!ssidExists) {
        JsonObject newCreds = wifiList.createNestedObject();
        newCreds["ssid"] = ssid;
        newCreds["password"] = password;
        newCreds["priority"] = priority;
    }

    // Sort networks by priority
    // Create a temporary array for sorting
    JsonArray sortedArray = doc.to<JsonArray>();
    
    // Extract networks into a std::vector for sorting
    std::vector<JsonObject> networksVector;
    for (JsonObject network : wifiList) {
        networksVector.push_back(network);
    }
    
    // Sort the vector by priority
    std::sort(networksVector.begin(), networksVector.end(), 
        [](JsonObject a, JsonObject b) {
            return a["priority"].as<int>() < b["priority"].as<int>();
        });
    
    // Rebuild the array in priority order
    doc.clear();
    JsonArray sortedWifiList = doc.to<JsonArray>();
    for (JsonObject network : networksVector) {
        JsonObject newNetwork = sortedWifiList.createNestedObject();
        newNetwork["ssid"] = network["ssid"];
        newNetwork["password"] = network["password"];
        newNetwork["priority"] = network["priority"];
    }

    // Serialize the updated JSON document
    String updatedWifiListJson;
    serializeJson(doc, updatedWifiListJson);
    
    // Check size before saving
    size_t jsonSize = updatedWifiListJson.length();
    Serial.println("WiFi credentials JSON size: " + String(jsonSize) + " bytes");
    
    if (jsonSize > WIFI_CREDENTIALS_JSON_SIZE) {
        Serial.println("WiFi credentials JSON too large for NVS storage");
        prefs.end();
        return false;
    }

    // Save the updated JSON string to NVS
    bool success = prefs.putString(WIFI_CREDENTIALS_KEY, updatedWifiListJson);
    prefs.end();

    if (!success) {
        Serial.println("Failed to save wifi credentials");
    }

    return success;
}

// Backward compatibility overload
bool saveWiFiCredentialsToNVS(const String& ssid, const String& password) {
    return saveWiFiCredentialsToNVS(ssid, password, -1); // -1 means lowest priority
}

// Load WiFi credentials from NVS with proper error handling
String loadWiFiCredentialsFromNVS() {
    Preferences prefs;
    String result = "[]";  // Always start with a valid empty array
    
    if (prefs.begin(WIFI_NAMESPACE, true)) {
        if (prefs.isKey(WIFI_CREDENTIALS_KEY)) {
            result = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
        } else {
            // No credentials saved yet, initialize with empty array
            Serial.println("No WiFi credentials found, initializing empty array.");
            prefs.end();
            if (prefs.begin(WIFI_NAMESPACE, false)) {
                prefs.putString(WIFI_CREDENTIALS_KEY, "[]");
            }
        }
        prefs.end();
        
        // Validate JSON
        DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
        DeserializationError error = deserializeJson(doc, result);
        
        if (error) {
            Serial.println("Warning: Invalid JSON format in stored WiFi credentials. Resetting.");
            result = "[]";
            
            // Reset the stored data
            if (prefs.begin(WIFI_NAMESPACE, false)) {
                prefs.putString(WIFI_CREDENTIALS_KEY, "[]");
                prefs.end();
            }
        } else if (!doc.is<JsonArray>()) {
            // Ensure it's actually an array
            Serial.println("Warning: WiFi credentials not stored as array. Resetting.");
            result = "[]";
            
            if (prefs.begin(WIFI_NAMESPACE, false)) {
                prefs.putString(WIFI_CREDENTIALS_KEY, "[]");
                prefs.end();
            }
        } else {
            // Log the parsed structure
            Serial.print("Loaded WiFi networks: ");
            serializeJson(doc, Serial);
            Serial.println();
        }
    }
    
    // After loading but before returning, attempt to repair any corrupted data
    repairWiFiCredentials();
    
    // Reload after repair
    if (prefs.begin(WIFI_NAMESPACE, true)) {
        result = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
        prefs.end();
    }
    
    return result;
}

bool resetWiFi() {
    resetWiFiSettings(); // Call the function from wifi_manager_wrapper
    return true; // Assume success, as there's no return value from resetWiFiSettings
}

// Add a new function to remove a WiFi credential by index
bool removeWiFiCredentialByIndex(int index) {
    Preferences prefs;
    prefs.begin(WIFI_NAMESPACE, false); // Read-write mode

    // Load existing credentials
    String wifiListJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");

    DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(doc, wifiListJson);
    if (error) {
        Serial.println("Failed to parse wifi list: " + String(error.c_str()));
        prefs.end();
        return false;
    }

    JsonArray wifiList = doc.as<JsonArray>();
    
    // Check if index is valid
    if (index < 0 || index >= wifiList.size()) {
        Serial.println("Invalid index for WiFi credential removal: " + String(index));
        prefs.end();
        return false;
    }
    
    // Create a new array without the element at the specified index
    JsonArray newWifiList = doc.to<JsonArray>();
    newWifiList.remove(index);
    
    // Serialize the updated JSON document
    String updatedWifiListJson;
    serializeJson(doc, updatedWifiListJson);
    
    // Save the updated JSON string to NVS
    bool success = prefs.putString(WIFI_CREDENTIALS_KEY, updatedWifiListJson);
    prefs.end();
    
    if (!success) {
        Serial.println("Failed to save updated WiFi credentials after removal");
    } else {
        Serial.println("Successfully removed WiFi credential at index: " + String(index));
    }
    
    return success;
}

// Update priority of a specific network - improved implementation
bool updateWiFiNetworkPriority(int index, int newPriority) {
    Preferences prefs;
    prefs.begin(WIFI_NAMESPACE, false); // Read-write mode

    Serial.print("Updating WiFi network priority: index=");
    Serial.print(index);
    Serial.print(", newPriority=");
    Serial.println(newPriority);

    // Load existing credentials
    String wifiListJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
    Serial.print("Loaded credentials JSON: ");
    Serial.println(wifiListJson);

    DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(doc, wifiListJson);
    if (error) {
        Serial.println("Failed to parse wifi list: " + String(error.c_str()));
        prefs.end();
        return false;
    }

    JsonArray wifiList = doc.as<JsonArray>();
    Serial.print("Found ");
    Serial.print(wifiList.size());
    Serial.println(" saved networks");
    
    // Check if index is valid
    if (index < 0 || index >= wifiList.size()) {
        Serial.println("Invalid index for WiFi priority update: " + String(index));
        prefs.end();
        return false;
    }
    
    // Create a simple flat array for easier manipulation
    struct NetworkInfo {
        String ssid;
        String password;
        int priority;
    };
    
    std::vector<NetworkInfo> networks;
    int i = 0;
    for (JsonObject network : wifiList) {
        NetworkInfo info;
        info.ssid = network["ssid"].as<String>();
        info.password = network["password"].as<String>();
        info.priority = network["priority"] | (i + 1); // Default to index+1 if priority not set
        networks.push_back(info);
        i++;
    }
    
    // Get current priority of the network to change
    int currentPriority = networks[index].priority;
    
    // Ensure newPriority is valid
    if (newPriority < 1) newPriority = 1;
    if (newPriority > networks.size()) newPriority = networks.size();
    
    // No change needed if priorities match
    if (currentPriority == newPriority) {
        Serial.println("No priority change needed - current and new are the same");
        prefs.end();
        return true;
    }
    
    Serial.print("Moving network from priority ");
    Serial.print(currentPriority);
    Serial.print(" to priority ");
    Serial.println(newPriority);
    
    // Update priorities
    for (auto& network : networks) {
        if (network.priority == currentPriority) {
            // This is the target network - set its new priority
            network.priority = newPriority;
        }
        else if (currentPriority < newPriority) {
            // Moving down (increasing number/decreasing priority)
            if (network.priority > currentPriority && network.priority <= newPriority) {
                network.priority--;
            }
        }
        else { // currentPriority > newPriority
            // Moving up (decreasing number/increasing priority)
            if (network.priority >= newPriority && network.priority < currentPriority) {
                network.priority++;
            }
        }
    }
    
    // Sort by priority
    std::sort(networks.begin(), networks.end(), 
        [](const NetworkInfo& a, const NetworkInfo& b) {
            return a.priority < b.priority;
        }
    );
    
    // Rebuild the JSON array
    doc.clear();
    JsonArray newWifiList = doc.to<JsonArray>();
    for (const auto& network : networks) {
        JsonObject newNetwork = newWifiList.createNestedObject();
        newNetwork["ssid"] = network.ssid;
        newNetwork["password"] = network.password;
        newNetwork["priority"] = network.priority;
    }
    
    // Serialize and save
    String updatedWifiListJson;
    serializeJson(doc, updatedWifiListJson);
    
    Serial.print("Saving updated credentials JSON: ");
    Serial.println(updatedWifiListJson);
    
    bool success = prefs.putString(WIFI_CREDENTIALS_KEY, updatedWifiListJson);
    prefs.end();
    
    return success;
}
