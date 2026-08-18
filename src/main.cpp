#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ModbusMaster.h>
#include <FS.h>
#include <LittleFS.h>  // Built-in ESP32 LittleFS
#include "web_interface.h"
#include "modbus_handler.h"
#include "config_manager.h"
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h"
#include "serial_monitor_interface.h"
#include "serial_interface/serial_core.h"
#include "serial_interface/menu_debug.h" // For background bus sniffing (sniffTick)
#include "wifi_interface/wifi_manager_wrapper.h" // Include wrapper instead of WiFiManager directly
#include "web_interface/log_utils.h" // Use the web_interface version of the logging utilities

// Define WiFi reset button pin based on board
#ifdef CONFIG_IDF_TARGET_ESP32C3
  #define WIFI_RESET_PIN DEFAULT_WIFI_RESET_PIN  // Use board-specific pin from config
#else
  #define WIFI_RESET_PIN 0  // Original ESP32S3 pin
#endif

// Global configuration instance - renamed to avoid conflict
XYModbusConfig xyConfig;

// Create XY_SKxxx instance with default pins (will be updated from config)
XY_SKxxx* powerSupply = nullptr;

// WiFi host keepalive task: claims the PSU's master/status registers from the
// very first second of boot (even while WiFi is still connecting), so the PSU
// screen recognises the WiFi module immediately. The status is written as
// "not connected" (0) until WiFi is up.
void wifiHostKeepAliveTask(void* param) {
  (void)param;
  while (true) {
    wifiModuleKeepAlive();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

AsyncWebServer server(80);
ModbusMaster modbus;

// Remove the local getLogTimestamp implementation
// Now using the one from log_utils.h

void setup() {
  Serial.begin(115200);

  #ifdef CONFIG_IDF_TARGET_ESP32C3
    // ESP32C3 specific initialization
    delay(2000); // ESP32C3 needs more time for serial
    Serial.println("=== XY-SK150 for ESP32C3 ===");
    Serial.printf("Free heap at start: %d bytes\n", ESP.getFreeHeap());

    // Check if we have enough memory to proceed
    if (ESP.getFreeHeap() < MIN_FREE_HEAP) {
      Serial.println("ERROR: Insufficient memory to start!");
      Serial.printf("Available: %d, Required: %d\n", ESP.getFreeHeap(), MIN_FREE_HEAP);
      while(1) delay(1000); // Halt execution
    }

    // Set conservative CPU frequency for stability
    setCpuFrequencyMhz(160);
    Serial.printf("CPU frequency set to: %d MHz\n", ESP.getCpuFreqMHz());

    // Disable Bluetooth to free the shared 2.4GHz radio and avoid WiFi/BT coexistence issues
    btStop();

    #else
    // ESP32S3 initialization
    delay(1000);
    Serial.println("=== XY-SK150 for ESP32S3 ===");
  #endif

  LOG_INFO("Starting XY-SK150 Modbus RTU System");
  LOG_INFO("WiFi Setup Process Starting...");

  // Initialize WiFi reset button
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);

  // Check if reset button is pressed during boot
  if (digitalRead(WIFI_RESET_PIN) == LOW) {
    Serial.println("WiFi Reset button pressed - resetting WiFi settings");
    resetWiFiSettings();
    Serial.println("WiFi settings reset! Restarting...");
    delay(2000);
    ESP.restart();
  }

  // Initialize LittleFS (correct naming for ESP32)
  if(!LittleFS.begin(true)) {
    LOG_ERROR("LittleFS Mount Failed");
  } else {
    LOG_INFO("LittleFS initialized successfully");
  }

  // Initialize the configuration manager
  if (!XYConfigManager::begin()) {
    Serial.println("Failed to initialize configuration manager");
  }

  // Load configuration from NVS
  xyConfig = XYConfigManager::loadConfig();

  // Display the loaded configuration
  displayConfig(xyConfig);

  // Create the power supply instance with the loaded configuration
  powerSupply = new XY_SKxxx(xyConfig.rxPin, xyConfig.txPin, xyConfig.slaveId);

  // Initialize Modbus RTU (attaches the global ModbusMaster to Serial1; the
  // real baud/pins are applied by powerSupply->begin() below)
  setupModbus();

  // Initialize the power supply (independent of WiFi)
  powerSupply->begin(xyConfig.baudRate);
  delay(500); // Give the device time to initialize

  // Test connection
  Serial.println("Testing connection to power supply...");
  if (powerSupply->testConnection()) {
    Serial.println("Connection successful!");
  } else {
    Serial.println("Connection failed. Please check wiring and settings.");
  }

  // Start claiming the PSU master/status registers from the very first second,
  // before/while WiFi connects. Status stays "not connected" until WiFi is up.
  xTaskCreatePinnedToCore(wifiHostKeepAliveTask, "wifiHost", 4096, NULL, 1, NULL, 1);

  // For initial setup, force the AP mode to appear temporarily
  // by forcing WiFi reset once (comment out after first use)
  // resetWiFiSettings();  // <-- COMMENTED OUT after successful connectionuse

  // Attempt to connect to WiFi with clearer naming and feedback
  Serial.println("Starting WiFi connection process...");

  // Initialize WiFi using the wrapper - use a more descriptive name
  if(!initWiFiManager("XY-SK150-Setup")) {
    Serial.println("Failed to connect and hit timeout");
    Serial.println("Will restart device and try again...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("WiFi connected successfully!");
  Serial.print("IP address: ");
  Serial.println(getWiFiIP());

  // More robust WiFi stabilization
  WiFi.persistent(true);
  WiFi.setSleep(false); // Disable WiFi sleep mode to improve stability

  // Give WiFi more time to stabilize before starting web server
  for(int i=0; i<5; i++) {
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connection stable");
      break;
    }
    Serial.println("Waiting for WiFi to stabilize...");
    delay(1000);
  }

  // This sequence helps resolve binding issues
  IPAddress localIP = WiFi.localIP();
  IPAddress subnet = WiFi.subnetMask();
  IPAddress gateway = WiFi.gatewayIP();
  IPAddress dns = WiFi.dnsIP();

  if (WiFi.status() == WL_CONNECTED) {
    // Disconnect and reconnect with explicit network parameters
    WiFi.disconnect(false);
    delay(500);
    WiFi.config(localIP, gateway, subnet, dns);

    if (!WiFi.reconnect()) {
      Serial.println("Reconnection failed, restarting...");
      ESP.restart();
    }

    delay(1000);
    Serial.print("Reconnected with IP: ");
    Serial.println(WiFi.localIP());
  }

  // After WiFi is connected, configure NTP for accurate timestamps
  if (WiFi.status() == WL_CONNECTED) {
    configureNTP();
  }

  // Try an alternative approach with server initialization
  try {
    // Setup web server routes first
    setupWebServer(&server);

    // Longer delay to ensure network stack is ready
    delay(2000);

    // Start server
    server.begin();
    Serial.print(getLogTimestamp());
    Serial.println("HTTP server started successfully");
  }
  catch (const std::exception& e) {
    Serial.print("Error starting server: ");
    Serial.println("Exception caught");

    // Try a fallback approach
    delay(5000);
    server.end();
    delay(1000);
    server.begin();
    Serial.println("HTTP server started (second attempt)");
  }

  Serial.println("\n\n----- XY-SK150 Modbus RTU Control System -----");
  displayDeviceStatus(powerSupply);

  // Initialize serial monitor interface
  Serial.println("\nInitializing serial monitor interface...");
  setupSerialMonitorControl();
  Serial.println("Enter commands to control the power supply.");
  initializeSerialInterface();
}

void loop() {
  // No longer update dummy Modbus data
  // updateModbusData(); // <-- Comment out or remove this line

  // Process serial monitor commands
  checkSerialMonitorInput(powerSupply, xyConfig);

  // Background bus sniffing (non-blocking, keeps keep-alive running)
  sniffTick(powerSupply);

  // You can process other interfaces here in the future:
  // processWebSocketMessages();
  // processRestApiRequests();
  // processMqttMessages();

  // Add any periodic tasks here
  // Server-side PSU polling: read fresh status and push it to all WebSocket
  // clients. This is how the UI gets live data - the client never polls.
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate > 250) { // Poll as fast as Modbus allows
    lastStatusUpdate = millis();
    pollAndBroadcastPSUStatus();
  }

  // Keep the WiFi host registers on the PSU alive - handled by the background
  // wifiHostKeepAliveTask, no longer needed in loop().

  // Sync the PSU RTC/weather block (Unix time + zeroed weather, ~10s like the
  // OEM XY-WFPOW module does). Runs only after NTP has provided valid time.
  static unsigned long lastRtcSync = 0;
  if (millis() - lastRtcSync > 10000) {
    lastRtcSync = millis();
    syncRtcWeatherToPSU();
  }

  // Check for WiFi reset button press during operation
  static unsigned long lastButtonCheck = 0;
  if (millis() - lastButtonCheck > 1000) { // Check button every second
    lastButtonCheck = millis();

    // If button is held down for 3 seconds
    if (digitalRead(WIFI_RESET_PIN) == LOW) {
      unsigned long buttonPressStart = millis();
      while (digitalRead(WIFI_RESET_PIN) == LOW) {
        delay(10); // Small delay for debounce

        // If button is held for 3 seconds, reset WiFi settings
        if (millis() - buttonPressStart > 3000) {
          Serial.println("WiFi Reset button held for 3 seconds - resetting WiFi settings");
          resetWiFiSettings();
          Serial.println("WiFi settings reset! Restarting...");
          delay(500);
          ESP.restart();
        }
      }
    }
  }

  // ESP32C3 specific memory monitoring and stability measures
  #ifdef CONFIG_IDF_TARGET_ESP32C3
    static unsigned long lastMemCheck = 0;
    if (millis() - lastMemCheck > 30000) { // Check every 30 seconds
      lastMemCheck = millis();

      size_t freeHeap = ESP.getFreeHeap();
      Serial.printf("Memory check - Free heap: %d bytes\n", freeHeap);

      // Critical memory check
      if (freeHeap < (MIN_FREE_HEAP / 2)) {
        Serial.println("CRITICAL: Very low memory detected!");
        Serial.printf("Free: %d, Critical threshold: %d\n", freeHeap, MIN_FREE_HEAP / 2);
        Serial.println("Restarting to prevent crash...");
        delay(1000);
        ESP.restart();
      }

      // Warning check
      if (freeHeap < MIN_FREE_HEAP) {
        Serial.println("WARNING: Low memory detected!");
        // Could implement feature disable here if needed
      }
    }

    // Yield more frequently on ESP32C3 for stability
    yield();
  #endif

  // Add a small delay to prevent throttling the CPU
  #ifdef CONFIG_IDF_TARGET_ESP32C3
    delay(150); // Slightly longer delay for ESP32C3
  #else
    delay(100);
  #endif
}