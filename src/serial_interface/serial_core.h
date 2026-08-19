#pragma once

#include <Arduino.h>
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h" // Make sure XYModbusConfig is included
#include "serial_interface.h"

MenuState getCurrentMenuState();
void setMenuState(MenuState state);
void initializeSerialInterface();
void processSerialInput();
void processSerialCommand(const String& input, XY_SKxxx* ps, XYModbusConfig& config);
void displayDeviceInfo(XY_SKxxx* ps);
void displayDeviceStatus(XY_SKxxx* ps);
void displayDeviceProtectionStatus(XY_SKxxx* ps);
