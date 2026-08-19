#include "config_manager.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// Default configuration - made static to avoid global namespace conflict
static DeviceConfig configData = {
  1,           // modbusId
  9600,        // baudRate
  8,           // dataBits
  0,           // parity (0=none, 1=odd, 2=even)
  1,           // stopBits
  5000,        // updateInterval in ms
  "XY-SK150S"  // deviceName
};

static bool loadFromNVS(DeviceConfig& out) {
  Preferences prefs;
  if (!prefs.begin("xyskcfg", true)) {
    // Namespace does not exist yet (first boot). Create it with the compiled-in
    // defaults so later reads don't spam nvs_open NOT_FOUND.
    if (prefs.begin("xyskcfg", false)) {
      prefs.putUChar("modbusId", configData.modbusId);
      prefs.putULong("baudRate", configData.baudRate);
      prefs.putUChar("dataBits", configData.dataBits);
      prefs.putUChar("parity", configData.parity);
      prefs.putUChar("stopBits", configData.stopBits);
      prefs.putUShort("updateInterval", configData.updateInterval);
      prefs.putString("deviceName", configData.deviceName);
      prefs.end();
      out = configData;
      return true;
    }
    Serial.println("Failed to open NVS for config writing");
    return false;
  }
  out.modbusId = prefs.getUChar("modbusId", configData.modbusId);
  out.baudRate = prefs.getULong("baudRate", configData.baudRate);
  out.dataBits = prefs.getUChar("dataBits", configData.dataBits);
  out.parity = prefs.getUChar("parity", configData.parity);
  out.stopBits = prefs.getUChar("stopBits", configData.stopBits);
  out.updateInterval = prefs.getUShort("updateInterval", configData.updateInterval);
  String name = prefs.getString("deviceName", configData.deviceName);
  strlcpy(out.deviceName, name.c_str(), sizeof(out.deviceName));
  prefs.end();
  return true;
}

bool loadConfig() {
  bool ok = loadFromNVS(configData);
  if (ok) Serial.println("Config loaded");
  return ok;
}

bool saveConfig() {
  Preferences prefs;
  if (!prefs.begin("xyskcfg", false)) {
    Serial.println("Failed to open NVS for config writing");
    return false;
  }
  prefs.putUChar("modbusId", configData.modbusId);
  prefs.putULong("baudRate", configData.baudRate);
  prefs.putUChar("dataBits", configData.dataBits);
  prefs.putUChar("parity", configData.parity);
  prefs.putUChar("stopBits", configData.stopBits);
  prefs.putUShort("updateInterval", configData.updateInterval);
  prefs.putString("deviceName", configData.deviceName);
  prefs.end();

  Serial.println("Config saved");
  return true;
}

DeviceConfig& getConfig() {
  // Refresh from NVS on first access so external changes are reflected
  static bool loaded = false;
  if (!loaded) {
    loadFromNVS(configData);
    loaded = true;
  }
  return configData;
}