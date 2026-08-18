// Fix HTTP method definition conflicts between ESPAsyncWebServer and WiFiManager
#define WEBSERVER_H  // Prevent WebServer.h from being included directly

// Define HTTP methods for ESPAsyncWebServer before including it
// These match the WebRequestMethod enum in ESPAsyncWebServer.h
#define HTTP_GET     0b00000001
#define HTTP_POST    0b00000010
#define HTTP_DELETE  0b00000100
#define HTTP_PUT     0b00001000
#define HTTP_PATCH   0b00010000
#define HTTP_HEAD    0b00100000
#define HTTP_OPTIONS 0b01000000
#define HTTP_ANY     0b01111111

#include "web_interface.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <AsyncWebSocket.h>
#include <WiFi.h>
#include "wifi_interface/wifi_manager_wrapper.h"
#include "wifi_interface/wifi_settings.h" // Include the new wifi_settings header
#include "modbus_handler.h"
#include "config_manager.h"
#include "web_interface/log_utils.h" // Update to use the web_interface-specific log utils

// Include XY-SKxxx header to access power supply functions
#include "XY-SKxxx.h"

// Include WiFi WebSocket handler
#include "../wifi_interface/wifi_websocket_handler.h"

// Declare external power supply instance
extern XY_SKxxx* powerSupply;

// Ensure we have the function declaration
extern void handleAddWifiNetworkCommand(AsyncWebSocketClient* client, DynamicJsonDocument& doc);

// Forward declarations for functions used before definition
bool isPSUKeyLocked(XY_SKxxx* powerSupply);
void handleKeyLockRequest(AsyncWebSocketClient* client);
void handleSetKeyLock(AsyncWebSocketClient* client, const JsonObject &json);

AsyncWebSocket ws("/ws");

// Mutex protecting Modbus bus access. The status poller runs in loop() while
// WebSocket handlers run in the async_tcp task, so concurrent Modbus
// transactions would corrupt the RTU bus. Recursive so nested helper calls
// (e.g. sendCompletePSUStatus -> readPSUStatusBatched) work.
SemaphoreHandle_t modbusMutex = nullptr;

void lockModbus() {
  if (modbusMutex) xSemaphoreTakeRecursive(modbusMutex, portMAX_DELAY);
}

void unlockModbus() {
  if (modbusMutex) xSemaphoreGiveRecursive(modbusMutex);
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".json")) return "application/json";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

bool handleFileRead(AsyncWebServerRequest *request) {
  String path = request->url();
  if (path.endsWith("/")) path += "index.html";
  
  String contentType = getContentType(path);
  
  // Check if file exists
  if (LittleFS.exists(path)) {
    request->send(LittleFS, path, contentType);
    return true;
  }
  
  // Special case for Apple Touch Icons and favicon - serve default if missing
  if (path.endsWith("apple-touch-icon.png") || 
      path.endsWith("apple-touch-icon-precomposed.png") ||
      path.endsWith("favicon.ico")) {
    
    // If we have a default icon file, serve that instead
    if (LittleFS.exists("/favicon.ico")) {
      request->send(LittleFS, "/favicon.ico", "image/x-icon");
      return true;
    } else {
      // Return a 204 No Content to prevent browser warnings
      request->send(204);
      return true;
    }
  }
  
  return false;
}

// Helper functions for XY-SKxxx power supply interface
// These work with the existing library methods instead of modifying the library

// Get voltage from power supply
float getPSUVoltage(XY_SKxxx* powerSupply) {
  float voltage = 0.0, current = 0.0, power = 0.0;
  if (powerSupply) {
    lockModbus();
    if (powerSupply->testConnection()) {
      powerSupply->getOutput(voltage, current, power);
    }
    unlockModbus();
  }
  return voltage;
}

// Get current from power supply
float getPSUCurrent(XY_SKxxx* powerSupply) {
  float voltage = 0.0, current = 0.0, power = 0.0;
  if (powerSupply) {
    lockModbus();
    if (powerSupply->testConnection()) {
      powerSupply->getOutput(voltage, current, power);
    }
    unlockModbus();
  }
  return current;
}

// Get power from power supply
float getPSUPower(XY_SKxxx* powerSupply) {
  float voltage = 0.0, current = 0.0, power = 0.0;
  if (powerSupply) {
    lockModbus();
    if (powerSupply->testConnection()) {
      powerSupply->getOutput(voltage, current, power);
    }
    unlockModbus();
  }
  return power;
}

// Check if output is enabled - fixed implementation
bool isPSUOutputEnabled(XY_SKxxx* powerSupply) {
  if (powerSupply) {
    lockModbus();
    bool enabled = false;
    if (powerSupply->testConnection()) {
      enabled = powerSupply->isOutputEnabled(true); // Force refresh from device
    }
    unlockModbus();
    return enabled;
  }
  return false;
}

// Set output state (on/off) - fixed implementation
bool setPSUOutput(XY_SKxxx* powerSupply, bool enable) {
  if (powerSupply) {
    lockModbus();
    bool success = false;
    if (powerSupply->testConnection()) {
      if (enable) {
        success = powerSupply->turnOutputOn();
      } else {
        success = powerSupply->turnOutputOff();
      }
    }
    unlockModbus();
    return success;
  }
  return false;
}

// Get operating mode from power supply
String getPSUOperatingMode(XY_SKxxx* powerSupply) {
  if (powerSupply) {
    lockModbus();
    String result = "Unknown";
    if (powerSupply->testConnection()) {
      OperatingMode mode = powerSupply->getOperatingMode(true);
      switch (mode) {
        case MODE_CV: result = "CV"; break;
        case MODE_CC: result = "CC"; break;
        case MODE_CP: result = "CP"; break;
        default: result = "Unknown";
      }
    }
    unlockModbus();
    return result;
  }
  return "Unknown";
}

// Get operating mode details including settings
void getPSUOperatingModeDetails(XY_SKxxx* powerSupply, String& modeName, float& setValue) {
  modeName = "Unknown";
  setValue = 0.0;
  if (!powerSupply) return;
  
  lockModbus();
  if (powerSupply->testConnection()) {
    OperatingMode mode = powerSupply->getOperatingMode(true);
    switch (mode) {
      case MODE_CV:
        modeName = "Constant Voltage";
        setValue = powerSupply->getCachedConstantVoltage(false);
        break;
      case MODE_CC:
        modeName = "Constant Current";
        setValue = powerSupply->getCachedConstantCurrent(false);
        break;
      case MODE_CP:
        modeName = "Constant Power";
        setValue = powerSupply->getCachedConstantPower(false);
        break;
      default:
        modeName = "Unknown";
        setValue = 0.0;
    }
  }
  unlockModbus();
}

