#ifndef PSU_SERVICE_H
#define PSU_SERVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "XY-SKxxx.h"

// Mutex protecting Modbus bus access. The status poller runs in loop() while
// MQTT command handlers run in the mqtt task, so concurrent Modbus
// transactions would corrupt the RTU bus. Recursive so nested helper calls work.
void initPsuService();
void lockModbus();
void unlockModbus();

// Status snapshot structure (kept identical to the former web interface so the
// external UI contract stays the same).
struct PSUStatusData {
  bool valid;
  float voltage, current, power;
  float inputVoltage;
  float voltageSet, currentSet, powerSet;
  bool outputEnabled, keyLocked, cpModeEnabled;
  uint16_t cvccMode;
  OperatingMode operatingMode;
  uint16_t model, version;
  float lvp, ovp, ocp, opp, otp;

  float ampHours;
  float wattHours;
  uint32_t outputTime;
  float internalTemp;
  float externalTemp;
  uint16_t protectionStatus;

  bool tempCelsius;
  uint8_t backlight;
  uint8_t sleepTimeout;
  uint8_t slaveAddress;
  uint8_t baudRateCode;
  bool beeper;
  uint8_t memoryGroup;
  bool mpptEnabled;
  float mpptThreshold;
  float batteryCutoff;
  bool outputOnAtStartup;
  float etp;

  bool bchEnabled;
  float bchThreshold;
  bool btfEnabled;
  float btfCutoff;
  bool clofEnabled;

  uint16_t hostType;
  uint16_t wifiConfig;
  uint16_t wifiStatus;
  uint32_t ipv4;

  uint16_t ohpHours, ohpMinutes;
  float overAmpHours;
  float overWattHours;
};

// Read fresh PSU status in batched Modbus reads. Returns false if the PSU
// did not respond.
bool readPSUStatusBatched(PSUStatusData& data);

// Build and serialize the status JSON (same schema the old web UI used).
String buildStatusJSON(const PSUStatusData& data);

// PSU helper functions (thread-safe, lock internally).
float getPSUVoltage(XY_SKxxx* powerSupply);
float getPSUCurrent(XY_SKxxx* powerSupply);
float getPSUPower(XY_SKxxx* powerSupply);
bool isPSUOutputEnabled(XY_SKxxx* powerSupply);
bool setPSUOutput(XY_SKxxx* powerSupply, bool enable);
String getPSUOperatingMode(XY_SKxxx* powerSupply);
void getPSUOperatingModeDetails(XY_SKxxx* powerSupply, String& modeName, float& setValue);

// ESPHome-style keep-alive of the WiFi host registers (0x0030-0x0034).
// Always claims the host as WiFi master (0x3B3A) and reports the actual
// WiFi status (0=offline, 5=connected) + our local IP every ~1s.
void wifiModuleKeepAlive();

// Dispatch a single command {action, ...} and return the response JSON string.
// Used by the MQTT subscriber. Returns "" for unknown actions.
String handleMqttAction(const String& action, const char* payload);

#endif // PSU_SERVICE_H