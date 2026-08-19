#include "menu_settings.h"
#include "serial_core.h"
#include "serial_interface.h"
#include "menu_main.h"  // Add this include to access displayMainMenu()

void displaySettingsMenu() {
  Serial.println("\n==== Device Settings ====");
  Serial.println("beeper [on/off] - Enable/disable beeper");
  Serial.println("brightness [level] - Set display brightness (1-5, 5 = brightest)");
  Serial.println("tempunit [c/f] - Set temperature unit (Celsius/Fahrenheit)");
  Serial.println("sleep [0-30] - Set sleep timeout (minutes, 0:off)");
  Serial.println("--------------------------");
  Serial.println("slave [1-247] - Set Modbus slave address");
  Serial.println("baud [0-8] - Set baudrate (0:9600, 1:14400, 2:19200, 3:38400,");
  Serial.println("              4:56000, 5:57600, 6:115200, 7:2400, 8:4800)");
  Serial.println("rxpin [pin] - Set Modbus RX pin number");
  Serial.println("txpin [pin] - Set Modbus TX pin number");
  Serial.println("--------------------------");
  Serial.println("mppt [on/off] - Enable/disable MPPT (Maximum Power Point Tracking)");
  Serial.println("mpptthr [value] - Set MPPT threshold (0-100%, default 80%)");
  Serial.println("--------------------------");
  Serial.println("default - Restore device to factory defaults");
  Serial.println("--------------------------");
  Serial.println("save - Save current settings to device");
  Serial.println("saveconfig - Save local configuration to flash");
  Serial.println("--------------------------");
  Serial.println("showsettings - Display all device settings");
  Serial.println("menu - Return to main menu");
  Serial.println("help - Show this menu");
}