// Efficient batched status read - reads all status registers in 4 Modbus
// transactions instead of ~15 separate single-register reads.
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
  
  // Energy & temperature measurements
  float ampHours;        // Output Ah
  float wattHours;       // Output Wh
  uint32_t outputTime;   // Output time in seconds
  float internalTemp;    // T_IN (°C/°F)
  float externalTemp;    // T_EX (°C/°F)
  uint16_t protectionStatus; // Protection code (0=Normal,1=OVP,...)
  
  // Device settings
  bool tempCelsius;      // F_C register (0=Celsius, 1=Fahrenheit)
  uint8_t backlight;     // B_LED
  uint8_t sleepTimeout;  // SLEEP (min)
  uint8_t slaveAddress;  // SLAVE_ADDR
  uint8_t baudRateCode;  // BAUDRATE
  bool beeper;           // BEEPER
  uint8_t memoryGroup;   // EXTRACT_M (0-9)
  bool mpptEnabled;      // MPPT_ENABLE
  float mpptThreshold;   // MPPT_THRESHOLD (0.00-1.00)
  float batteryCutoff;   // BTF (A)
  bool outputOnAtStartup; // S_INI
  float etp;             // S_ETP (external temperature protection)
  
  // Battery charging / output-off settings (SK150S, undocumented in SK120 docs)
  bool bchEnabled;       // BCH_ENABLE (0x0029)
  float bchThreshold;    // BCH_THRESHOLD (0x002A)
  bool btfEnabled;       // BTF_ENABLE (0x002B)
  float btfCutoff;       // BTF_CUTOFF (0x002C)
  bool clofEnabled;      // CLOF_ENABLE (0x002D)

  // Host / WiFi module registers (0x0030-0x0034)
  uint16_t hostType;     // MASTER (0x3B3A = WiFi host)
  uint16_t wifiConfig;   // WIFI-CONFIG (0=Invalid,1=Pairing,2=Valid)
  uint16_t wifiStatus;   // WIFI-STATUS (0-4)
  uint32_t ipv4;         // IP from IPV4-H/IPV4-L
  
  // Extended protection settings
  uint16_t ohpHours, ohpMinutes; // OHP time
  float overAmpHours;   // OHA (Ah)
  float overWattHours;  // OWH (Wh)
};

// Reads registers while caller holds the Modbus mutex
bool readPSUStatusBatchedLocked(PSUStatusData& data);

// Read fresh status from the PSU using batched register reads.
// Returns false if the PSU did not respond.
bool readPSUStatusBatched(PSUStatusData& data) {
  if (!powerSupply) return false;
  
  lockModbus();
  
  bool ok = readPSUStatusBatchedLocked(data);
  
  unlockModbus();
  return ok;
}

bool readPSUStatusBatchedLocked(PSUStatusData& data) {
  uint16_t buf[16];
  bool valid = true;
  float liveVoltageSet = 0, liveCurrentSet = 0;
  
  // Batch 1: 0x0000 - 0x000E (15 contiguous): V_SET, I_SET, VOUT, IOUT, POWER,
  // UIN, AH_L, AH_H, WH_L, WH_H, OUT_H, OUT_M, OUT_S, T_IN, T_EX
  if (!powerSupply->readRegisters(REG_V_SET, 15, buf)) {
    valid = false;
  } else {
    // buf[0]=V_SET, buf[1]=I_SET are the WORKING setpoints (updated by
    // memory-group recalls), unlike the profile registers read in batch 7.
    liveVoltageSet = buf[0] / 100.0f;
    liveCurrentSet = buf[1] / 1000.0f;
    data.voltage = buf[2] / 100.0f;
    data.current = buf[3] / 1000.0f;
    data.power = buf[4] / 100.0f;
    data.inputVoltage = buf[5] / 100.0f;
    // Energy counters (low word first, 0.01 unit per register - matches ESPHome)
    data.ampHours = ((uint32_t)buf[6] | ((uint32_t)buf[7] << 16)) * 0.01f;
    data.wattHours = ((uint32_t)buf[8] | ((uint32_t)buf[9] << 16)) * 0.01f;
    data.outputTime = buf[10] * 3600u + buf[11] * 60u + buf[12];
    data.internalTemp = buf[13] / 10.0f;
    data.externalTemp = buf[14] / 10.0f;
  }
  
  // Batch 2: LOCK(0x000F), PROTECT(0x0010), CVCC(0x0011), ONOFF(0x0012)
  if (!powerSupply->readRegisters(REG_LOCK, 4, buf)) {
    valid = false;
  } else {
    data.keyLocked = (buf[0] != 0);
    data.protectionStatus = buf[1];
    data.cvccMode = buf[2];
    data.outputEnabled = (buf[3] != 0);
  }
  
  // Batch 3: F_C(0x0013), B_LED(0x0014), SLEEP(0x0015)
  if (!powerSupply->readRegisters(REG_F_C, 3, buf)) {
    valid = false;
  } else {
    data.tempCelsius = (buf[0] == 0);
    data.backlight = buf[1];
    data.sleepTimeout = buf[2];
  }
  
  // Batch 4: MODEL(0x0016), VERSION(0x0017), SLAVE_ADDR(0x0018), BAUDRATE(0x0019)
  if (!powerSupply->readRegisters(REG_MODEL, 4, buf)) {
    valid = false;
  } else {
    data.model = buf[0];
    data.version = buf[1];
    data.slaveAddress = buf[2];
    data.baudRateCode = buf[3];
  }
  
  // Batch 5: BEEPER(0x001C), EXTRACT_M(0x001D)
  if (!powerSupply->readRegisters(REG_BEEPER, 2, buf)) {
    valid = false;
  } else {
    data.beeper = (buf[0] != 0);
    data.memoryGroup = buf[1] % 10;
  }
  
  // Batch 6: MPPT_ENABLE(0x001F), MPPT_THRESHOLD(0x0020), BTF(0x0021),
  // CP_ENABLE(0x0022), CP_SET(0x0023)
  if (!powerSupply->readRegisters(REG_MPPT_ENABLE, 5, buf)) {
    valid = false;
  } else {
    data.mpptEnabled = (buf[0] != 0);
    data.mpptThreshold = buf[1] / 100.0f;
    data.batteryCutoff = buf[2] / 1000.0f;
    data.cpModeEnabled = (buf[3] != 0);
    data.powerSet = buf[4] / 10.0f;
  }
  
  // Batch 7: 0x0050 - 0x005E (15 contiguous): CV_SET, CC_SET, S_LVP, S_OVP,
  // S_OCP, S_OPP, S_OHP_H, S_OHP_M, S_OAH_L, S_OAH_H, S_OWH_L, S_OWH_H,
  // S_OTP, S_INI, S_ETP
  if (!powerSupply->readRegisters(REG_CV_SET, 15, buf)) {
    valid = false;
  } else {
    data.voltageSet = buf[0] / 100.0f;
    data.currentSet = buf[1] / 1000.0f;
    data.lvp = buf[2] / 100.0f;
    data.ovp = buf[3] / 100.0f;
    data.ocp = buf[4] / 1000.0f;
    data.opp = buf[5] / 10.0f;
    data.ohpHours = buf[6];
    data.ohpMinutes = buf[7];
    uint32_t oah = (uint32_t)buf[8] | ((uint32_t)buf[9] << 16);
    data.overAmpHours = oah / 1000.0f;
    uint32_t owh = (uint32_t)buf[10] | ((uint32_t)buf[11] << 16);
    data.overWattHours = owh * 0.01f;
    data.otp = buf[12] / 10.0f;
    data.outputOnAtStartup = (buf[13] & 0x0001) != 0;
    data.etp = buf[14] / 10.0f;
    // Working setpoints always win - they reflect the active group recall
    data.voltageSet = liveVoltageSet;
    data.currentSet = liveCurrentSet;
  }

  // Batch 8: BCH_ENABLE(0x0029), BCH_THRESHOLD(0x002A), BTF_ENABLE(0x002B),
  // BTF_CUTOFF(0x002C), CLOF_ENABLE(0x002D) - battery charging/output-off settings
  if (!powerSupply->readRegisters(0x0029, 5, buf)) {
    valid = false;
  } else {
    data.bchEnabled = (buf[0] != 0);
    data.bchThreshold = buf[1] / 100.0f;
    data.btfEnabled = (buf[2] != 0);
    data.btfCutoff = buf[3] / 1000.0f;
    data.clofEnabled = (buf[4] != 0);
  }

  // Batch 9: MASTER(0x0030), WIFI_CONFIG(0x0031), WIFI_STATUS(0x0032),
  // IPV4_H(0x0033), IPV4_L(0x0034) - host type / WiFi module info
  if (!powerSupply->readRegisters(0x0030, 5, buf)) {
    valid = false;
  } else {
    data.hostType = buf[0];
    data.wifiConfig = buf[1];
    data.wifiStatus = buf[2];
    data.ipv4 = ((uint32_t)buf[3] << 16) | buf[4];
  }
  
  if (!valid) return false;
  
  // Determine operating mode: CP > CC > CV (matches library logic)
  if (data.cpModeEnabled) {
    data.operatingMode = MODE_CP;
  } else if (data.cvccMode == 1) {
    data.operatingMode = MODE_CC;
  } else {
    data.operatingMode = MODE_CV;
  }
  
  data.valid = true;
  return true;
}

