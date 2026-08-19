#include <Arduino.h>
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h"
#include "serial_monitor_interface.h"
#include "config_manager.h"
#include "wifi_interface/wifi_settings.h" // Include the new wifi_settings header
#include "wifi_interface/wifi_native.h"   // resetWiFiSettings etc.
#include "mqtt/mqtt_manager.h"           // mqtt set/get console commands

// Include all the interface components
#include "serial_interface/serial_interface.h"
#include "serial_interface/serial_core.h"

// Changed from object to pointer to match main.cpp
extern XY_SKxxx* powerSupply;

// Handle 'mqtt get|set|start|stop' console commands (defined below)
void handleMqttConsoleCommand(const String& command);

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
    } else if (command.startsWith("mqtt ")) {
      handleMqttConsoleCommand(command);
    } else if (command == "help") {
      Serial.println("Available commands:");
      Serial.println("status - Display current status");
      Serial.println("config - Display current configuration");
      Serial.println("wifi - Configure WiFi settings");
      Serial.println("mqtt get - Show MQTT config");
      Serial.println("mqtt set <host> [port] [user] [pass] [name] - Set MQTT config");
      Serial.println("mqtt start / mqtt stop - Start/stop the MQTT client");
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

// Handle 'mqtt set/get/start/stop' console commands
void handleMqttConsoleCommand(const String& command) {
  if (command == "mqtt get") {
    if (!mqttConfigLoaded()) {
      Serial.println("MQTT: not configured");
      return;
    }
    Serial.printf("MQTT: host=%s port=%d user=%s pass=%s name=%s state=%s\n",
                  mqttHost().c_str(), mqttPort(), mqttUser().c_str(),
                  mqttPass().c_str(), mqttDeviceName().c_str(),
                  mqttConnected() ? "connected" : "disconnected");
  } else if (command.startsWith("mqtt set ")) {
    String args = command.substring(9);
    args.trim();
    if (args.length() == 0) {
      Serial.println("Usage: mqtt set <host> [port] [user] [pass] [name]");
      return;
    }
    int space = args.indexOf(' ');
    String host = (space > 0) ? args.substring(0, space) : args;
    String rest = (space > 0) ? args.substring(space + 1) : "";
    rest.trim();

    uint16_t port = 1883;
    String user, pass, name;
    if (rest.length() > 0) {
      int s2 = rest.indexOf(' ');
      String portStr = (s2 > 0) ? rest.substring(0, s2) : rest;
      port = (uint16_t)portStr.toInt();
      rest = (s2 > 0) ? rest.substring(s2 + 1) : "";
      rest.trim();

      if (rest.length() > 0) {
        int s3 = rest.indexOf(' ');
        user = (s3 > 0) ? rest.substring(0, s3) : rest;
        rest = (s3 > 0) ? rest.substring(s3 + 1) : "";
        rest.trim();

        if (rest.length() > 0) {
          int s4 = rest.indexOf(' ');
          pass = (s4 > 0) ? rest.substring(0, s4) : rest;
          rest = (s4 > 0) ? rest.substring(s4 + 1) : "";
          rest.trim();
          if (rest.length() > 0) name = rest;
        }
      }
    }
    if (name.length() == 0) name = "XY-SK150S";
    mqttSaveConfig(host, port, user, pass, name);
    Serial.println("MQTT config saved. Restart device or use 'mqtt start'.");
  } else if (command == "mqtt start") {
    mqttStart();
  } else if (command == "mqtt stop") {
    mqttStop();
  } else {
    Serial.println("Usage: mqtt get | mqtt set <host> [port] [user] [pass] [name] | mqtt start | mqtt stop");
  }
}