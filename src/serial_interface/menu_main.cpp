#include "menu_main.h"
#include "serial_core.h"
#include "menu_basic.h"
#include "menu_measurement.h"
#include "menu_protection.h"
#include "menu_settings.h"
#include "menu_debug.h"
#include "menu_cd_data.h"
#include "menu_wifi.h"  // Include the new WiFi menu header

void displayMainMenu() {
  Serial.println("\n==== Main Menu ====");
  Serial.println("1. Basic Control");
  Serial.println("2. Measurement");
  Serial.println("3. Protection");
  Serial.println("4. Settings");
  Serial.println("5. Debug (Register R/W)");
  Serial.println("6. CD Data Groups");
  Serial.println("7. WiFi Settings");  // Add the new menu option
  Serial.println("status - Show power supply status");
  Serial.println("prot - Show protection settings and status");
  Serial.println("config - Show current configuration");
  Serial.println("info - Display device information");
  Serial.println("help - Show this menu");
  Serial.println("Enter option number or command:");
}

void handleMainMenu(const String& input, XY_SKxxx* ps, XYModbusConfig& config) {
  if (input == "1") {
    setMenuState(MenuState::BASIC_CONTROL);
    displayBasicControlMenu();
  } else if (input == "2") {
    setMenuState(MenuState::MEASUREMENT_MENU);
    displayMeasurementMenu();
  } else if (input == "3") {
    setMenuState(MenuState::PROTECTION_MENU);
    displayProtectionMenu();
  } else if (input == "4") {
    setMenuState(MenuState::SETTINGS_MENU);
    displaySettingsMenu();
  } else if (input == "5") {
    setMenuState(MenuState::DEBUG_MENU);
    displayDebugMenu();
  } else if (input == "6") {
    setMenuState(MenuState::CD_DATA_MENU);
    displayCDDataMenu();
  } else if (input == "7") {  // Add handler for the new WiFi menu option
    setMenuState(MenuState::WIFI_MENU);
    displayWiFiMenu();
  } else if (input.equalsIgnoreCase("status")) {
    // Call the displayDeviceStatus function from serial_core.cpp
    displayDeviceStatus(ps);
  } else if (input.equalsIgnoreCase("config")) {
    displayConfig(config);
    return;
  } else if (input.equalsIgnoreCase("help")) {
    displayMainMenu();
  } else if (input.equalsIgnoreCase("info")) {
    displayDeviceInfo(ps);
  } else {
    Serial.println("Unknown command. Type 'help' for options.");
  }
}