void handleSettingsMenu(const String& input, XY_SKxxx* ps, XYModbusConfig& config) {
  if (!ps) {
    Serial.println("Error: Power supply not initialized");
    return;
  }
  
  if (input == "showsettings") {
    displayAllDeviceSettings(ps);
  } else if (input.startsWith("baudrate ")) {
    uint8_t baudCode;
    if (parseUInt8(input.substring(9), baudCode)) {
      if (ps->setBaudRate(baudCode)) {
        Serial.print("Baud rate code set to: ");
        Serial.println(baudCode);
        Serial.println("You must save and restart the device for this to take effect");
      } else {
        Serial.println("Failed to set baud rate");
      }
    }
  } else if (input.startsWith("address ")) {
    uint8_t address;
    if (parseUInt8(input.substring(8), address)) {
      if (ps->setSlaveAddress(address)) {
        Serial.print("Modbus address set to: ");
        Serial.println(address);
        Serial.println("You must save and restart the device for this to take effect");
      } else {
        Serial.println("Failed to set Modbus address");
      }
    }
  } else if (input.startsWith("slave ")) {
    uint8_t address;
    if (parseUInt8(input.substring(6), address)) {
      if (address >= 1 && address <= 247) {
        if (ps->setSlaveAddress(address)) {
          Serial.print("Slave address set to: ");
          Serial.println(address);
          Serial.println("You must save settings for this to take effect");
        } else {
          Serial.println("Failed to set slave address");
        }
      } else {
        Serial.println("Invalid address. Must be between 1-247.");
      }
    }
  } else if (input.startsWith("brightness ")) {
    uint8_t level;
    if (parseUInt8(input.substring(11), level)) {
      if (level >= 1 && level <= 5) {
        if (ps->setBacklightBrightness(level)) {
          Serial.print("Display brightness set to: ");
          Serial.println(level);
        } else {
          Serial.println("Failed to set display brightness");
        }
      } else {
        Serial.println("Invalid brightness level. Must be between 1-5.");
      }
    }
  } else if (input.startsWith("tempunit ")) {
    String unit = input.substring(9);
    unit.toLowerCase();
    unit.trim();
    
    if (unit == "c") {
      if (ps->setTemperatureUnit(false)) {
        Serial.println("Temperature unit set to Celsius");
      } else {
        Serial.println("Failed to set temperature unit");
      }
    } else if (unit == "f") {
      if (ps->setTemperatureUnit(true)) {
        Serial.println("Temperature unit set to Fahrenheit");
      } else {
        Serial.println("Failed to set temperature unit");
      }
    } else {
      Serial.println("Invalid unit. Use 'c' for Celsius or 'f' for Fahrenheit");
    }
  } else if (input.startsWith("sleep ")) {
    uint8_t timeout;
    if (parseUInt8(input.substring(6), timeout)) {
      if (timeout <= 30) {
        if (ps->setSleepTimeout(timeout)) {
          if (timeout == 0) {
            Serial.println("Sleep function disabled");
          } else {
            Serial.print("Sleep timeout set to ");
            Serial.print(timeout);
            Serial.println(" minutes");
          }
        } else {
          Serial.println("Failed to set sleep timeout");
        }
      } else {
        Serial.println("Invalid timeout. Must be between 0-30.");
      }
    }
  } else if (input.startsWith("rxpin ")) {
    uint8_t pin;
    String pinStr = input.substring(6);
    if (parseUInt8(pinStr, pin)) {
      config.rxPin = pin;
      Serial.println("RX pin set to " + String(pin));
    }
  } else if (input.startsWith("txpin ")) {
    uint8_t pin;
    String pinStr = input.substring(6);
    if (parseUInt8(pinStr, pin)) {
      config.txPin = pin;
      Serial.println("TX pin set to " + String(pin));
    }
  } else if (input == "save") {
    if (ps->updateDeviceSettings()) {
      Serial.println("Settings saved to device");
    } else {
      Serial.println("Failed to save settings");
    }
  } else if (input == "default") {
    if (ps->restoreFactoryDefaults()) {
      Serial.println("Factory defaults restored");
      Serial.println("Device will restart. Please reconnect with default settings.");
    } else {
      Serial.println("Failed to restore factory defaults");
    }
  } else if (input == "saveconfig") {
    if (XYConfigManager::saveConfig(config)) {
      Serial.println("Configuration saved");
      Serial.println("Please restart the device for changes to take effect");
    } else {
      Serial.println("Failed to save configuration");
    }
  } else if (input.startsWith("beeper ")) {
    String subCmd = input.substring(7);
    subCmd.trim();
    
    if (subCmd == "on") {
      if (ps->setBeeper(true)) {
        Serial.println("Beeper enabled");
      } else {
        Serial.println("Failed to enable beeper");
      }
    } else if (subCmd == "off") {
      if (ps->setBeeper(false)) {
        Serial.println("Beeper disabled");
      } else {
        Serial.println("Failed to disable beeper");
      }
    } else {
      Serial.println("Invalid option. Use 'on' or 'off'");
    }
  } else if (input.startsWith("mppt ")) {
    String subCmd = input.substring(5);
    subCmd.trim();
    
    if (subCmd == "on") {
      if (ps->setMPPTEnable(true)) {
        Serial.println("MPPT mode enabled");
      } else {
        Serial.println("Failed to enable MPPT mode");
      }
    } else if (subCmd == "off") {
      if (ps->setMPPTEnable(false)) {
        Serial.println("MPPT mode disabled");
      } else {
        Serial.println("Failed to disable MPPT mode");
      }
    } else {
      Serial.println("Invalid option. Use 'on' or 'off'");
    }
  } else if (input.startsWith("mpptthr ")) {
    float threshold;
    if (parseFloat(input.substring(8), threshold)) {
      // Convert from percentage (0-100) to decimal (0-1)
      if (threshold >= 0 && threshold <= 100) {
        threshold /= 100.0f;
        if (ps->setMPPTThreshold(threshold)) {
          Serial.print("MPPT threshold set to ");
          Serial.print(threshold * 100, 0);
          Serial.println("%");
        } else {
          Serial.println("Failed to set MPPT threshold");
        }
      } else {
        Serial.println("Invalid threshold. Must be between 0-100%.");
      }
    }
  } else if (input.startsWith("baud ")) {
    uint8_t baudCode;
    if (parseUInt8(input.substring(5), baudCode)) {
      if (baudCode <= 8) {
        if (ps->setBaudRate(baudCode)) {
          Serial.print("Baud rate code set to: ");
          Serial.println(baudCode);
          
          long newBaud;
          switch (baudCode) {
            case 0: newBaud = 9600; break;
            case 1: newBaud = 14400; break;
            case 2: newBaud = 19200; break;
            case 3: newBaud = 38400; break;
            case 4: newBaud = 56000; break;
            case 5: newBaud = 57600; break;
            case 6: newBaud = 115200; break;
            case 7: newBaud = 2400; break;
            case 8: newBaud = 4800; break;
            default: newBaud = 9600;
          }
          
          Serial.print("New baud rate will be: ");
          Serial.print(newBaud);
          Serial.println(" bps");
          Serial.println("You must save settings and restart the device for this to take effect");
          
          // Update the local config too
          config.baudRate = newBaud;
        } else {
          Serial.println("Failed to set baud rate");
        }
      } else {
        Serial.println("Invalid baud code. Must be between 0-8.");
      }
    }
  } else if (input == "help") {
    displaySettingsMenu();
  } else if (input == "menu") {
    setMenuState(MenuState::MAIN_MENU);
    displayMainMenu();
  } else {
    Serial.println("Unknown command. Type 'help' for options.");
  }
}

