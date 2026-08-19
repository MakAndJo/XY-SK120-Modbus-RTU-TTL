#include "wifi_native.h"
#include "wifi_settings.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>

// Map common ESP32 WiFi disconnect reasons to human-readable strings
static const char* wifiDisconnectReasonStr(uint8_t reason) {
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

static void wifiStaDisconnectedHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("[WIFI] STA disconnected, reason=%u (%s)\n", reason, wifiDisconnectReasonStr(reason));
  }
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

void initNativeWifiRadio() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // keep the radio responsive
  // 10-11dBm instead of max 19.5dBm: on ESP32-C3/S3 the RF front-end
  // saturates at max power, which breaks the data path.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.onEvent(wifiStaDisconnectedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

// Connect to one specific network with timeout. Skip placeholder passwords.
bool connectToSavedNetworks() {
  WiFi.disconnect();
  delay(100);

  String wifiListJson = loadWiFiCredentialsFromNVS();
  if (wifiListJson == "[]") {
    Serial.println("No saved WiFi credentials found in NVS");
    return false;
  }

  DynamicJsonDocument doc(WIFI_CREDENTIALS_JSON_SIZE);
  DeserializationError error = deserializeJson(doc, wifiListJson);
  if (error) {
    Serial.println("Failed to parse WiFi credentials JSON");
    return false;
  }

  JsonArray networks = doc.as<JsonArray>();
  if (networks.size() == 0) {
    return false;
  }

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
    info.priority = network["priority"] | (sortedNetworks.size() + 1);
    sortedNetworks.push_back(info);
  }

  std::sort(sortedNetworks.begin(), sortedNetworks.end(),
    [](const NetworkInfo& a, const NetworkInfo& b) {
      return a.priority < b.priority;
    });

  for (const NetworkInfo& network : sortedNetworks) {
    // Skip networks stored with a placeholder password (cannot work)
    if (network.password.startsWith("temp_password_") ||
        network.password.startsWith("placeholder_")) {
      Serial.println("Skipping network with placeholder password: " + network.ssid);
      continue;
    }

    Serial.printf("[WIFI] Connecting to %s (priority %d)...\n",
                  network.ssid.c_str(), network.priority);

    WiFi.disconnect();
    delay(100);
    WiFi.begin(network.ssid.c_str(), network.password.c_str());

    int connectAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && connectAttempts < 20) {
      delay(500);
      Serial.print(".");
      connectAttempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WIFI] Connected to %s, IP=%s\n",
                    network.ssid.c_str(), WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("[WIFI] Failed to connect to " + network.ssid);
  }

  WiFi.disconnect();
  delay(100);
  Serial.println("[WIFI] All saved networks failed to connect");
  return false;
}

void resetWiFiSettings() {
  Preferences prefs;
  if (prefs.begin(WIFI_NAMESPACE, false)) {
    prefs.putString(WIFI_CREDENTIALS_KEY, "[]");
    prefs.end();
  }
  WiFi.disconnect(true);
}

bool exitConfigPortal() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  return true;
}