// Build and serialize the status JSON from a status snapshot
String buildStatusJSON(const PSUStatusData& data) {
  DynamicJsonDocument doc(2048);
  doc["action"] = "statusResponse";
  doc["connected"] = data.valid;
  doc["outputEnabled"] = data.outputEnabled;
  doc["voltage"] = data.voltage;
  doc["current"] = data.current;
  doc["power"] = data.power;
  doc["inputVoltage"] = data.inputVoltage;
  
  // Energy & temperatures
  doc["ampHours"] = data.ampHours;
  doc["wattHours"] = data.wattHours;
  doc["outputTime"] = data.outputTime;
  doc["internalTemp"] = data.internalTemp;
  doc["externalTemp"] = data.externalTemp;
  doc["protectionStatus"] = data.protectionStatus;
  
  // Device settings
  doc["tempCelsius"] = data.tempCelsius;
  doc["backlight"] = data.backlight;
  doc["sleepTimeout"] = data.sleepTimeout;
  doc["slaveAddress"] = data.slaveAddress;
  doc["baudRateCode"] = data.baudRateCode;
  doc["beeper"] = data.beeper;
  doc["memoryGroup"] = data.memoryGroup;
  doc["mpptEnabled"] = data.mpptEnabled;
  doc["mpptThreshold"] = data.mpptThreshold;
  doc["batteryCutoff"] = data.batteryCutoff;
  doc["outputOnAtStartup"] = data.outputOnAtStartup;
  doc["etp"] = data.etp;
  
  // Battery charging / output-off settings
  doc["bchEnabled"] = data.bchEnabled;
  doc["bchThreshold"] = data.bchThreshold;
  doc["btfEnabled"] = data.btfEnabled;
  doc["btfCutoff"] = data.btfCutoff;
  doc["clofEnabled"] = data.clofEnabled;

  // Host / WiFi module info
  doc["hostType"] = data.hostType;
  doc["wifiConfig"] = data.wifiConfig;
  doc["wifiStatus"] = data.wifiStatus;
  doc["ipv4"] = data.ipv4;
  
  // Extended protections
  doc["ohpHours"] = data.ohpHours;
  doc["ohpMinutes"] = data.ohpMinutes;
  doc["overAmpHours"] = data.overAmpHours;
  doc["overWattHours"] = data.overWattHours;
  
  const char* modeCode;
  float setValue;
  switch (data.operatingMode) {
    case MODE_CV:
      modeCode = "CV";
      setValue = data.voltageSet;
      break;
    case MODE_CC:
      modeCode = "CC";
      setValue = data.currentSet;
      break;
    case MODE_CP:
      modeCode = "CP";
      setValue = data.powerSet;
      break;
    default:
      modeCode = "Unknown";
      setValue = 0.0;
  }
  doc["operatingMode"] = modeCode;
  doc["setValue"] = setValue;
  
  doc["voltageSet"] = data.voltageSet;
  doc["currentSet"] = data.currentSet;
  doc["cpModeEnabled"] = data.cpModeEnabled;
  doc["powerSet"] = data.powerSet;
  
  doc["inputVoltage"] = data.inputVoltage;
  
  doc["lvp"] = data.lvp;
  doc["ovp"] = data.ovp;
  doc["ocp"] = data.ocp;
  doc["opp"] = data.opp;
  doc["otp"] = data.otp;
  
  DeviceConfig config = getConfig();
  doc["deviceName"] = config.deviceName;
  
  doc["model"] = data.model;
  doc["version"] = data.version;
  doc["keyLockEnabled"] = data.keyLocked;
  
  String response;
  serializeJson(doc, response);
  return response;
}

// Add a unified function to fetch complete PSU status
void sendCompletePSUStatus(AsyncWebSocketClient* client) {
  if (!client || !powerSupply) {
    return;
  }
  
  PSUStatusData data;
  if (!readPSUStatusBatched(data)) {
    data.valid = false;
  }
  
  String response = buildStatusJSON(data);
  client->text(response);
}

// Poll the PSU and push fresh status to all connected WebSocket clients.
// Called periodically from loop(). Does nothing when nobody is connected.
// Only broadcasts when the status actually changed - identical polls are
// skipped to avoid flooding the network with redundant messages.
static String lastResponse;

void forceStatusBroadcast() {
  lastResponse = "";
}

void pollAndBroadcastPSUStatus() {
  if (!powerSupply) return;
  if (ws.count() == 0) return;  // Nobody listening - skip Modbus traffic
  
  PSUStatusData data;
  String response;
  
  if (!readPSUStatusBatched(data)) {
    // Device offline - notify clients once instead of spamming the same error
    DynamicJsonDocument doc(128);
    doc["action"] = "statusResponse";
    doc["connected"] = false;
    serializeJson(doc, response);
  } else {
    response = buildStatusJSON(data);
  }
  
  // Only push when the payload differs from the last broadcast
  if (response == lastResponse) {
    return;
  }
  lastResponse = response;
  ws.textAll(response);
}

// Claim the PSU host block as the WiFi master and keep it alive. The PSU drops
// the module's address and blinks in pairing mode if the host block goes stale,
// so we always write MASTER(0x3B3A) + WIFI-STATUS + IP every second.
// WIFI-STATUS=5 is the "connected to server" state the OEM XY-WFPOW module
// writes (seen live on the bus); status=0 while WiFi is down. Never touch
// WIFI-CONFIG (0x0031) - that lets the PSU's own conf NONE/AP/TOUCH setting be
// applied and kept by the user.
void wifiModuleKeepAlive() {
  if (!powerSupply) return;
  IPAddress wifi = WiFi.localIP();
  uint32_t ipv4 = ((uint32_t)wifi[0] << 24) | ((uint32_t)wifi[1] << 16) |
                  ((uint32_t)wifi[2] << 8) | wifi[3];
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (!connected) ipv4 = 0;

  uint16_t master = 0x3B3A;
  uint16_t tail[3] = { connected ? 0x0005 : 0x0000,
                       (uint16_t)((ipv4 >> 16) & 0xFFFF),
                       (uint16_t)(ipv4 & 0xFFFF) };
  lockModbus();
  bool w1 = powerSupply->writeRegister(REG_MASTER, master);
  bool w2 = powerSupply->writeRegisters(REG_WIFI_STATUS, 3, tail);
  if (!w1 || !w2) {
    LOG_ERROR("WiFi host keep-alive write failed");
  }
  unlockModbus();
}

