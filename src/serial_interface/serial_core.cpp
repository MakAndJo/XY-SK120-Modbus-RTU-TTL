#include "serial_core.h"
#include "menu_main.h"
#include "menu_basic.h"
#include "menu_measurement.h"
#include "menu_protection.h"
#include "menu_settings.h"
#include "menu_debug.h"
#include "menu_cd_data.h"
#include "menu_wifi.h"  // Add this include for WiFi menu functions

// Global variables for serial interface
static MenuState currentMenu = MenuState::MAIN_MENU;
static String serialBuffer = "";
static bool serialInputComplete = false;

MenuState getCurrentMenuState() {
  return currentMenu;
}

void setMenuState(MenuState state) {
  currentMenu = state;
}

void initializeSerialInterface() {
  Serial.println("\n===== XY-SK Power Supply Interface =====");
  Serial.println("Type 'help' for menu, 'status' for current readings, 'prot' for protection settings");
  displayMainMenu();
}

void processSerialInput() {
  static String command = "";
  
  // Check for new serial input
  if (Serial.available()) {
    char c = Serial.read();
    
    // Process command when newline received
    if (c == '\n' || c == '\r') {
      if (command.length() > 0) {
        XYModbusConfig config;
        processSerialCommand(command, nullptr, config);
        command = "";
      }
    } else {
      // Add character to buffer
      command += c;
    }
  }
}

void processSerialCommand(const String& input, XY_SKxxx* ps, XYModbusConfig& config) {
  // Skip empty input
  if (input.length() == 0) {
    return;
  }
  
  Serial.print("Command: ");
  Serial.println(input);
  
  // Global commands that work in any menu
  if (input.equalsIgnoreCase("menu") || input.equalsIgnoreCase("main")) {
    currentMenu = MenuState::MAIN_MENU;
    displayMainMenu();
    return;
  } else if (input.equalsIgnoreCase("info")) {
    displayDeviceInfo(ps);
    return;
  } else if (input.equalsIgnoreCase("help")) {
    switch (currentMenu) {
      case MenuState::MAIN_MENU: displayMainMenu(); break;
      case MenuState::BASIC_CONTROL: displayBasicControlMenu(); break;
      case MenuState::MEASUREMENT_MENU: displayMeasurementMenu(); break;
      case MenuState::PROTECTION_MENU: displayProtectionMenu(); break;
      case MenuState::SETTINGS_MENU: displaySettingsMenu(); break;
      case MenuState::DEBUG_MENU: displayDebugMenu(); break;
      case MenuState::CD_DATA_MENU: displayCDDataMenu(); break;
      case MenuState::WIFI_MENU: displayWiFiMenu(); break;  // Add case for WiFi menu
    }
    return;
  } else if (input.equalsIgnoreCase("status")) {
    displayDeviceStatus(ps);
    return;
  } else if (input.equalsIgnoreCase("prot")) {
    displayDeviceProtectionStatus(ps);
    return;
  } else if (input.equalsIgnoreCase("reset")) {
    // Add factory reset command
    Serial.println("\n==== FACTORY RESET ====");
    Serial.println("WARNING: This will reset ALL device settings to factory defaults!");
    Serial.println("All custom configurations, calibrations, and saved presets will be lost.");
    Serial.println("Type 'y' and press Enter to confirm, or any other key to cancel.");
    Serial.print("Proceed with factory reset? ");
    
    // Clear any pending input
    while (Serial.available()) {
      Serial.read();
    }
    
    // Wait for user confirmation
    bool timeout = true;
    unsigned long startTime = millis();
    
    while (millis() - startTime < 30000) { // 30 second timeout
      if (Serial.available()) {
        char c = Serial.read();
        Serial.print(c); // Echo character
        timeout = false;
        
        if (c == 'y' || c == 'Y') {
          // Consume the rest of the line
          while (Serial.available()) {
            Serial.read();
          }
          
          Serial.println("\n\nExecuting factory reset...");
          if (ps->restoreFactoryDefaults()) {
            Serial.println("Factory reset command sent successfully.");
            Serial.println("Device will restart with default settings.");
            Serial.println("You may need to reconnect using the default baud rate (115200).");
          } else {
            Serial.println("Failed to execute factory reset command.");
          }
        } else {
          Serial.println("\nFactory reset cancelled.");
        }
        break;
      }
      delay(100);
    }
    
    if (timeout) {
      Serial.println("\nTimeout waiting for confirmation. Factory reset cancelled.");
    }
    return;
  }
  
  // Process commands based on current menu
  switch (currentMenu) {
    case MenuState::MAIN_MENU:
      handleMainMenu(input, ps, config);
      break;
    case MenuState::BASIC_CONTROL:
      handleBasicControl(input, ps);
      break;
    case MenuState::MEASUREMENT_MENU:
      handleMeasurementMenu(input, ps);
      break;
    case MenuState::PROTECTION_MENU:
      handleProtectionMenu(input, ps);
      break;
    case MenuState::SETTINGS_MENU:
      handleSettingsMenu(input, ps, config);
      break;
    case MenuState::DEBUG_MENU:
      handleDebugMenu(input, ps);
      break;
    case MenuState::CD_DATA_MENU:
      handleCDDataMenu(input, ps);
      break;
    case MenuState::WIFI_MENU:  // Add case for handling WiFi menu commands
      handleWiFiMenu(input, ps);
      break;
  }
}

