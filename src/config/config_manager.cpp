#include "config_manager.h"
#include <FS.h>
#include <LittleFS.h>  // Built-in ESP32 LittleFS
#include <ArduinoJson.h>

// Default configuration - made static to avoid global namespace conflict
static DeviceConfig configData = {
  1,           // modbusId
  9600,        // baudRate
  8,           // dataBits
  0,           // parity (0=none, 1=odd, 2=even)
  1,           // stopBits
  5000,        // updateInterval in ms
  "XY-SK150S"   // deviceName
};

bool loadConfig() {
  File configFile = LittleFS.open("/config.json", "r");

  if (!configFile) {
    Serial.println("Failed to open config file for reading");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    configFile.close();
    return false;
  }

  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  configFile.close();

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, buf.get());

  if (error) {
    Serial.println("Failed to parse config file");
    return false;
  }

  configData.modbusId = doc["modbusId"] | 1;
  configData.baudRate = doc["baudRate"] | 9600;
  configData.dataBits = doc["dataBits"] | 8;
  configData.parity = doc["parity"] | 0;
  configData.stopBits = doc["stopBits"] | 1;
  configData.updateInterval = doc["updateInterval"] | 5000;

  strlcpy(configData.deviceName,
          doc["deviceName"] | "XY-SK120",
          sizeof(configData.deviceName));

  Serial.println("Config loaded");
  return true;
}

bool saveConfig() {
  DynamicJsonDocument doc(1024);

  doc["modbusId"] = configData.modbusId;
  doc["baudRate"] = configData.baudRate;
  doc["dataBits"] = configData.dataBits;
  doc["parity"] = configData.parity;
  doc["stopBits"] = configData.stopBits;
  doc["updateInterval"] = configData.updateInterval;
  doc["deviceName"] = configData.deviceName;

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  serializeJson(doc, configFile);
  configFile.close();

  Serial.println("Config saved");
  return true;
}

DeviceConfig& getConfig() {
  return configData;
}