// Sync the PSU RTC/weather block exactly like the OEM XY-WFPOW module does:
// fn 0x10, addr 0x0200, 21 registers (0x2A bytes):
//   reg0/reg1 = Unix epoch seconds split low/high 16-bit words
//   reg2      = sync status (3 = time synced)
//   reg3+     = weather (all zero until we implement a weather source)
void syncRtcWeatherToPSU() {
  if (!powerSupply) return;

  time_t now = time(nullptr);
  if (now < 1000000000) return; // NTP not synced yet, don't push garbage

  // configTime() only sets the TZ env var - time() stays UTC. The PSU
  // screensaver shows the raw epoch as wall clock, so push LOCAL time.
  uint32_t t = (uint32_t)(now + gmtOffset_sec + daylightOffset_sec);
  uint16_t regs[21] = {0};
  regs[0] = t & 0xFFFF;          // low 16 bits
  regs[1] = (t >> 16) & 0xFFFF;  // high 16 bits
  regs[2] = 0x0003;              // status: time synced

  lockModbus();
  bool ok = powerSupply->writeRegisters(REG_RTC_TIME_LO, 21, regs);
  if (!ok) {
    LOG_ERROR("RTC/weather block write failed");
  }
  unlockModbus();
}

// Function to specifically send operating mode details
void sendOperatingModeDetails(AsyncWebSocketClient* client) {
  if (!client || !powerSupply) {
    return;
  }
  
  lockModbus();
  if (!powerSupply->testConnection()) {
    unlockModbus();
    return;
  }
  
  DynamicJsonDocument responseDoc(256);
  responseDoc["action"] = "operatingModeResponse";
  
  // Get the operating mode - using the backend cache
  OperatingMode mode = powerSupply->getOperatingMode(true);
  String modeCode, modeName;
  float setValue = 0.0;
  
  switch (mode) {
    case MODE_CV:
      modeCode = "CV";
      modeName = "Constant Voltage";
      setValue = powerSupply->getCachedConstantVoltage(false);
      break;
    case MODE_CC:
      modeCode = "CC";
      modeName = "Constant Current";
      setValue = powerSupply->getCachedConstantCurrent(false);
      break;
    case MODE_CP:
      modeCode = "CP";
      modeName = "Constant Power";
      setValue = powerSupply->getCachedConstantPower(false);
      break;
    default:
      modeCode = "Unknown";
      modeName = "Unknown";
  }
  
  responseDoc["success"] = true;
  responseDoc["modeCode"] = modeCode;
  responseDoc["modeName"] = modeName;
  responseDoc["setValue"] = setValue;
  
  // Add detailed settings for all modes
  responseDoc["voltageSet"] = powerSupply->getCachedConstantVoltage(false);
  responseDoc["currentSet"] = powerSupply->getCachedConstantCurrent(false);
  
  // Check if CP mode is enabled and get its set value
  bool cpModeEnabled = powerSupply->isConstantPowerModeEnabled(false);
  responseDoc["cpModeEnabled"] = cpModeEnabled;
  if (cpModeEnabled) {
    responseDoc["powerSet"] = powerSupply->getCachedConstantPower(false);
  }
  
  String response;
  serializeJson(responseDoc, response);
  client->text(response);
  unlockModbus();
}

// Add function to read key lock status from PSU
bool isPSUKeyLocked(XY_SKxxx* powerSupply) {
  if (!powerSupply) return false;
  
  lockModbus();
  // Force refresh the key lock status to get latest value
  // This is important to detect changes made on the physical device
  bool locked = powerSupply->isKeyLocked(true);
  unlockModbus();
  return locked;
}

// Add dedicated key lock status handler
void handleKeyLockRequest(AsyncWebSocketClient* client) {
  XY_SKxxx* psu = powerSupply;
  if (!psu) {
    DynamicJsonDocument doc(256);
    doc["action"] = "keyLockStatusResponse";
    doc["success"] = false;
    doc["error"] = "No PSU connected";
    String response;
    serializeJson(doc, response);
    client->text(response);
    return;
  }
  
  DynamicJsonDocument doc(256);
  doc["action"] = "keyLockStatusResponse";
  doc["success"] = true;
  
  // Force refresh to get current state
  lockModbus();
  bool isLocked = psu->isKeyLocked(true);
  unlockModbus();
  doc["locked"] = isLocked;
  
  String response;
  serializeJson(doc, response);
  client->text(response);
}

// Change parameter type to const JsonObject& to fix the binding error
void handleSetKeyLock(AsyncWebSocketClient* client, const JsonObject &json) {
  XY_SKxxx* psu = powerSupply;
  if (!psu) {
    DynamicJsonDocument doc(256);
    doc["action"] = "setKeyLockResponse";
    doc["success"] = false;
    doc["error"] = "No PSU connected";
    String response;
    serializeJson(doc, response);
    client->text(response);
    return;
  }
  
  bool lock = json["lock"] | false;
  lockModbus();
  bool success = psu->setKeyLock(lock);
  
  // Always return the actual current state (which may differ if the operation failed)
  bool isLocked = psu->isKeyLocked(true);
  unlockModbus();
  
  DynamicJsonDocument doc(256);
  doc["action"] = "setKeyLockResponse";
  doc["success"] = success;
  doc["locked"] = isLocked;
  
  String response;
  serializeJson(doc, response);
  client->text(response);
}

// Add this function to handle the WiFi network addition
void handleAddWifiNetworkWebSocketCommand(AsyncWebSocketClient* client, DynamicJsonDocument& doc) {
    if (!doc.containsKey("ssid") || !doc.containsKey("password")) {
        DynamicJsonDocument responseDoc(128);
        responseDoc["action"] = "addWifiNetworkResponse";
        responseDoc["success"] = false;
        responseDoc["error"] = "Missing SSID or password";
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        return;
    }
    
    String ssid = doc["ssid"].as<String>();
    String password = doc["password"].as<String>();
    int priority = doc.containsKey("priority") ? doc["priority"].as<int>() : 1;
    
    // Get saved networks JSON - use the same logic as the serial interface
    Preferences prefs;
    String wifiListJson;
    
    if (prefs.begin(WIFI_NAMESPACE, true)) { // Read-only mode first
        wifiListJson = prefs.getString(WIFI_CREDENTIALS_KEY, "[]");
        prefs.end();
    } else {
        DynamicJsonDocument responseDoc(128);
        responseDoc["action"] = "addWifiNetworkResponse";
        responseDoc["success"] = false;
        responseDoc["error"] = "Failed to access saved WiFi information";
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        return;
    }
    
    // Parse existing JSON
    DynamicJsonDocument wifiDoc(WIFI_CREDENTIALS_JSON_SIZE);
    DeserializationError error = deserializeJson(wifiDoc, wifiListJson);
    
    if (error) {
        Serial.println("Error parsing saved WiFi networks. Creating new list.");
        wifiDoc.clear();
        wifiDoc = JsonArray(); // Ensure it's initialized as an array
    }
    
    // Check if this network already exists
    JsonArray networks = wifiDoc.as<JsonArray>();
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
    serializeJson(wifiDoc, updatedJson);
    
    // Debug output
    Serial.print("WiFi credentials JSON size: ");
    Serial.println(updatedJson.length());
    Serial.print("WiFi credentials JSON content: ");
    Serial.println(updatedJson);
    
    // Save back to NVS
    bool success = false;
    if (prefs.begin(WIFI_NAMESPACE, false)) { // Write mode
        success = prefs.putString(WIFI_CREDENTIALS_KEY, updatedJson);
        prefs.end();
    }
    
    // Send response
    DynamicJsonDocument responseDoc(128);
    responseDoc["action"] = "addWifiNetworkResponse";
    responseDoc["success"] = success;
    responseDoc["ssid"] = ssid;
    
    String response;
    serializeJson(responseDoc, response);
    
    client->text(response);
    
    // Log success/failure
    if (success) {
        Serial.println("WiFi credentials saved successfully from WebSocket request");
    } else {
        Serial.println("Failed to save WiFi credentials from WebSocket request");
    }
}

