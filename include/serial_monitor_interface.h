#ifndef SERIAL_MONITOR_INTERFACE_H
#define SERIAL_MONITOR_INTERFACE_H

#include <Arduino.h>
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h"

// Function to display device status
void displayStatus(XY_SKxxx* powerSupply);

// Function to display configuration
void displayConfig(XYModbusConfig &config);

// Print help message
void printHelp();

// Process a command from the serial monitor
void handleSerialCommand(String command, XY_SKxxx* powerSupply, XYModbusConfig &config);

// Initialize the serial monitor interface
void setupSerialMonitorControl();

// Check for input on the serial monitor
void checkSerialMonitorInput(XY_SKxxx* powerSupply, XYModbusConfig &config);

// Function to handle WiFi settings commands
void handleWifiSettingsCommands();

#endif // SERIAL_MONITOR_INTERFACE_H
