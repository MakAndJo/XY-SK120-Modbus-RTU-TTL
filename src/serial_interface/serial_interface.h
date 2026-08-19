#pragma once

#include <Arduino.h>
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h"

// Menu states for navigation
enum class MenuState {
  MAIN_MENU,
  BASIC_CONTROL,
  MEASUREMENT_MENU,
  PROTECTION_MENU,
  SETTINGS_MENU,
  DEBUG_MENU,
  CD_DATA_MENU,  // Add new menu state for CD Data Groups
  WIFI_MENU      // Add new menu state for WiFi Settings
};

// Helper parsing functions
bool parseFloat(const String& input, float& value);
bool parseUInt8(const String& input, uint8_t& value);
bool parseUInt16(const String& input, uint16_t& value);
bool parseHex(const String& input, uint16_t& value);

// Main interface functions
void setupSerialMonitorControl();
void checkSerialMonitorInput(XY_SKxxx* ps, XYModbusConfig& config);
void displayConfig(XYModbusConfig& config);
