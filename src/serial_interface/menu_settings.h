#pragma once

#include <Arduino.h>
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h" // Include this for XYModbusConfig

// Forward declarations of functions in menu_settings.cpp
void displaySettingsMenu();

void handleSettingsMenu(const String& input, XY_SKxxx* ps, XYModbusConfig& config);

// Display basic device settings
void displayDeviceSettings(XY_SKxxx* ps);

// Display all device settings
void displayAllDeviceSettings(XY_SKxxx* ps);