// Handle device setting/protection commands from the web UI.
// Runs inside handleWebSocketMessage with powerSupply already verified non-null.
void handleDeviceSettingAction(AsyncWebSocketClient* client, const String& action, DynamicJsonDocument& doc) {
  bool success = false;
  String responseAction;
  
  if (action == "setProtection") {
    String key = doc["key"].as<String>();
    float value = doc["value"] | 0.0f;
    
    lockModbus();
    if (key == "lvp")       success = powerSupply->setLowVoltageProtection(value);
    else if (key == "ovp")  success = powerSupply->setOverVoltageProtection(value);
    else if (key == "ocp")  success = powerSupply->setOverCurrentProtection(value);
    else if (key == "opp")  success = powerSupply->setOverPowerProtection(value);
    else if (key == "otp")  success = powerSupply->setOverTemperatureProtection(value);
    unlockModbus();
    
    DynamicJsonDocument responseDoc(256);
    responseDoc["action"] = "setProtectionResponse";
    responseDoc["success"] = success;
    responseDoc["key"] = key;
    responseDoc["value"] = value;
    String response;
    serializeJson(responseDoc, response);
    client->text(response);
    LOG_WS(client->remoteIP(), WiFi.localIP(), "WebSocket sent: " + response);
    return;
  }
  
  if (action == "setBacklight") {
    uint8_t level = doc["level"] | 5;
    lockModbus();
    success = powerSupply->setBacklightBrightness(level);
    unlockModbus();
    responseAction = "setBacklightResponse";
  }
  else if (action == "setSleepTimeout") {
    uint8_t minutes = doc["minutes"] | 2;
    lockModbus();
    success = powerSupply->setSleepTimeout(minutes);
    unlockModbus();
    responseAction = "setSleepTimeoutResponse";
  }
  else if (action == "setSlaveAddress") {
    uint8_t address = doc["address"] | 1;
    lockModbus();
    success = powerSupply->setSlaveAddress(address);
    unlockModbus();
    responseAction = "setSlaveAddressResponse";
  }
  else if (action == "setBaudRate") {
    uint8_t code = doc["code"] | 6;
    lockModbus();
    success = powerSupply->setBaudRate(code);
    unlockModbus();
    responseAction = "setBaudRateResponse";
  }
  else if (action == "setTempUnit") {
    bool celsius = doc["celsius"] | true;
    lockModbus();
    success = powerSupply->setTemperatureUnit(celsius);
    unlockModbus();
    responseAction = "setTempUnitResponse";
  }
  else if (action == "setBeeper") {
    bool enabled = doc["enabled"] | false;
    lockModbus();
    success = powerSupply->setBeeper(enabled);
    unlockModbus();
    responseAction = "setBeeperResponse";
  }
  else if (action == "setMppt") {
    bool enable = doc["enable"] | false;
    float threshold = doc["threshold"] | 0.80f;
    lockModbus();
    success = powerSupply->setMPPTEnable(enable);
    if (success && threshold > 0) success = powerSupply->setMPPTThreshold(threshold);
    unlockModbus();
    responseAction = "setMpptResponse";
  }
  else if (action == "setBatteryCutoff") {
    float current = doc["current"] | 0.0f;
    lockModbus();
    success = powerSupply->setBatteryCutoffCurrent(current);
    unlockModbus();
    responseAction = "setBatteryCutoffResponse";
  }
  else if (action == "setBch") {
    bool enabled = doc["enabled"] | false;
    float threshold = doc["threshold"] | 0.0f;
    lockModbus();
    success = powerSupply->setBatteryChargingEnable(enabled);
    if (success && threshold > 0) success = powerSupply->setBatteryChargingThreshold(threshold);
    unlockModbus();
    responseAction = "setBchResponse";
  }
  else if (action == "setBtfEnable") {
    bool enabled = doc["enabled"] | false;
    lockModbus();
    success = powerSupply->setBatteryCutoffEnable(enabled);
    unlockModbus();
    responseAction = "setBtfEnableResponse";
  }
  else if (action == "setBtfCutoff") {
    // Writes the 0x002C BTF cutoff current (separate from legacy 0x001F BatFul)
    float current = doc["current"] | 0.0f;
    lockModbus();
    success = powerSupply->setBatteryCutoffCurrentBtf(current);
    unlockModbus();
    responseAction = "setBtfCutoffResponse";
  }
  else if (action == "setClof") {
    bool enabled = doc["enabled"] | false;
    lockModbus();
    success = powerSupply->setOutputOffOnGroupChange(enabled);
    unlockModbus();
    responseAction = "setClofResponse";
  }
  else if (action == "setPowerOnInit") {
    bool enabled = doc["enabled"] | false;
    lockModbus();
    success = powerSupply->setPowerOnInitialization(enabled);
    unlockModbus();
    responseAction = "setPowerOnInitResponse";
  }
  else if (action == "setCpMode") {
    bool enabled = doc["enabled"] | false;
    lockModbus();
    success = powerSupply->setConstantPowerMode(enabled);
    unlockModbus();
    responseAction = "setCpModeResponse";
  }
  else if (action == "setOhp") {
    uint16_t hours = doc["hours"] | 0;
    uint16_t minutes = doc["minutes"] | 0;
    lockModbus();
    success = powerSupply->setHighPowerProtectionTime(hours, minutes);
    unlockModbus();
    responseAction = "setOhpResponse";
  }
  else if (action == "setOha") {
    float ampHours = doc["ampHours"] | 0.0f;
    uint32_t mAh = (uint32_t)lroundf(ampHours * 1000.0f);
    lockModbus();
    success = powerSupply->setOverAmpHourProtection((uint16_t)(mAh & 0xFFFF), (uint16_t)(mAh >> 16));
    unlockModbus();
    responseAction = "setOhaResponse";
  }
  else if (action == "setOwh") {
    float wattHours = doc["wattHours"] | 0.0f;
    uint32_t tWh = (uint32_t)lroundf(wattHours * 100.0f);
    lockModbus();
    success = powerSupply->setOverWattHourProtection((uint16_t)(tWh & 0xFFFF), (uint16_t)(tWh >> 16));
    unlockModbus();
    responseAction = "setOwhResponse";
  }
  else if (action == "setMemoryGroup") {
    uint8_t group = doc["group"] | 0;
    lockModbus();
    // callMemoryGroup skips M0 assuming it is already active, but a write of 0
    // is still required to actively recall group M0 on some units.
    if (group == 0) {
      success = powerSupply->writeRegister(REG_EXTRACT_M, 0);
    } else {
      success = powerSupply->callMemoryGroup(static_cast<xy_sk::MemoryGroup>(group));
    }
    unlockModbus();
    responseAction = "setMemoryGroupResponse";
  }
  else if (action == "getMemoryGroup") {
    uint8_t group = doc["group"] | 0;
    // Each group M0-M9 is 15 registers at 0050H + group*0010H (same layout as the working window)
    uint16_t buf[15];
    bool ok = false;
    lockModbus();
    ok = powerSupply->readRegisters(REG_CV_SET + ((uint16_t)group * 0x0010u), 15, buf);
    unlockModbus();
    DynamicJsonDocument responseDoc(512);
    responseDoc["action"] = "memoryGroupData";
    responseDoc["group"] = group;
    responseDoc["success"] = ok;
    if (ok) {
      responseDoc["voltageSet"] = buf[0] / 100.0f;
      responseDoc["currentSet"] = buf[1] / 1000.0f;
      responseDoc["lvp"] = buf[2] / 100.0f;
      responseDoc["ovp"] = buf[3] / 100.0f;
      responseDoc["ocp"] = buf[4] / 1000.0f;
      responseDoc["opp"] = buf[5] / 10.0f;
      responseDoc["ohpHours"] = buf[6];
      responseDoc["ohpMinutes"] = buf[7];
      uint32_t oah = (uint32_t)buf[8] | ((uint32_t)buf[9] << 16);
      responseDoc["overAmpHours"] = oah / 1000.0f;
      uint32_t owh = (uint32_t)buf[10] | ((uint32_t)buf[11] << 16);
      responseDoc["overWattHours"] = owh * 0.01f;
      responseDoc["otp"] = buf[12] / 10.0f;
      responseDoc["outputOnAtStartup"] = (buf[13] & 0x0001) != 0;
      responseDoc["etp"] = buf[14] / 10.0f;
    }
    String response;
    serializeJson(responseDoc, response);
    client->text(response);
    LOG_WS(client->remoteIP(), WiFi.localIP(), "WebSocket sent: " + response);
    return;
  }
  else if (action == "saveMemoryGroup") {
    uint8_t group = doc["group"] | 0;
    uint16_t buf[15];
    bool ok = false;
    lockModbus();
    // Read-modify-write so fields not sent by the client are preserved
    ok = powerSupply->readRegisters(REG_CV_SET + ((uint16_t)group * 0x0010u), 15, buf);
    if (ok) {
      if (doc.containsKey("lvp")) buf[2] = (uint16_t)lroundf((doc["lvp"] | 0.0f) * 100.0f);
      if (doc.containsKey("ovp")) buf[3] = (uint16_t)lroundf((doc["ovp"] | 0.0f) * 100.0f);
      if (doc.containsKey("ocp")) buf[4] = (uint16_t)lroundf((doc["ocp"] | 0.0f) * 1000.0f);
      if (doc.containsKey("opp")) buf[5] = (uint16_t)lroundf((doc["opp"] | 0.0f) * 10.0f);
      if (doc.containsKey("ohpHours")) buf[6] = (uint16_t)(doc["ohpHours"] | 0);
      if (doc.containsKey("ohpMinutes")) buf[7] = (uint16_t)(doc["ohpMinutes"] | 0);
      if (doc.containsKey("overAmpHours")) {
        uint32_t oah = (uint32_t)lroundf((doc["overAmpHours"] | 0.0f) * 1000.0f);
        buf[8] = oah & 0xFFFF;
        buf[9] = (oah >> 16) & 0xFFFF;
      }
      if (doc.containsKey("overWattHours")) {
        uint32_t owh = (uint32_t)lroundf((doc["overWattHours"] | 0.0f) * 100.0f);
        buf[10] = owh & 0xFFFF;
        buf[11] = (owh >> 16) & 0xFFFF;
      }
      if (doc.containsKey("otp")) buf[12] = (uint16_t)lroundf((doc["otp"] | 0.0f) * 10.0f);
      if (doc.containsKey("outputOnAtStartup"))
        buf[13] = (buf[13] & 0xFFFE) | ((doc["outputOnAtStartup"] | false) ? 1 : 0);
      if (doc.containsKey("etp")) buf[14] = (uint16_t)lroundf((doc["etp"] | 0.0f) * 10.0f);
      ok = powerSupply->writeRegisters(REG_CV_SET + ((uint16_t)group * 0x0010u), 15, buf);
    }
    unlockModbus();
    responseAction = "saveMemoryGroupResponse";
  }
  else if (action == "psuReset") {
    lockModbus();
    success = powerSupply->restoreFactoryDefaults();
    unlockModbus();
    responseAction = "psuResetResponse";
  }
  else if (action == "clearProtection") {
    lockModbus();
    success = powerSupply->writeRegister(REG_PROTECT, 0x0000);
    unlockModbus();
    responseAction = "clearProtectionResponse";
  }
  else {
    return; // Unknown action - do nothing
  }
  
  DynamicJsonDocument responseDoc(128);
  responseDoc["action"] = responseAction;
  responseDoc["success"] = success;
  String response;
  serializeJson(responseDoc, response);
  client->text(response);
  LOG_WS(client->remoteIP(), WiFi.localIP(), "WebSocket sent: " + response);
  // Fresh status will be broadcast by the loop() poller
}