void displayDeviceInfo(XY_SKxxx* ps) {
  // Check if powerSupply is initialized
  if (!ps) {
    Serial.println("Error: Power supply not initialized");
    return;
  }

  uint16_t model = ps->getModel();
  uint16_t version = ps->getVersion();
  
  Serial.println("\n==== Device Information ====");
  Serial.print("Model: ");
  Serial.println(model);
  Serial.print("Firmware Version: ");
  Serial.println(version);
  
  // Get current baudrate
  uint8_t baudCode = ps->getBaudRateCode();
  Serial.print("Baud Rate Code: ");
  Serial.print(baudCode);
  
  // Display the actual baud rate
  long actualBaud = ps->getActualBaudRate();
  if (actualBaud > 0) {
    Serial.print(" (");
    Serial.print(actualBaud);
    Serial.println(" bps)");
  } else {
    Serial.println(" (Unknown)");
  }
}

// Main implementation of status display
void displayDeviceStatus(XY_SKxxx* ps) {
  if (!ps) {
    Serial.println("Error: Power supply not initialized");
    return;
  }

  Serial.println("\n==== Power Supply Status ====");
  
  // Check if output is enabled
  bool outputEnabled = ps->isOutputEnabled(true);
  Serial.print("Power Supply Output: ");
  Serial.println(outputEnabled ? "ON" : "OFF");
  
  // Read current output values
  float voltage = ps->getOutputVoltage(true);
  float current = ps->getOutputCurrent(true);
  float power = ps->getOutputPower(true);
  
  Serial.print("Output Voltage: ");
  Serial.print(voltage, 2);
  Serial.println(" V");
  
  Serial.print("Output Current: ");
  Serial.print(current, 3);
  Serial.println(" A");
  
  Serial.print("Output Power: ");
  Serial.print(power, 3); // Changed from 2 to 3 decimal places
  Serial.println(" W");
  
  // Check operating mode
  Serial.print("Operating Mode: ");
  OperatingMode mode = ps->getOperatingMode(true);
  float cpValue; // Move variable declaration outside of switch statement

  switch (mode) {
    case MODE_CP:
      Serial.println("Constant Power (CP)");
      cpValue = ps->getCachedConstantPower(false); // Already refreshed in getOperatingMode
      Serial.print("CP Setting: ");
      Serial.print(cpValue, 2);
      Serial.println(" W");
      break;
    case MODE_CC:
      Serial.println("Constant Current (CC)");
      break;
    case MODE_CV:
      Serial.println("Constant Voltage (CV)");
      break;
  }
  
  // Front panel keys status - IMPORTANT ADDITION
  bool keyLocked = ps->isKeyLocked(true); // Force refresh
  Serial.print("Front Panel Keys: ");
  Serial.println(keyLocked ? "LOCKED" : "UNLOCKED");
  
  // Read settings
  float setVoltage = ps->getSetVoltage(true);
  float setCurrent = ps->getSetCurrent(true);
  
  Serial.print("Set Voltage: ");
  Serial.print(setVoltage, 2);
  Serial.println(" V");
  
  Serial.print("Set Current: ");
  Serial.print(setCurrent, 3);
  Serial.println(" A");
  
  // Read input voltage
  float inputVoltage = ps->getInputVoltage(true);
  Serial.print("Input Voltage: ");
  Serial.print(inputVoltage, 2);
  Serial.println(" V");
  
  // Read MPPT status and threshold
  bool mpptEnabled;
  if (ps->getMPPTEnable(mpptEnabled)) {
    Serial.print("MPPT Status: ");
    Serial.println(mpptEnabled ? "ENABLED" : "DISABLED");
    
    if (mpptEnabled) {
      float mpptThreshold;
      if (ps->getMPPTThreshold(mpptThreshold)) {
        Serial.print("MPPT Threshold: ");
        Serial.print(mpptThreshold * 100, 0); // Convert to percentage
        Serial.println("%");
      }
    }
  }
  
  // Read temperature
  float internalTemp = ps->getInternalTemperature(true);
  bool isCelsius;
  ps->getTemperatureUnit(isCelsius);
  
  Serial.print("Internal Temperature: ");
  Serial.print(internalTemp, 1);
  Serial.println(isCelsius ? " °F" : " °C"); // Flip the interpretation: 0=Celsius, 1=Fahrenheit
}

