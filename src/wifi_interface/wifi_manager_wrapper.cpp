#include "wifi_manager_wrapper.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "wifi_manager_instance.h"

// Reference the globally defined WiFiManager instance instead of defining it here
extern WiFiManager wifiManager;

// Global flag to track if WiFiManager saved credentials
bool wifiCredentialsSaved = false;

// Map common ESP32 WiFi disconnect reasons to human-readable strings
const char* wifiDisconnectReasonStr(uint8_t reason) {
  switch (reason) {
    case 1: return "UNSPECIFIED";
    case 2: return "AUTH_EXPIRE";
    case 3: return "AUTH_LEAVE";
    case 4: return "ASSOC_EXPIRE";
    case 8: return "BEACON_TIMEOUT";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 200: return "AUTH_FAIL";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL2";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    case 206: return "AP_TSF_RESET";
    default: return "OTHER";
  }
}

// WiFi event handler to log the real reason a STA connection fails
void wifiStaDisconnectedHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("[WIFI] STA disconnected, reason=%u (%s)\n", reason, wifiDisconnectReasonStr(reason));
  }
}

// Callback when WiFiManager saves config
void saveWifiCallback() {
  Serial.println("WiFiManager: Credentials Saved");
  wifiCredentialsSaved = true;

  // Immediately try to sync the current WiFi to our storage
  // This ensures we capture the network while we're still connected
  syncCurrentWiFiToStorage();
}

// Process WiFiManager credentials to our storage system
void processWiFiManagerCredentials() {
  if (wifiCredentialsSaved) {
    Serial.println("Processing WiFiManager credentials");

    // Get the credentials that WiFiManager just saved
    String ssid = WiFi.SSID();
    String password = WiFi.psk();

    if (ssid.length() > 0) {
      // Save to our NVS storage with highest priority
      saveWiFiCredentialsToNVS(ssid, password, 1);
      Serial.println("WiFiManager credentials transferred to NVS storage: " + ssid);
    }

    wifiCredentialsSaved = false;
  }
}

// Enhance the syncCurrentWiFiToStorage function
void syncCurrentWiFiToStorage() {
  // Check if we're connected to WiFi
  if (WiFi.status() == WL_CONNECTED) {
    String currentSSID = WiFi.SSID();

    if (currentSSID.length() > 0) {
      Serial.print("Checking if current WiFi (");
      Serial.print(currentSSID);
      Serial.println(") is in saved networks...");

      // Get saved networks
      String wifiListJson = loadWiFiCredentialsFromNVS();

      // Check if current network is already saved
      bool alreadySaved = false;

      if (wifiListJson != "[]") {
        DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
        DeserializationError error = deserializeJson(doc, wifiListJson);

        if (!error) {
          JsonArray networks = doc.as<JsonArray>();

          for (JsonObject network : networks) {
            String savedSSID = network["ssid"].as<String>();
            if (savedSSID == currentSSID) {
              alreadySaved = true;
              break;
            }
          }
        }
      }

      // If not saved, save it - but we need to handle a special case here
      if (!alreadySaved) {
        Serial.println("Current network not found in saved networks. Adding it...");

        // WiFi.psk() often doesn't work (returns empty) so we'll fake a temporary password
        // This password will work for reconnection because the ESP32 still has the real password stored
        // in its internal flash, but we can't access it for security reasons
        String tempPassword = "temp_password_" + String(random(10000, 99999));
        Serial.println("Note: Using a temporary password as ESP32 doesn't expose the actual WiFi password");

        // Create the JSON document explicitly
        DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);

        // Check if we already have a JSON array or need to create one
        if (wifiListJson == "[]") {
          // Create a new array
          JsonArray networks = doc.to<JsonArray>();

          // Create a new network object
          JsonObject network = networks.createNestedObject();
          network["ssid"] = currentSSID;
          network["password"] = tempPassword;
          network["priority"] = 1;  // Set highest priority

          // Serialize to String
          String jsonOutput;
          serializeJson(doc, jsonOutput);

          // Log the size to debug
          Serial.print("WiFi credentials JSON size: ");
          Serial.println(jsonOutput.length());

          // Save the JSON to NVS
          Preferences prefs;
          if (prefs.begin(WIFI_NAMESPACE, false)) {
            bool success = prefs.putString(WIFI_CREDENTIALS_KEY, jsonOutput);
            prefs.end();

            if (success) {
              Serial.println("Successfully saved current WiFi network to storage!");
              Serial.println("NOTE: Password is a placeholder. To set the real password, use 'syncwifi' while connected.");
            } else {
              Serial.println("Failed to save current WiFi network to storage.");
            }
          }
        } else {
          // Parse existing JSON
          DeserializationError error = deserializeJson(doc, wifiListJson);
          if (!error) {
            // Get networks array
            JsonArray networks = doc.as<JsonArray>();

            // Update priorities - increment all existing ones to make room for new entry
            for (JsonObject network : networks) {
              int priority = network["priority"];
              network["priority"] = priority + 1;
            }

            // Add new network with priority 1
            JsonObject newNetwork = networks.createNestedObject();
            newNetwork["ssid"] = currentSSID;
            newNetwork["password"] = tempPassword;
            newNetwork["priority"] = 1;

            // Serialize to String
            String jsonOutput;
            serializeJson(doc, jsonOutput);

            // Log the size to debug
            Serial.print("WiFi credentials JSON size: ");
            Serial.println(jsonOutput.length());

            // Save the JSON to NVS
            Preferences prefs;
            if (prefs.begin(WIFI_NAMESPACE, false)) {
              bool success = prefs.putString(WIFI_CREDENTIALS_KEY, jsonOutput);
              prefs.end();

              if (success) {
                Serial.println("Successfully saved current WiFi network to storage!");
                Serial.println("NOTE: This network is saved with a placeholder password but will");
                Serial.println("still be tried using WiFiManager's internal credentials on boot.");
              } else {
                Serial.println("Failed to save current WiFi network to storage.");
              }
            }
          }
        }
      } else {
        Serial.println("Current network already in saved networks.");
      }
    }
  }
}

