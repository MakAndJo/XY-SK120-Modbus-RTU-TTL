#ifndef XY_SKXXX_CONFIG_H
#define XY_SKXXX_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

// Board-specific pin definitions
#ifdef CONFIG_IDF_TARGET_ESP32S3
    // XIAO ESP32S3 pin mapping
    #define DEFAULT_MODBUS_RX_PIN D7     // Default RX pin (D7 on XIAO ESP32S3)
    #define DEFAULT_MODBUS_TX_PIN D6     // Default TX pin (D6 on XIAO ESP32S3)
    #define DEFAULT_WIFI_RESET_PIN D0    // WiFi reset button pin
    #define BOARD_LED_PIN D10            // Built-in LED
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    // XIAO ESP32C3 pin mapping (using safe GPIO pins)
    #define DEFAULT_MODBUS_RX_PIN 4      // GPIO4 (D2 equivalent)
    #define DEFAULT_MODBUS_TX_PIN 5      // GPIO5 (D3 equivalent)
    #define DEFAULT_WIFI_RESET_PIN 9     // GPIO9 (avoid GPIO0 - strapping pin)
    #define BOARD_LED_PIN 8              // Built-in RGB LED data pin
#else
    // Fallback for unknown ESP32 variants
    #define DEFAULT_MODBUS_RX_PIN 7      // Standard GPIO pins
    #define DEFAULT_MODBUS_TX_PIN 6
    #define DEFAULT_WIFI_RESET_PIN 0
    #define BOARD_LED_PIN 2
#endif

// Common configuration
#define DEFAULT_MODBUS_SLAVE_ID 1    // Default Modbus slave ID
#define DEFAULT_MODBUS_BAUD_RATE 115200  // Default baud rate

// Memory constraints based on board
#ifdef CONFIG_IDF_TARGET_ESP32C3
    #define MAX_CONFIG_SIZE 512          // Smaller config for limited flash
    #define MAX_WIFI_NETWORKS 3          // Reduce stored networks
    #define WEB_SERVER_TIMEOUT 5000      // Shorter timeouts
    #define MODBUS_BUFFER_SIZE 64        // Smaller buffers
    #define MIN_FREE_HEAP 50000          // Minimum heap threshold
#else
    #define MAX_CONFIG_SIZE 2048
    #define MAX_WIFI_NETWORKS 10
    #define WEB_SERVER_TIMEOUT 30000
    #define MODBUS_BUFFER_SIZE 256
    #define MIN_FREE_HEAP 30000
#endif

// NVS namespace for storing settings
#define PREFS_NAMESPACE "xysk120"

// Configuration structure
struct XYModbusConfig {
    uint8_t rxPin;          // RX pin for Modbus communication
    uint8_t txPin;          // TX pin for Modbus communication
    uint8_t slaveId;        // Modbus slave ID
    uint32_t baudRate;      // Baud rate for Modbus communication
    
    // Default constructor uses default values
    XYModbusConfig() :
        rxPin(DEFAULT_MODBUS_RX_PIN),
        txPin(DEFAULT_MODBUS_TX_PIN),
        slaveId(DEFAULT_MODBUS_SLAVE_ID),
        baudRate(DEFAULT_MODBUS_BAUD_RATE)
    {}
};

// Class to manage configuration settings
class XYConfigManager {
public:
    // Initialize the configuration manager
    static bool begin() {
        return _preferences.begin(PREFS_NAMESPACE, false);
    }
    
    // End the configuration manager session
    static void end() {
        _preferences.end();
    }
    
    // Load configuration from NVS
    static XYModbusConfig loadConfig() {
        XYModbusConfig config;
        
        // If a key exists in NVS, read it; otherwise, use the default value
        if (_preferences.isKey("rxPin")) {
            config.rxPin = _preferences.getUChar("rxPin", DEFAULT_MODBUS_RX_PIN);
        }
        
        if (_preferences.isKey("txPin")) {
            config.txPin = _preferences.getUChar("txPin", DEFAULT_MODBUS_TX_PIN);
        }
        
        if (_preferences.isKey("slaveId")) {
            config.slaveId = _preferences.getUChar("slaveId", DEFAULT_MODBUS_SLAVE_ID);
        }
        
        if (_preferences.isKey("baudRate")) {
            config.baudRate = _preferences.getULong("baudRate", DEFAULT_MODBUS_BAUD_RATE);
        }
        
        return config;
    }
    
    // Save configuration to NVS
    static bool saveConfig(const XYModbusConfig& config) {
        _preferences.putUChar("rxPin", config.rxPin);
        _preferences.putUChar("txPin", config.txPin);
        _preferences.putUChar("slaveId", config.slaveId);
        _preferences.putULong("baudRate", config.baudRate);
        
        return true;
    }
    
    // Reset configuration to defaults
    static bool resetConfig() {
        XYModbusConfig defaultConfig;
        return saveConfig(defaultConfig);
    }
    
    // Check if configuration exists in NVS
    static bool configExists() {
        return _preferences.isKey("rxPin") || 
               _preferences.isKey("txPin") || 
               _preferences.isKey("slaveId") || 
               _preferences.isKey("baudRate");
    }

private:
    // Static member only declared here, defined elsewhere
    static Preferences _preferences;
};

#endif // XY_SKXXX_CONFIG_H
