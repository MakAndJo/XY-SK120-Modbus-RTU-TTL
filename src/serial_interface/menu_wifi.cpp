// This file is now just a stub that includes the functionality 
// from the refactored modules.

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "menu_wifi.h"
#include "serial_core.h"
#include "serial_interface.h"
#include "../wifi_interface/wifi_settings.h"

// Display WiFi menu
void displayWiFiMenu() {
  Serial.println("\n==== WiFi Settings ====");
  Serial.println("scan - Scan for WiFi networks");
  Serial.println("connect \"ssid\" \"password\" - Connect to a WiFi network");
  Serial.println("ap \"ssid\" \"password\" - Set up a WiFi access point");
  Serial.println("exit - Exit AP mode and return to station mode");
  Serial.println("status - Show current WiFi status");
  Serial.println("ip - Show IP configuration");
  Serial.println("savedwifi - Display saved WiFi networks");
  Serial.println("addwifi \"ssid\" \"password\" [priority] - Add network to saved list");
  Serial.println("syncwifi - Sync current WiFi to saved networks");
  Serial.println("repairwifi - Repair corrupted WiFi credentials");
  Serial.println("menu - Return to main menu");
  Serial.println("help - Show this menu");
  Serial.println("Note: Use quotes for SSIDs containing spaces (e.g., connect \"My WiFi\" \"pass123\")");
}

// Handle WiFi menu commands
void handleWiFiMenu(const String& input, XY_SKxxx* ps) {
  if (input == "scan") {
    scanWiFiNetworks();
  } else if (input == "status") {
    displayWiFiStatus();
  } else if (input == "ip") {
    displayIPInfo();
  } else if (input == "savedwifi") {
    displaySavedWiFiNetworks();
  } else if (input == "syncwifi") {
    syncCurrentWiFi();
  } else if (input == "repairwifi") {
    Serial.println("Attempting to repair WiFi credentials...");
    if (repairWiFiCredentials()) {
      Serial.println("WiFi credentials repaired successfully.");
      displaySavedWiFiNetworks();
    } else {
      Serial.println("Failed to repair WiFi credentials.");
    }
  } else if (input.startsWith("connect ")) {
    String remainingInput = input.substring(8);
    String ssid, password;
    String unused;
    bool quoted = extractQuotedParameters(remainingInput, ssid, password, unused);
    
    if (!quoted) {
      // Try space-based parsing as fallback
      int spaceIndex = remainingInput.indexOf(' ');
      if (spaceIndex > 0) {
        ssid = remainingInput.substring(0, spaceIndex);
        password = remainingInput.substring(spaceIndex + 1);
      } else {
        ssid = remainingInput;
        password = "";
      }
    }
    
    if (ssid.isEmpty()) {
      Serial.println("Invalid format. Use: connect \"Your SSID\" \"Your Password\"");
      return;
    }
    
    Serial.println("Command: connect \"" + ssid + "\" \"" + password + "\"");
    connectToWiFi(ssid, password);
  } else if (input.startsWith("ap ")) {
    String remainingInput = input.substring(3);
    String ssid, password;
    String unused;
    bool quoted = extractQuotedParameters(remainingInput, ssid, password, unused);
    
    if (!quoted) {
      // Try space-based parsing as fallback
      int spaceIndex = remainingInput.indexOf(' ');
      if (spaceIndex > 0) {
        ssid = remainingInput.substring(0, spaceIndex);
        password = remainingInput.substring(spaceIndex + 1);
      } else {
        ssid = remainingInput;
        password = "";
      }
    }
    
    if (ssid.isEmpty()) {
      Serial.println("Invalid format. Use: ap \"Your AP SSID\" \"Your AP Password\"");
      return;
    }
    
    Serial.println("Command: ap \"" + ssid + "\" \"" + password + "\"");
    setupWiFiAP(ssid, password);
  } else if (input.startsWith("addwifi ")) {
    String remainingInput = input.substring(8);
    String ssid, password;
    String leftover;
    
    bool quoted = extractQuotedParameters(remainingInput, ssid, password, leftover);
    
    if (!quoted) {
      // Try space-based parsing as fallback
      int firstSpace = remainingInput.indexOf(' ');
      if (firstSpace > 0) {
        ssid = remainingInput.substring(0, firstSpace);
        remainingInput = remainingInput.substring(firstSpace + 1);
        
        int secondSpace = remainingInput.indexOf(' ');
        if (secondSpace > 0) {
          password = remainingInput.substring(0, secondSpace);
          leftover = remainingInput.substring(secondSpace + 1);
        } else {
          password = remainingInput;
          leftover = "";
        }
      } else {
        Serial.println("Invalid format. Use: addwifi \"Your SSID\" \"Your Password\" [priority]");
        return;
      }
    }
    
    // Check for priority (optional parameter)
    int priority = 1; // Default priority
    leftover.trim();
    if (leftover.length() > 0 && isDigit(leftover.charAt(0))) {
      priority = leftover.toInt();
    }
    
    Serial.println("Command: addwifi \"" + ssid + "\" \"" + password + "\" " + priority);
    
    // Call the handler function
    handleAddWifi(input, ssid, password, priority);
  } else if (input == "help") {
    displayWiFiMenu();
  } else if (input == "menu") {
    setMenuState(MenuState::MAIN_MENU);
    displayMainMenu();
  } else {
    Serial.println("Unknown command. Type 'help' for options.");
  }
}

// Helper function to extract quoted parameters from a command
bool extractQuotedParameters(const String& input, String& param1, String& param2, String& remaining) {
  // Reset parameters
  param1 = "";
  param2 = "";
  remaining = input;
  
  // First quoted parameter
  int firstQuoteStart = input.indexOf('"');
  if (firstQuoteStart < 0) return false;
  
  int firstQuoteEnd = input.indexOf('"', firstQuoteStart + 1);
  if (firstQuoteEnd < 0) return false;
  
  param1 = input.substring(firstQuoteStart + 1, firstQuoteEnd);
  
  // Second quoted parameter (optional)
  int secondQuoteStart = input.indexOf('"', firstQuoteEnd + 1);
  if (secondQuoteStart >= 0) {
    int secondQuoteEnd = input.indexOf('"', secondQuoteStart + 1);
    if (secondQuoteEnd >= 0) {
      param2 = input.substring(secondQuoteStart + 1, secondQuoteEnd);
      
      // Extract remaining text after second quote
      if (secondQuoteEnd + 1 < input.length()) {
        remaining = input.substring(secondQuoteEnd + 1);
      } else {
        remaining = "";
      }
    }
  } else {
    // No second parameter, extract remaining text after first quote
    if (firstQuoteEnd + 1 < input.length()) {
      remaining = input.substring(firstQuoteEnd + 1);
    } else {
      remaining = "";
    }
  }
  
  return true;
}
