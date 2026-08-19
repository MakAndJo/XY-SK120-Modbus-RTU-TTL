#pragma once

#include <Arduino.h>
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h" // Include this for XYModbusConfig

void handleMainMenu(const String& input, XY_SKxxx* ps, XYModbusConfig& config);

// Display main menu
void displayMainMenu();