void displayConfig(XYModbusConfig& config) {
  Serial.println("\n==== Configuration ====");
  Serial.print("RX Pin: ");
  Serial.println(config.rxPin);
  Serial.print("TX Pin: ");
  Serial.println(config.txPin);
  Serial.print("Slave ID: ");
  Serial.println(config.slaveId);
  Serial.print("Baud Rate: ");
  Serial.println(config.baudRate);
}

void setupSerialMonitorControl() {
  // Clear any previous state
  serialBuffer = "";
  serialInputComplete = false;
  
  // Show the initial menu
  initializeSerialInterface();
  
  // Debug message to confirm initialization
  Serial.println("Serial monitor control initialized");
}

void checkSerialMonitorInput(XY_SKxxx* ps, XYModbusConfig& config) {
  // Process any complete input
  if (serialInputComplete) {
    // Trim whitespace
    serialBuffer.trim();
    if (serialBuffer.length() > 0) {
      processSerialCommand(serialBuffer, ps, config);
    }
    serialBuffer = "";
    serialInputComplete = false;
  }
  
  // Check for new serial input
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    
    // Process on newline
    if (inChar == '\n' || inChar == '\r') {
      if (serialBuffer.length() > 0) {
        serialInputComplete = true;
        break;
      }
    } else {
      // Add character to buffer
      serialBuffer += inChar;
    }
  }
}

// Helper function to parse float values from input string
bool parseFloat(const String& input, float& value) {
  char* endPtr;
  value = strtof(input.c_str(), &endPtr);
  
  if (endPtr == input.c_str() || *endPtr != '\0') {
    Serial.println("Invalid number format");
    return false;
  }
  
  return true;
}

// Helper function to parse uint8_t values from input string
bool parseUInt8(const String& input, uint8_t& value) {
  char* endPtr;
  long val = strtol(input.c_str(), &endPtr, 10);
  
  if (endPtr == input.c_str() || *endPtr != '\0' || val < 0 || val > 255) {
    Serial.println("Invalid number format");
    return false;
  }
  
  value = (uint8_t)val;
  return true;
}

// Helper function to parse uint16_t values from input string
bool parseUInt16(const String& input, uint16_t& value) {
  char* endPtr;
  long val = strtol(input.c_str(), &endPtr, 10);
  
  if (endPtr == input.c_str() || *endPtr != '\0' || val < 0 || val > 65535) {
    Serial.println("Invalid number format");
    return false;
  }
  
  value = (uint16_t)val;
  return true;
}

// Helper function to parse hex values from input string
bool parseHex(const String& input, uint16_t& value) {
  char* endPtr;
  long val = strtol(input.c_str(), &endPtr, 16);
  
  if (endPtr == input.c_str() || *endPtr != '\0' || val < 0 || val > 65535) {
    Serial.println("Invalid hex format");
    return false;
  }
  
  value = (uint16_t)val;
  return true;
}
