#include <Arduino.h>
#include <ModbusMaster.h>
#include <ArduinoOTA.h>
#include "modbus/modbus_handler.h"
#include "modbus/rtc_weather.h"
#include "modbus/psu_service.h"
#include "config_manager.h"
#include "XY-SKxxx.h"
#include "XY-SKxxx_Config.h"
#include "serial_monitor_interface.h"
#include "serial_interface/serial_core.h"
#include "serial_interface/menu_debug.h" // For background bus sniffing (sniffTick)
#include "wifi_interface/wifi_native.h"
#include "wifi_interface/wifi_settings.h"
#include "wifi_interface/captive_portal.h"
#include "mqtt/mqtt_manager.h"
#include "log_utils/log_utils.h"

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

ModbusMaster modbus;

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

  // Initialize the configuration manager (NVS)
  if (!XYConfigManager::begin()) {
    Serial.println("Failed to initialize configuration manager");
  }

  // Load configuration from NVS
  xyConfig = XYConfigManager::loadConfig();

  // Display the loaded configuration
  displayConfig(xyConfig);

  // Create the power supply instance with the loaded configuration
  powerSupply = new XY_SKxxx(xyConfig.rxPin, xyConfig.txPin, xyConfig.slaveId);

  // Initialize the Modbus bus mutex before any task can touch the bus
  initPsuService();

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
  // Priorities: keepalive runs at prio 3 (above the MQTT status poller at 1) so
  // it always wins the Modbus mutex in time - the PSU drops its WiFi tab if the
  // master write is delayed by bus contention.
  xTaskCreatePinnedToCore(wifiHostKeepAliveTask, "wifiHost", 4096, NULL, 3, NULL, 1);

  // Start the WiFi radio in station mode.
  initNativeWifiRadio();

  // Try the saved networks first (priority order).
  if (!connectToSavedNetworks()) {
    Serial.println("No saved network reachable - starting provisioning AP");
    Serial.println("Connect to AP 'XY-SK150-Setup' and open http://192.168.4.1");
    startCaptivePortal();
  }

  // Give WiFi a moment to stabilize
  for(int i=0; i<5; i++) {
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connection stable");
      break;
    }
    Serial.println("Waiting for WiFi to stabilize...");
    delay(1000);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(getWiFiIP());

    // Configure NTP for accurate timestamps and start the background weather
    // client (Open-Meteo refresh for the PSU weather block).
    configureNTP();
    startWeatherClient();
  }

  // OTA over UDP (espota): pio run -e <env> -t upload --upload_port <ip>
  ArduinoOTA.setHostname(mqttDeviceId().c_str());
  ArduinoOTA.begin();
  Serial.printf("ArduinoOTA host: %s\n", mqttDeviceId().c_str());

  Serial.println("\n\n----- XY-SK150 Modbus RTU Control System -----");
  displayDeviceStatus(powerSupply);

  // Initialize serial monitor interface
  Serial.println("\nInitializing serial monitor interface...");
  setupSerialMonitorControl();
  Serial.println("Enter commands to control the power supply.");
  initializeSerialInterface();
}

void loop() {
  // Process serial monitor commands
  checkSerialMonitorInput(powerSupply, xyConfig);

  // Background bus sniffing (non-blocking, keeps keep-alive running)
  sniffTick(powerSupply);

  // OTA service
  ArduinoOTA.handle();

  // Start MQTT as soon as WiFi is up. mqttStart() is idempotent, so this also
  // re-forwards the client after WiFi reconnects (the MQTT task itself keeps
  // reconnecting while connected() == false).
  if (WiFi.status() == WL_CONNECTED) {
    mqttStart();
    // Serve the embedded client + /api/* on the LAN IP so the block can be
    // controlled directly (local mode) even while the broker is unreachable.
    startLocalServer();
  } else {
    stopLocalServer();
  }

  // Periodically read fresh PSU status and publish it over MQTT only when it
  // actually changed (retained, so the server/client always has the last one).
  static unsigned long lastStatusUpdate = 0;
  if (mqttConnected() && millis() - lastStatusUpdate > 250) {
    lastStatusUpdate = millis();
    mqttPublishStatus();
  }

  // React to REG_WIFI_CONFIG changes made on the block (AP / Touch / None).
  static unsigned long lastModeCheck = 0;
  if (millis() - lastModeCheck > 1000) {
    lastModeCheck = millis();
    checkWifiConfigMode();
  }

  // Sync the PSU RTC/weather block (Unix time + weather, ~10s like the OEM
  // XY-WFPOW module). This feeds the standby clock/weather screen. A single
  // failed write (bus timeout under contention) is retried next cycle.
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