bool connectToSavedNetworks() {
  // FIRST TRY: Use WiFiManager's internally stored credentials (special priority 0)
  Serial.println("First trying WiFiManager's internal credentials...");
  WiFi.disconnect(); // stop any stale/ongoing STA attempt first
  delay(100);
  WiFi.begin(); // This attempts to connect using ESP32's internal stored credentials

  // Wait for connection with timeout
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  // If connected with WiFiManager's credentials, return success
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected using WiFiManager's internal credentials!");
    Serial.print("Connected to: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Sync the network to our storage but mark it as a placeholder
    syncCurrentWiFiToStorage();
    return true;
  }

  // SECOND TRY: Load and try saved WiFi credentials from NVS in priority order
  Serial.println("WiFiManager credentials failed. Trying NVS-stored networks...");
  WiFi.disconnect(); // abort the still-running connect attempt before moving on
  delay(100);
  String wifiListJson = loadWiFiCredentialsFromNVS();

  // If no saved credentials, return false and let WiFiManager handle it
  if (wifiListJson == "[]") {
    Serial.println("No saved WiFi credentials found in NVS");
    return false;
  }

  // Parse the saved credentials
  DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
  DeserializationError error = deserializeJson(doc, wifiListJson);
  if (error) {
    Serial.println("Failed to parse WiFi credentials JSON");
    return false;
  }

  // Get the networks array
  JsonArray networks = doc.as<JsonArray>();
  if (networks.size() == 0) {
    Serial.println("No networks found in saved credentials");
    return false;
  }

  // Sort networks by priority
  // Create a temporary vector for sorting
  struct NetworkInfo {
    String ssid;
    String password;
    int priority;
  };

  std::vector<NetworkInfo> sortedNetworks;

  for (JsonObject network : networks) {
    NetworkInfo info;
    info.ssid = network["ssid"].as<String>();
    info.password = network["password"].as<String>();
    info.priority = network["priority"] | sortedNetworks.size() + 1;
    sortedNetworks.push_back(info);
  }

  // Sort by priority (lower number = higher priority)
  std::sort(sortedNetworks.begin(), sortedNetworks.end(),
    [](const NetworkInfo& a, const NetworkInfo& b) {
      return a.priority < b.priority;
    }
  );

  // Try to connect to each network in priority order
  for (const NetworkInfo& network : sortedNetworks) {
    Serial.print("Attempting to connect to network: ");
    Serial.print(network.ssid);
    Serial.print(" (Priority: ");
    Serial.print(network.priority);
    Serial.println(")");

    // Skip networks with placeholder passwords as they won't work
    if (network.password.startsWith("temp_password_")) {
      Serial.println("Skipping network with placeholder password - will be tried using WiFiManager's credentials instead");
      continue;
    }

    // Attempt to connect with a timeout
    WiFi.disconnect(); // stop any stale/ongoing STA attempt first
    delay(100);
    WiFi.begin(network.ssid.c_str(), network.password.c_str());

    // Wait for connection (with timeout)
    int connectAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && connectAttempts < 20) {
      delay(500);
      Serial.print(".");
      connectAttempts++;
    }

    // If connected, return success
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.print("Connected to ");
      Serial.println(network.ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return true;
    }

    Serial.println();
    Serial.print("Failed to connect to ");
    Serial.println(network.ssid);
  }

  // If all attempts fail, return false to trigger portal mode
  WiFi.disconnect(); // fully stop STA so WiFiManager's autoConnect starts from a clean state
  delay(100);
  Serial.println("All saved networks failed to connect");
  return false;
}