void handleWebSocketMessage(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                           AwsFrameInfo* info, uint8_t* data, size_t len) {
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = String((char*)data);
    
    // Enhanced logging with IP address information
    IPAddress clientIP = client->remoteIP();
    IPAddress serverIP = WiFi.localIP();
    LOG_WS(clientIP, serverIP, "WebSocket received: " + message);
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      LOG_ERROR("deserializeJson() failed: " + String(error.c_str()));
      return;
    }
    
    String action = doc["action"];
    
    // Add ping response handler
    if (action == "ping") {
        // Simply respond with a pong message
        client->text("{\"action\":\"pong\"}");
        LOG_WS(serverIP, clientIP, "WebSocket sent: {\"action\":\"pong\"}");
        return;
    }
    
    if (action == "getData") {
      // Simply call the comprehensive status function instead of duplicating the logic
      sendCompletePSUStatus(client);
    } 
    else if (action == "setConfig") {
      // Handle configuration settings
      client->text("{\"status\":\"success\",\"message\":\"Configuration updated\"}");
    }
    // Power supply control commands
    else if (action == "powerOutput") {
      // Toggle power output on/off - ensure we get the correct current state first
      if (powerSupply && powerSupply->testConnection()) {
        bool enable = doc["enable"];
        LOG_INFO("Power output command received. Setting output to: " + String(enable ? "ON" : "OFF"));
        
        lockModbus();
        bool success = setPSUOutput(powerSupply, enable);
        
        // Wait a moment for the command to take effect
        delay(100);
        
        // Get current status after change
        bool outputEnabled = isPSUOutputEnabled(powerSupply);
        unlockModbus();
        
        LOG_INFO("Output status after command: " + String(outputEnabled ? "ON" : "OFF"));
        
        // Send response
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "powerOutputResponse";
        responseDoc["success"] = success;
        responseDoc["enabled"] = outputEnabled;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      } else {
        String errorMsg = "{\"action\":\"powerOutputResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    else if (action == "setVoltage") {
      // Set voltage
      if (powerSupply && powerSupply->testConnection()) {
        float voltage = doc["voltage"];
        lockModbus();
        bool success = powerSupply->setVoltage(voltage);
        
        // Read current settings after change
        float v = getPSUVoltage(powerSupply);
        unlockModbus();
        
        // Send response
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "setVoltageResponse";
        responseDoc["success"] = success;
        responseDoc["voltage"] = v;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      } else {
        String errorMsg = "{\"action\":\"setVoltageResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    else if (action == "setCurrent") {
      // Set current
      if (powerSupply && powerSupply->testConnection()) {
        float current = doc["current"];
        lockModbus();
        bool success = powerSupply->setCurrent(current);
        
        // Read current settings after change
        float c = getPSUCurrent(powerSupply);
        unlockModbus();
        
        // Send response
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "setCurrentResponse";
        responseDoc["success"] = success;
        responseDoc["current"] = c;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      } else {
        String errorMsg = "{\"action\":\"setCurrentResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    else if (action == "setPower") {
      // Set constant power (CP mode)
      if (powerSupply && powerSupply->testConnection()) {
        float power = doc["power"];
        lockModbus();
        bool success = powerSupply->setConstantPower(power);
        unlockModbus();
        
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "setPowerResponse";
        responseDoc["success"] = success;
        responseDoc["power"] = power;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      } else {
        String errorMsg = "{\"action\":\"setPowerResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    else if (action == "getStatus") {
      // No need for duplicate code, just call the unified function
      sendCompletePSUStatus(client);
    }
    // Key lock control
    else if (action == "setKeyLock") {
      if (powerSupply && powerSupply->testConnection()) {
        bool lock = doc["lock"];
        LOG_INFO("Key lock command received. Setting keys to: " + String(lock ? "LOCKED" : "UNLOCKED"));
        
        lockModbus();
        bool success = powerSupply->setKeyLock(lock);
        
        // Get current status after change
        bool keyLocked = powerSupply->isKeyLocked(true);
        unlockModbus();
        
        // Send response
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "keyLockResponse";
        responseDoc["success"] = success;
        responseDoc["locked"] = keyLocked;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      } else {
        String errorMsg = "{\"action\":\"keyLockResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    // Constant Voltage mode
    else if (action == "setConstantVoltage") {
      if (powerSupply && powerSupply->testConnection()) {
        float voltage = doc["voltage"];
        lockModbus();
        bool success = powerSupply->setConstantVoltage(voltage);
        unlockModbus();
        
        // Send response
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "constantVoltageResponse";
        responseDoc["success"] = success;
        responseDoc["voltage"] = voltage;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
        
        // Fresh status is broadcast to all clients by the loop() poller
      } else {
        String errorMsg = "{\"action\":\"constantVoltageResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    // Constant Current mode
    else if (action == "setConstantCurrent") {
      if (powerSupply && powerSupply->testConnection()) {
        float current = doc["current"];
        lockModbus();
        bool success = powerSupply->setConstantCurrent(current);
        unlockModbus();
        
        // Send response
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "constantCurrentResponse";
        responseDoc["success"] = success;
        responseDoc["current"] = current;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
        
        // Fresh status is broadcast to all clients by the loop() poller
      } else {
        String errorMsg = "{\"action\":\"constantCurrentResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    // Constant Power mode
    else if (action == "setConstantPower") {
      if (powerSupply && powerSupply->testConnection()) {
        float power = doc["power"];
        lockModbus();
        bool success = powerSupply->setConstantPower(power);
        unlockModbus();
        
        // Send response
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "constantPowerResponse";
        responseDoc["success"] = success;
        responseDoc["power"] = power;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
        
        // Fresh status is broadcast to all clients by the loop() poller
      } else {
        String errorMsg = "{\"action\":\"constantPowerResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    // Constant Power mode toggle
    else if (action == "setConstantPowerMode") {
      if (powerSupply && powerSupply->testConnection()) {
        bool enable = doc["enable"];
        lockModbus();
        bool success = powerSupply->setConstantPowerMode(enable);
        
        // Get current state after change
        bool isEnabled = powerSupply->isConstantPowerModeEnabled(true);
        unlockModbus();
        
        // Send response
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "constantPowerModeResponse";
        responseDoc["success"] = success;
        responseDoc["enabled"] = isEnabled;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
        
        // Fresh status is broadcast to all clients by the loop() poller
      } else {
        String errorMsg = "{\"action\":\"constantPowerModeResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
        client->text(errorMsg);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + errorMsg);
      }
    }
    // Add a specific action to get operating mode details
    else if (action == "getOperatingMode") {
      sendOperatingModeDetails(client);
    }
    // Add a comprehensive status request action
    else if (action == "getStatus") {
      sendCompletePSUStatus(client);
    }
    // Add a WebSocket handler for WiFi status
    else if (action == "getWifiStatus") {
        // Get WiFi status
        String wifiStatusStr = getWifiStatus(); // This returns a JSON string

        // Parse the JSON string to extract the data
        DynamicJsonDocument wifiDoc(512);
        deserializeJson(wifiDoc, wifiStatusStr);
        
        // Create a response using the direct format
        DynamicJsonDocument responseDoc(512);
        responseDoc["action"] = "wifiStatusResponse";
        responseDoc["status"] = wifiDoc["status"];
        responseDoc["ssid"] = wifiDoc["ssid"];
        responseDoc["ip"] = wifiDoc["ip"];
        responseDoc["rssi"] = wifiDoc["rssi"];
        responseDoc["mac"] = wifiDoc["mac"];

        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        Serial.print("WebSocket sent: "); // Debug print
        Serial.println(response); // Debug print
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
        return;
    }

    if (action == "addWifiNetwork") {
        handleAddWifiNetworkCommand(client, doc);
        return;
    }

    if (action == "loadWifiCredentials") {
        String wifiCredentials = loadWiFiCredentialsFromNVS();
        
        DynamicJsonDocument responseDoc(1024);
        responseDoc["action"] = "loadWifiCredentialsResponse";
        responseDoc["success"] = true;
        responseDoc["credentials"] = serialized(wifiCredentials);
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
        return;
    }

    if (action == "resetWifi") {
        bool success = resetWiFi();
        
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "resetWifiResponse";
        responseDoc["success"] = success;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
        return;
    }
    
    // Handle incoming message...
    if (action == "getKeyLockStatus") {
      handleKeyLockRequest(client);
      return;
    }
    
    // Handle key lock command - Fix this call by using as<JsonObject>()
    if (action == "setKeyLock") {
      // Convert doc to JsonObject to match function parameter
      handleSetKeyLock(client, doc.as<JsonObject>());
      return;
    }
    
    // Add a handler for time zone settings requests
    if (action == "getTimeZones") {
      String timeZones = getAvailableTimeZones();
      String currentTZ = getCurrentTimeZone();
      
      DynamicJsonDocument responseDoc(1024);
      responseDoc["action"] = "timeZonesResponse";
      responseDoc["timeZones"] = serialized(timeZones);
      responseDoc["current"] = serialized(currentTZ);
      
      String response;
      serializeJson(responseDoc, response);
      client->text(response);
      LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      return;
    }
    
    // Handle time zone setting changes
    if (action == "setTimeZone") {
      int tzIndex = doc["index"];
      
      bool success = setTimeZoneByIndex(tzIndex);
      
      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "setTimeZoneResponse";
      responseDoc["success"] = success;
      
      // If successful, include the new time zone info
      if (success) {
        responseDoc["timeZone"] = serialized(getCurrentTimeZone());
      }
      
      String response;
      serializeJson(responseDoc, response);
      client->text(response);
      LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      return;
    }
    
    // Handle save wifi credentials request
    if (action == "saveWifiCredentials") {
      String ssid = doc["ssid"];
      String password = doc["password"];

      bool success = saveWiFiCredentialsToNVS(ssid, password); // Use the new function

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "saveWifiCredentialsResponse";
      responseDoc["success"] = success;

      String response;
      serializeJson(responseDoc, response);
      client->text(response);
      LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      return;
    }

    // Handle load wifi credentials request
    if (action == "loadWifiCredentials") {
      String wifiCredentials = loadWiFiCredentialsFromNVS(); // Use the new function

      DynamicJsonDocument responseDoc(WIFI_CREDENTIALS_JSON_SIZE);
      responseDoc["action"] = "loadWifiCredentialsResponse";
      responseDoc["wifiCredentials"] = wifiCredentials;

      String response;
      serializeJson(responseDoc, response);
      client->text(response);
      LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      return;
    }
    
    // Handle reset wifi request
    if (action == "resetWifi") {
      bool success = resetWiFi(); // Use the new function

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "resetWifiResponse";
      responseDoc["success"] = success;

      String response;
      serializeJson(responseDoc, response);
      client->text(response);
      LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      return;
    }
    
    // Handle get wifi status request
    if (action == "getWifiStatus") {
      String wifiStatus = getWifiStatus();

      DynamicJsonDocument responseDoc(512);
      responseDoc["action"] = "wifiStatusResponse";
      responseDoc["wifiStatus"] = wifiStatus;

      String response;
      serializeJson(responseDoc, response);
      client->text(response);
      Serial.print("WebSocket sent: "); // Debug print
      Serial.println(response); // Debug print
      LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
      return;
    }

    if (action == "removeWifiNetwork") {
        handleRemoveWifiNetworkCommand(client, doc);
        return;
    }

    if (action == "updateWifiPriority") {
        int index = doc["index"];
        int newPriority = doc["priority"];
        
        Serial.print("Received WiFi priority update request: index=");
        Serial.print(index);
        Serial.print(", newPriority=");
        Serial.println(newPriority);
        
        // Try to update the priority
        bool success = updateWiFiNetworkPriority(index, newPriority);
        
        DynamicJsonDocument responseDoc(256);
        responseDoc["action"] = "updateWifiPriorityResponse";
        responseDoc["success"] = success;
        
        if (!success) {
            responseDoc["error"] = "Failed to update network priority";
        }
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
        LOG_WS(serverIP, clientIP, "WebSocket sent: " + response);
        return;
    }

    // Add the new action handler
    if (action == "connectWifi") {
        handleConnectWifiCommand(client, doc);
        return;
    }
    
    // ---- Device settings & protections (ESPHome-style controls) ----
    else if (action == "setProtection" || action == "setBacklight" || action == "setSleepTimeout" ||
             action == "setSlaveAddress" || action == "setBaudRate" || action == "setTempUnit" ||
             action == "setBeeper" || action == "setMppt" || action == "setBatteryCutoff" ||
             action == "setBch" || action == "setBtfEnable" || action == "setBtfCutoff" ||
             action == "setPowerOnInit" || action == "setOhp" || action == "setOha" ||
             action == "setOwh" || action == "setMemoryGroup" || action == "psuReset" ||
             action == "clearProtection" || action == "setCpMode" || action == "getMemoryGroup" ||
             action == "saveMemoryGroup") {
      if (!powerSupply) {
        client->text("{\"action\":\"deviceSettingResponse\",\"success\":false,\"error\":\"Power supply not connected\"}");
        return;
      }
      handleDeviceSettingAction(client, action, doc);
    }
    
    // ESP32 device restart: { action: "restart" }
    else if (action == "restart") {
      client->text("{\"action\":\"restartResponse\",\"success\":true}");
      delay(500);
      ESP.restart();
    }
  }
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
              AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      LOG_INFO("WebSocket client #" + String(client->id()) + " connected from " + client->remoteIP().toString());
      // Ensure the newly connected client gets a fresh status broadcast right
      // away, even if the status hasn't changed since the last poll.
      forceStatusBroadcast();
      break;
    case WS_EVT_DISCONNECT:
      LOG_INFO("WebSocket client #" + String(client->id()) + " disconnected");
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(server, client, (AwsFrameInfo*)arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void setupWebServer(AsyncWebServer* server) {
  // Initialize the Modbus access mutex
  if (modbusMutex == nullptr) {
    modbusMutex = xSemaphoreCreateRecursiveMutex();
  }
  
  // Try to configure NTP for better logging timestamps
  if (WiFi.status() == WL_CONNECTED) {
    configureNTP();
  }
  
  // Wrap in try-catch to handle possible initialization errors
  try {
    // Initialize WebSocket
    ws.onEvent(onWsEvent);
    server->addHandler(&ws);
    
    // Set up CORS headers for all requests
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
    
    // Route for root / web page
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/index.html", "text/html");
    });
    
    // Route to load style.css file
    server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/style.css", "text/css");
    });
    
    // Route to load main.js file
    server->on("/main.js", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/main.js", "application/javascript");
    });
    
    // API endpoints - remove the /api/data endpoint that used dummy sensor data
    server->on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
      DynamicJsonDocument doc(1024);
      
      // Add power supply status information instead of modbus data
      if (powerSupply && powerSupply->testConnection()) {
        float voltage = 0, current = 0, power = 0;
        powerSupply->getOutput(voltage, current, power);
        bool outputEnabled = powerSupply->isOutputEnabled(true);
        
        doc["outputEnabled"] = outputEnabled;
        doc["voltage"] = voltage;
        doc["current"] = current;
        doc["power"] = power;
      }
      
      String jsonString;
      serializeJson(doc, jsonString);
      
      // Simply send the string as a response
      request->send(200, "application/json", jsonString);
    });

    server->on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
      DynamicJsonDocument doc(1024);
      DeviceConfig config = getConfig();
      
      doc["deviceName"] = config.deviceName;
      doc["modbusId"] = config.modbusId;
      doc["baudRate"] = config.baudRate;
      doc["dataBits"] = config.dataBits;
      doc["parity"] = config.parity;
      doc["stopBits"] = config.stopBits;
      doc["updateInterval"] = config.updateInterval;
      
      String jsonString;
      serializeJson(doc, jsonString);
      
      // Send the JSON string directly
      request->send(200, "application/json", jsonString);
    });

    server->on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request){
      // Dummy response for now
      request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved\"}");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      // Handle POST data when it's available
      if (len > 0) {
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (!error) {
          DeviceConfig& config = getConfig();
          
          if (doc.containsKey("deviceName")) strlcpy(config.deviceName, doc["deviceName"], sizeof(config.deviceName));
          if (doc.containsKey("modbusId")) config.modbusId = doc["modbusId"];
          if (doc.containsKey("baudRate")) config.baudRate = doc["baudRate"];
          if (doc.containsKey("dataBits")) config.dataBits = doc["dataBits"];
          if (doc.containsKey("parity")) config.parity = doc["parity"];
          if (doc.containsKey("stopBits")) config.stopBits = doc["stopBits"];
          if (doc.containsKey("updateInterval")) config.updateInterval = doc["updateInterval"];
          
          // Save the updated configuration
          saveConfig();
        }
      }
    });

    // Add an API endpoint for time zone settings
    server->on("/api/timezone", HTTP_GET, [](AsyncWebServerRequest *request){
      String timeZones = getAvailableTimeZones();
      String currentTZ = getCurrentTimeZone();
      
      DynamicJsonDocument doc(1024);
      doc["timeZones"] = serialized(timeZones);
      doc["current"] = serialized(currentTZ);
      
      String jsonString;
      serializeJson(doc, jsonString);
      
      // Add CORS headers
      AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", jsonString);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
      request->send(resp);
    });
    
    server->on("/api/timezone", HTTP_POST, [](AsyncWebServerRequest *request){
      request->send(200, "application/json", "{\"success\":true}");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (len > 0) {
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (!error && doc.containsKey("index")) {
          int tzIndex = doc["index"];
          bool success = setTimeZoneByIndex(tzIndex);
          
          // Respond with success status
          String response = "{\"success\":" + String(success ? "true" : "false") + "}";
          AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", response);
          resp->addHeader("Access-Control-Allow-Origin", "*");
          request->send(resp);
        }
      }
    });

    // Add a simple health check endpoint
    server->on("/health", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/plain", "OK");
    });
    
    // Add a simple health check endpoint that doesn't require AsyncTCP
    server->on("/ping", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/plain", "pong");
    });
    
    LOG_INFO("Web server routes configured successfully");
  } 
  catch (const std::exception& e) {
    LOG_ERROR("Error setting up web server routes");
  }
  
  // Handle file reads
  server->onNotFound([](AsyncWebServerRequest *request){
    if (!handleFileRead(request)) {
      request->send(404, "text/plain", "File Not Found");
    }
  });
}