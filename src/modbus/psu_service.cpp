#include "psu_service.h"
#include <WiFi.h>
#include "log_utils/log_utils.h"
#include "config_manager.h"
#include "wifi_interface/wifi_settings.h"

// Declared in main.cpp
extern XY_SKxxx* powerSupply;

// Mutex protecting Modbus bus access
SemaphoreHandle_t modbusMutex = nullptr;

void initPsuService() {
  if (modbusMutex == nullptr) {
    modbusMutex = xSemaphoreCreateRecursiveMutex();
  }
}

void lockModbus() {
  if (modbusMutex) xSemaphoreTakeRecursive(modbusMutex, portMAX_DELAY);
}

void unlockModbus() {
  if (modbusMutex) xSemaphoreGiveRecursive(modbusMutex);
}

// Helper functions for XY-SKxxx power supply interface

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

// Reads registers while caller holds the Modbus mutex
static bool readPSUStatusBatchedLocked(PSUStatusData& data);

bool readPSUStatusBatched(PSUStatusData& data) {
  if (!powerSupply) return false;

  lockModbus();
  bool ok = readPSUStatusBatchedLocked(data);
  unlockModbus();
  return ok;
}

static bool readPSUStatusBatchedLocked(PSUStatusData& data) {
  uint16_t buf[16];
  bool valid = true;
  float liveVoltageSet = 0, liveCurrentSet = 0;

  // Batch 1: 0x0000 - 0x000E (15 contiguous): V_SET, I_SET, VOUT, IOUT, POWER,
  // UIN, AH_L, AH_H, WH_L, WH_H, OUT_H, OUT_M, OUT_S, T_IN, T_EX
  if (!powerSupply->readRegisters(REG_V_SET, 15, buf)) {
    valid = false;
  } else {
    liveVoltageSet = buf[0] / 100.0f;
    liveCurrentSet = buf[1] / 1000.0f;
    data.voltage = buf[2] / 100.0f;
    data.current = buf[3] / 1000.0f;
    data.power = buf[4] / 100.0f;
    data.inputVoltage = buf[5] / 100.0f;
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
  // BTF_CUTOFF(0x002C), CLOF_ENABLE(0x002D)
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
  // IPV4_H(0x0033), IPV4_L(0x0034)
  if (!powerSupply->readRegisters(0x0030, 5, buf)) {
    valid = false;
  } else {
    data.hostType = buf[0];
    data.wifiConfig = buf[1];
    data.wifiStatus = buf[2];
    data.ipv4 = ((uint32_t)buf[3] << 16) | buf[4];
  }

  if (!valid) return false;

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

String buildStatusJSON(const PSUStatusData& data) {
  DynamicJsonDocument doc(2048);
  doc["action"] = "statusResponse";
  doc["connected"] = data.valid;
  doc["outputEnabled"] = data.outputEnabled;
  doc["voltage"] = data.voltage;
  doc["current"] = data.current;
  doc["power"] = data.power;
  doc["inputVoltage"] = data.inputVoltage;

  doc["ampHours"] = data.ampHours;
  doc["wattHours"] = data.wattHours;
  doc["outputTime"] = data.outputTime;
  doc["internalTemp"] = data.internalTemp;
  doc["externalTemp"] = data.externalTemp;
  doc["protectionStatus"] = data.protectionStatus;

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

  doc["bchEnabled"] = data.bchEnabled;
  doc["bchThreshold"] = data.bchThreshold;
  doc["btfEnabled"] = data.btfEnabled;
  doc["btfCutoff"] = data.btfCutoff;
  doc["clofEnabled"] = data.clofEnabled;

  doc["hostType"] = data.hostType;
  doc["wifiConfig"] = data.wifiConfig;
  doc["wifiStatus"] = data.wifiStatus;
  doc["ipv4"] = data.ipv4;

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

// ---- Command dispatch (former WebSocket action handlers) ----

static String getStatusResponse() {
  PSUStatusData data;
  if (!readPSUStatusBatched(data)) {
    data.valid = false;
  }
  return buildStatusJSON(data);
}

static String fmtError(const String& action) {
  return "{\"action\":\"" + action + "\",\"success\":false,\"error\":\"Power supply not connected\"}";
}

// Handle device setting/protection commands (former handleDeviceSettingAction)
static String handleDeviceSetting(String action, DynamicJsonDocument& doc) {
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
    return response;
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
    return response;
  }
  else if (action == "saveMemoryGroup") {
    uint8_t group = doc["group"] | 0;
    uint16_t buf[15];
    bool ok = false;
    lockModbus();
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
    return ""; // Unknown action - do nothing
  }

  DynamicJsonDocument responseDoc(128);
  responseDoc["action"] = responseAction;
  responseDoc["success"] = success;
  String response;
  serializeJson(responseDoc, response);
  return response;
}

String handleMqttAction(const String& action, const char* payload) {
  DynamicJsonDocument doc(2048);
  if (payload && payload[0]) {
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      return "{\"action\":\"error\",\"success\":false,\"error\":\"Bad JSON\"}";
    }
  } else {
    doc.to<JsonObject>();
  }

  if (action == "ping") {
    return "{\"action\":\"pong\"}";
  }
  if (action == "getData" || action == "getStatus") {
    return getStatusResponse();
  }
  if (action == "getTimeZone") {
    DynamicJsonDocument responseDoc(1024);
    responseDoc["action"] = "timeZoneData";
    responseDoc["timeZones"] = serialized(getAvailableTimeZones());
    responseDoc["current"] = serialized(getCurrentTimeZone());
    String response;
    serializeJson(responseDoc, response);
    return response;
  }
  if (action == "getWifiStatus") {
    DynamicJsonDocument responseDoc(512);
    responseDoc["action"] = "wifiStatusResponse";
    responseDoc["wifiStatus"] = getWifiStatus();
    String response;
    serializeJson(responseDoc, response);
    return response;
  }
  if (action == "addWifiNetwork") {
    String ssid = doc["ssid"] | "";
    String password = doc["password"] | "";
    int priority = doc["priority"] | -1;
    bool success = ssid.length() > 0 && saveWiFiCredentialsToNVS(ssid, password, priority);
    if (success) connectToSavedNetworks();
    DynamicJsonDocument responseDoc(256);
    responseDoc["action"] = "addWifiNetworkResponse";
    responseDoc["success"] = success;
    responseDoc["ssid"] = ssid;
    String response;
    serializeJson(responseDoc, response);
    return response;
  }
  if (action == "setConfig") {
    return "{\"action\":\"setConfigResponse\",\"status\":\"success\"}";
  }
  if (action == "powerOutput") {
    if (powerSupply && powerSupply->testConnection()) {
      bool enable = doc["enable"];
      lockModbus();
      bool success = setPSUOutput(powerSupply, enable);
      delay(100);
      bool outputEnabled = isPSUOutputEnabled(powerSupply);
      unlockModbus();

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "powerOutputResponse";
      responseDoc["success"] = success;
      responseDoc["enabled"] = outputEnabled;
      String response;
      serializeJson(responseDoc, response);
      return response;
    }
    return fmtError("powerOutputResponse");
  }
  if (action == "setVoltage") {
    if (powerSupply && powerSupply->testConnection()) {
      float voltage = doc["voltage"];
      lockModbus();
      bool success = powerSupply->setVoltage(voltage);
      float v = getPSUVoltage(powerSupply);
      unlockModbus();

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "setVoltageResponse";
      responseDoc["success"] = success;
      responseDoc["voltage"] = v;
      String response;
      serializeJson(responseDoc, response);
      return response;
    }
    return fmtError("setVoltageResponse");
  }
  if (action == "setCurrent") {
    if (powerSupply && powerSupply->testConnection()) {
      float current = doc["current"];
      lockModbus();
      bool success = powerSupply->setCurrent(current);
      float c = getPSUCurrent(powerSupply);
      unlockModbus();

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "setCurrentResponse";
      responseDoc["success"] = success;
      responseDoc["current"] = c;
      String response;
      serializeJson(responseDoc, response);
      return response;
    }
    return fmtError("setCurrentResponse");
  }
  if (action == "setPower") {
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
      return response;
    }
    return fmtError("setPowerResponse");
  }
  if (action == "setKeyLock") {
    if (powerSupply && powerSupply->testConnection()) {
      bool lock = doc["lock"];
      lockModbus();
      bool success = powerSupply->setKeyLock(lock);
      bool keyLocked = powerSupply->isKeyLocked(true);
      unlockModbus();

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "keyLockResponse";
      responseDoc["success"] = success;
      responseDoc["locked"] = keyLocked;
      String response;
      serializeJson(responseDoc, response);
      return response;
    }
    return fmtError("keyLockResponse");
  }
  if (action == "setConstantVoltage") {
    if (powerSupply && powerSupply->testConnection()) {
      float voltage = doc["voltage"];
      lockModbus();
      bool success = powerSupply->setConstantVoltage(voltage);
      unlockModbus();

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "constantVoltageResponse";
      responseDoc["success"] = success;
      responseDoc["voltage"] = voltage;
      String response;
      serializeJson(responseDoc, response);
      return response;
    }
    return fmtError("constantVoltageResponse");
  }
  if (action == "setConstantCurrent") {
    if (powerSupply && powerSupply->testConnection()) {
      float current = doc["current"];
      lockModbus();
      bool success = powerSupply->setConstantCurrent(current);
      unlockModbus();

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "constantCurrentResponse";
      responseDoc["success"] = success;
      responseDoc["current"] = current;
      String response;
      serializeJson(responseDoc, response);
      return response;
    }
    return fmtError("constantCurrentResponse");
  }
  if (action == "setConstantPower") {
    if (powerSupply && powerSupply->testConnection()) {
      float power = doc["power"];
      lockModbus();
      bool success = powerSupply->setConstantPower(power);
      unlockModbus();

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "constantPowerResponse";
      responseDoc["success"] = success;
      responseDoc["power"] = power;
      String response;
      serializeJson(responseDoc, response);
      return response;
    }
    return fmtError("constantPowerResponse");
  }
  if (action == "setConstantPowerMode") {
    if (powerSupply && powerSupply->testConnection()) {
      bool enable = doc["enable"];
      lockModbus();
      bool success = powerSupply->setConstantPowerMode(enable);
      bool isEnabled = powerSupply->isConstantPowerModeEnabled(true);
      unlockModbus();

      DynamicJsonDocument responseDoc(256);
      responseDoc["action"] = "constantPowerModeResponse";
      responseDoc["success"] = success;
      responseDoc["enabled"] = isEnabled;
      String response;
      serializeJson(responseDoc, response);
      return response;
    }
    return fmtError("constantPowerModeResponse");
  }
  if (action == "getOperatingMode") {
    if (!powerSupply) return fmtError("operatingModeResponse");

    lockModbus();
    if (!powerSupply->testConnection()) {
      unlockModbus();
      return fmtError("operatingModeResponse");
    }

    DynamicJsonDocument responseDoc(256);
    responseDoc["action"] = "operatingModeResponse";

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
    responseDoc["voltageSet"] = powerSupply->getCachedConstantVoltage(false);
    responseDoc["currentSet"] = powerSupply->getCachedConstantCurrent(false);

    bool cpModeEnabled = powerSupply->isConstantPowerModeEnabled(false);
    responseDoc["cpModeEnabled"] = cpModeEnabled;
    if (cpModeEnabled) {
      responseDoc["powerSet"] = powerSupply->getCachedConstantPower(false);
    }

    String response;
    serializeJson(responseDoc, response);
    unlockModbus();
    return response;
  }
  if (action == "setTimeZone") {
    int tzIndex = doc["index"];
    bool success = setTimeZoneByIndex(tzIndex);
    DynamicJsonDocument responseDoc(256);
    responseDoc["action"] = "setTimeZoneResponse";
    responseDoc["success"] = success;
    if (success) {
      responseDoc["timeZone"] = serialized(getCurrentTimeZone());
    }
    String response;
    serializeJson(responseDoc, response);
    return response;
  }
  if (action == "restart") {
    DynamicJsonDocument responseDoc(128);
    responseDoc["action"] = "restartResponse";
    responseDoc["success"] = true;
    String response;
    serializeJson(responseDoc, response);
    return response;
  }

  // Device settings & protections
  if (action == "setProtection" || action == "setBacklight" || action == "setSleepTimeout" ||
      action == "setSlaveAddress" || action == "setBaudRate" || action == "setTempUnit" ||
      action == "setBeeper" || action == "setMppt" || action == "setBatteryCutoff" ||
      action == "setBch" || action == "setBtfEnable" || action == "setBtfCutoff" ||
      action == "setPowerOnInit" || action == "setOhp" || action == "setOha" ||
      action == "setOwh" || action == "setMemoryGroup" || action == "psuReset" ||
      action == "clearProtection" || action == "setCpMode" || action == "getMemoryGroup" ||
      action == "saveMemoryGroup") {
    if (!powerSupply) {
      return "{\"action\":\"deviceSettingResponse\",\"success\":false,\"error\":\"Power supply not connected\"}";
    }
    return handleDeviceSetting(action, doc);
  }

  return "";
}