bool initWiFiManager(const char* apName) {
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // keep the radio responsive (helps STA connect and AP association)
  // 10dBm instead of max 19.5dBm: on ESP32-C3/S3 the RF front-end saturates at max power,
  // which breaks the data path (AP disappears / association fails / STA handshake timeout).
  WiFi.setTxPower(WIFI_POWER_11dBm);

  // Register diagnostic handler for STA disconnect reasons
  WiFi.onEvent(wifiStaDisconnectedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // Diagnostic: what does WiFiManager have saved (mask the password)
  {
    String savedSSID = wifiManager.getWiFiSSID(true);
    String savedPass = wifiManager.getWiFiPass(true);
    Serial.printf("[WIFI] WiFiManager saved: SSID='%s' passLen=%u\n",
                  savedSSID.c_str(), savedPass.length());
  }

  // Set custom AP name
  wifiManager.setConfigPortalTimeout(180); // 3 minutes timeout for the config portal

  // Set the callback for when the user saves WiFi credentials
  wifiManager.setSaveConfigCallback(saveWifiCallback);

  // Add custom parameter for device name
  WiFiManagerParameter custom_device_name("deviceName", "Device Name", "XY-SK150S", 40);
  wifiManager.addParameter(&custom_device_name);

  // Attempt to connect to WiFi using saved credentials
  if (!connectToSavedNetworks()) {
    // If all saved networks fail, start the AP
    bool result = wifiManager.autoConnect(apName);

    if (result) {
      // If connected, sync current connection to storage
      syncCurrentWiFiToStorage();
    }

    return result;
  }

  return true;
}

bool initWiFiManager(const char* apName, const char* apPassword) {
  WiFiManager wifiManager;

  // Set debug output to true for more information
  wifiManager.setDebugOutput(true);

  // Configure access point
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));

  // Set access point channel
  WiFi.setTxPower(WIFI_POWER_11dBm);

  // Extend timeout to 5 minutes to give more time for configuration
  wifiManager.setConfigPortalTimeout(300);

  // Lower the signal quality threshold to see more networks
  wifiManager.setMinimumSignalQuality(10);

  // Set the callback for when the user saves WiFi credentials
  wifiManager.setSaveConfigCallback(saveWifiCallback);

  // Set a custom header to make the portal more user-friendly
  wifiManager.setCustomHeadElement("<style>body{background-color:#f8f9fa;font-family:Arial,sans-serif;}</style>");

  // Make the network scan refresh more frequently (every 10 seconds)
  wifiManager.setWiFiAPChannel(1);
  wifiManager.setScanDispPerc(true);

  // Normal connection attempt
  Serial.print("Attempting to connect to WiFi or starting AP named: ");
  Serial.println(apName);

  // Run the autoConnect with or without password as needed
  bool result;
  if (apPassword == NULL || strlen(apPassword) == 0) {
    result = wifiManager.autoConnect(apName);
  } else {
    result = wifiManager.autoConnect(apName, apPassword);
  }

  // Give network time to stabilize after connection
  if (result) {
    delay(1000);

    // Process any credentials that were saved
    processWiFiManagerCredentials();

    // Also check if current connection is saved
    syncCurrentWiFiToStorage();
  }

  return result;
}

// Exit the config portal properly
bool exitConfigPortal() {
  // Process any credentials before stopping portal
  processWiFiManagerCredentials();

  // Tell WiFiManager to stop the portal
  bool success = wifiManager.stopConfigPortal();

  // Make sure WiFi is in the correct mode
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  return success;
}

void resetWiFiSettings() {
  wifiManager.resetSettings();
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String getWiFiSSID() {
  return WiFi.SSID();
}

String getWiFiIP() {
  return WiFi.localIP().toString();
}

int getWiFiRSSI() {
  return WiFi.RSSI();
}

String getWiFiMAC() {
  return WiFi.macAddress();
}

// Legacy function that now calls the NVS version
bool saveWiFiCredentials(const String& ssid, const String& password) {
  return saveWiFiCredentialsToNVS(ssid, password);
}

// Legacy function that now calls the NVS version
String loadWiFiCredentials() {
  return loadWiFiCredentialsFromNVS();
}