// Keep the existing displayDeviceSettings function for backward compatibility
void displayDeviceSettings(XY_SKxxx* ps) {
  if (!ps) {
    Serial.println("Error: Power supply not initialized");
    return;
  }
  
  // Basic device settings
  Serial.println("\n==== Device Settings ====");
  
  // Beeper status
  bool beeperEnabled;
  if (ps->getBeeper(beeperEnabled)) {
    Serial.print("Beeper: ");
    Serial.println(beeperEnabled ? "ENABLED" : "DISABLED");
  }
  
  // Brightness
  uint8_t brightness = ps->getBacklightBrightness();
  if (brightness > 0) {
    Serial.print("Display Brightness: ");
    Serial.print(brightness);
    Serial.println(" (1-5)");
  }
  
  // Sleep timeout
  uint8_t sleepTimeout = ps->getSleepTimeout();
  if (sleepTimeout != 255) {
    Serial.print("Sleep Timeout: ");
    if (sleepTimeout == 0) {
      Serial.println("Never");
    } else {
      Serial.print(sleepTimeout);
      Serial.println(" minutes");
    }
  }
}

// New comprehensive settings display function
void displayAllDeviceSettings(XY_SKxxx* ps) {
  if (!ps) {
    Serial.println("Error: Power supply not initialized");
    return;
  }
  
  Serial.println("\n===== All Device Settings =====");
  
  // Basic device settings
  bool beeperEnabled;
  if (ps->getBeeper(beeperEnabled)) {
    Serial.print("Beeper: ");
    Serial.println(beeperEnabled ? "ENABLED" : "DISABLED");
  }
  
  // Display settings
  uint8_t brightness = ps->getBacklightBrightness();
  if (brightness > 0) {
    Serial.print("Display Brightness: ");
    Serial.print(brightness);
    Serial.println(" (1-5)");
  }
  
  // Sleep settings
  uint8_t sleepTimeout = ps->getSleepTimeout();
  if (sleepTimeout != 255) {
    Serial.print("Sleep Timeout: ");
    if (sleepTimeout == 0) {
      Serial.println("Never");
    } else {
      Serial.print(sleepTimeout);
      Serial.println(" minutes");
    }
  }
  
  // Temperature settings
  bool isCelsius;
  if (ps->getTemperatureUnit(isCelsius)) {
    Serial.print("Temperature Unit: ");
    Serial.println(isCelsius ? "Fahrenheit" : "Celsius");
  }
  
  // Key lock status
  bool keyLocked = ps->isKeyLocked(true);
  Serial.print("Front Panel Keys: ");
  Serial.println(keyLocked ? "LOCKED" : "UNLOCKED");
  
  // Beeper settings
  if (ps->getBeeper(beeperEnabled)) {
    Serial.print("Beeper: ");
    Serial.println(beeperEnabled ? "ENABLED" : "DISABLED");
  }
  
  // MPPT settings
  bool mpptEnabled;
  if (ps->getMPPTEnable(mpptEnabled)) {
    Serial.print("MPPT Mode: ");
    Serial.println(mpptEnabled ? "ENABLED" : "DISABLED");
    
    if (mpptEnabled) {
      float mpptThreshold;
      if (ps->getMPPTThreshold(mpptThreshold)) {
        Serial.print("MPPT Threshold: ");
        Serial.print(mpptThreshold * 100, 0);
        Serial.println("%");
      }
    }
  }
  
  // Battery settings
  float btfCurrent;
  if (ps->getBatteryCutoffCurrent(btfCurrent)) {
    Serial.print("Battery Cutoff Current: ");
    if (btfCurrent > 0) {
      Serial.print(btfCurrent, 3);
      Serial.println(" A");
    } else {
      Serial.println("OFF");
    }
  }
  
  // Power-on initialization setting
  bool outputOnAtStartup;
  if (ps->getPowerOnInitialization(outputOnAtStartup)) {
    Serial.print("Output On At Startup: ");
    Serial.println(outputOnAtStartup ? "YES" : "NO");
  }
  
  Serial.println();
}
