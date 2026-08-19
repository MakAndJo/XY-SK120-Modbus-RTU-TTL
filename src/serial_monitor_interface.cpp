#include <Arduino.h>
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h"
#include "serial_monitor_interface.h"
#include "config_manager.h"
#include "wifi_interface/wifi_settings.h" // Include the new wifi_settings header
#include "wifi_interface/wifi_manager_wrapper.h"

// Include all the interface components
#include "serial_interface/serial_interface.h"
#include "serial_interface/serial_core.h"

// Changed from object to pointer to match main.cpp
extern XY_SKxxx* powerSupply;

// Use inline to avoid multiple definition errors at link time
// These inline functions allow main.cpp to call the real implementations

inline void displayStatus(XY_SKxxx* ps) {
  // Fully qualify the function call with a global scope operator (::)
  ::displayStatus(ps);  // Call the function in serial_core.cpp
}

inline void displayConfig(XYModbusConfig& config) {
  ::displayConfig(config);  // Call the function in serial_core.cpp
}

inline void setupSerialMonitorControl() {
  ::setupSerialMonitorControl();  // Call the function in serial_core.cpp
}

// Modify checkSerialMonitorInput to include WiFi settings
inline void checkSerialMonitorInput(XY_SKxxx* ps, XYModbusConfig& config) {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    Serial.print("Received command: "); // Debug print
    Serial.println(command); // Debug print

    if (command == "status") {
      displayStatus(ps);
    } else if (command == "config") {
      displayConfig(config);
    } else if (command == "wifi") {
      Serial.println("Calling handleWifiSettingsCommands"); // Debug print
      handleWifiSettingsCommands(); // Call the new function
    } else if (command == "help") {
      Serial.println("Available commands:");
      Serial.println("status - Display current status");
      Serial.println("config - Display current configuration");
      Serial.println("wifi - Configure WiFi settings");
      Serial.println("help - Display available commands");
    } else {
      Serial.println("Invalid command. Type 'help' for available commands.");
    }
  }
}

// Function to handle WiFi settings commands
void handleWifiSettingsCommands() {
  Serial.println("WiFi Settings Menu:");
  Serial.println("1: Save WiFi Credentials");
  Serial.println("2: Load WiFi Credentials");
  Serial.println("3: Reset WiFi Settings");
  Serial.println("Enter your choice: ");

  while (Serial.available() == 0) {
    delay(100); // Wait for user input
  }

  int choice = Serial.parseInt();
  Serial.readStringUntil('\n'); // Consume the newline character
  Serial.print("User choice: "); // Debug print
  Serial.println(choice); // Debug print

  switch (choice) {
    case 1: {
      Serial.println("Enter SSID: ");
      String ssid = Serial.readStringUntil('\n');
      ssid.trim();

      Serial.println("Enter Password: ");
      String password = Serial.readStringUntil('\n');
      password.trim();

      if (saveWiFiCredentialsToNVS(ssid, password)) {
        Serial.println("WiFi credentials saved successfully!");
      } else {
        Serial.println("Failed to save WiFi credentials.");
      }
      break;
    }
    case 2: {
      String wifiCredentials = loadWiFiCredentialsFromNVS();
      Serial.print("WiFi Credentials: ");
      Serial.println(wifiCredentials);
      break;
    }
    case 3: {
      if (resetWiFi()) {
        Serial.println("WiFi settings reset successfully. Device will restart.");
      } else {
        Serial.println("Failed to reset WiFi settings.");
      }
      break;
    }
    default:
      Serial.println("Invalid choice.");
      break;
  }
}