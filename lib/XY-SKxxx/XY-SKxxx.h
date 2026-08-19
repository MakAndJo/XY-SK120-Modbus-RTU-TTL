#ifndef XY_SKXXX_H
#define XY_SKXXX_H

#include <Arduino.h>
#include <ModbusMaster.h>
#include "XY-SKxxx-cd-data-group.h" // Add this include for Memory Group definitions

// Define Modbus register addresses (follow protocol naming convention, p.3 of documentation)
#define REG_V_SET 0x0000        // Voltage setting, 2 bytes, 2 decimal places, unit: V, Read and Write
#define REG_I_SET 0x0001        // Current setting, 2 bytes, 3 decimal places, unit: A, Read and Write

#define REG_VOUT 0x0002         // Output voltage display value, 2 bytes, 2 decimal places, unit: V, Read only
#define REG_IOUT 0x0003         // Output current display value, 2 bytes, 3 decimal places, unit: A, Read only

#define REG_POWER 0x0004        // Output power display value, 2 bytes, 2 decimal places, unit: W, Read only
#define REG_UIN 0x0005          // Input voltage display value, 2 bytes, 2 decimal places, unit: V, Read only

#define REG_AH_LOW 0x0006       // Amp-hour low register, 2 bytes, 0 decimal places, unit: mAh, Read only
#define REG_AH_HIGH 0x0007      // Amp-hour high register, 2 bytes, 0 decimal places, unit: mAh, Read only

#define REG_WH_LOW 0x0008       // Watt-hour low register, 2 bytes, 0 decimal places, unit: mWh, Read only
#define REG_WH_HIGH 0x0009      // Watt-hour high register, 2 bytes, 0 decimal places, unit: mWh, Read only

#define REG_OUT_H 0x000A        // Hours of output time, 2 bytes, 0 decimal places, unit: h, Read only
#define REG_OUT_M 0x000B        // Minutes of output time, 2 bytes, 0 decimal places, unit: min, Read only
#define REG_OUT_S 0x000C        // Seconds of output time, 2 bytes, 0 decimal places, unit: s, Read only

#define REG_T_IN 0x000D         // Internal temperature, 2 bytes, 1 decimal place, unit: °F / °C, Read only
#define REG_T_EX 0x000E         // External temperature, 2 bytes, 1 decimal place, unit: °F / °C, Read only

#define REG_LOCK 0x000F         // Key lock status, 2 bytes, 0 decimal places, unit: 0/1 (0: unlock, 1: lock), Read and Write
#define REG_PROTECT 0x0010      // WIP: Protection status???, 2 bytes, 0 decimal places, unit: 0/1, Read and Write - XY6020L-Modbus-Interface.pdf: note 3

#define REG_CVCC 0x0011         // CC/CV (constant current / constant voltage) mode status, 2 bytes, 0 decimal places, unit: 0/1, Read only

#define REG_ONOFF 0x0012        // Output on/off status, 2 bytes, 0 decimal places, unit: 0/1, Read and Write

#define REG_F_C 0x0013          // temperature unit, 2 bytes, 0 decimal places, unit: 0=Celsius, 1=Fahrenheit, Read and Write

#define REG_B_LED 0x0014        // WIP: Backlight brightness, 2 bytes, 0 decimal places, unit: 0-5, Read and Write, factory default: 5 (brightest)

#define REG_SLEEP 0x0015        // Sleep timeout, 2 bytes, 0 decimal places, unit: min, Read and Write, factory default: 2 min

#define REG_MODEL 0x0016        // WIP: Model number: XY-SK120 returns 22873, 2 bytes, 0 decimal places, unit: ???, Read only
#define REG_VERSION 0x0017      // DPS Firmware Version number, 2 bytes, 0 decimal places, unit: ???, Read only

#define REG_SLAVE_ADDR 0x0018   // Modbus Slave address, 2 bytes, 0 decimal places, unit: 0-247, Read and Write, factory default: 1
#define REG_BAUDRATE_L 0x0019   // Baud rate setting, 2 bytes, 0 decimal places, unit: 0-8 (0: 9600, 1: 14400, 2: 19200, 3: 38400, 4: 56000, 5: 576000, 6: 115200, 7: 2400, 8: 4800), Read and Write, factory default: 6 (115200)

#define REG_T_IN_CAL 0x001A     // Internal temperature calibration, 2 bytes, 1 decimal place, unit: °F / °C, Read and Write
#define REG_T_EXT_CAL 0x001B    // External temperature calibration, 2 bytes, 1 decimal place, unit: °F / °C, Read and Write

#define REG_EXTRACT_M 0x001D    // Data group selection, 2 bytes, 0 decimal places, unit: 0-9, Read and Write

#define REG_SYS_STATUS 0x001E   // WIP: System status???, 2 bytes, 0 decimal places, unit: ???, Read and Write

// Additional Modbus register addresses for XY-SK120 0x0030 - 0x0034, will not implement these as it's related to the Sinilink ESP8285H16 module

// Additional Modbus register addresses for XY-SK120 0x0050 - 0x005D
#define REG_CV_SET         0x0050    // CV (constant voltage) setting, 2 bytes, 2 decimal places, unit: V, Read and Write
#define REG_CC_SET         0x0051    // CC (constant current) setting, 2 bytes, 3 decimal places, unit: A, Read and Write

#define REG_S_LVP          0x0052    // LVP (input low voltage protection) setting, 2 bytes, 2 decimal places, unit: V, Read and Write
#define REG_S_OVP          0x0053    // OVP (output over voltage protection) setting, 2 bytes, 2 decimal places, unit: V, Read and Write

#define REG_S_OCP          0x0054    // OCP (output over current protection) setting, 2 bytes, 3 decimal places, unit: A, Read and Write
#define REG_S_OPP          0x0055    // OPP (output over power protection) setting, 2 bytes, 1 decimal place, unit: W, Read and Write

#define REG_S_OHP_H        0x0056    // OHP_H (output high power protection - hours) setting, 2 bytes, 0 decimal places, unit: h, Read and Write
#define REG_S_OHP_M        0x0057    // OHP_M (output high power protection - minutes) setting, 2 bytes, 0 decimal places, unit: min, Read and Write

#define REG_S_OAH_L        0x0058    // OAH_LOW (over-amp-hour protection - low) setting, 2 bytes, 0 decimal places, unit: mAh, Read and Write
#define REG_S_OAH_H        0x0059    // OAH_HIGH (over-amp-hour protection - high) setting, 2 bytes, 0 decimal places, unit: mAh, Read and Write

#define REG_S_OWH_L        0x005A    // OWH_LOW (over-watt-hour protection - low) setting, 2 bytes, 0 decimal places, unit: 10mWh, Read and Write
#define REG_S_OWH_H        0x005B    // OWH_HIGH (over-watt-hour protection - high) setting, 2 bytes, 0 decimal places, unit: 10mWh, Read and Write

#define REG_S_OTP          0x005C   // Over temperature protection setting, 2 bytes, 1 decimal place, unit: °F / °C, Read and Write

#define REG_S_INI          0x005D    // Power-on initialization setting, 2 bytes, 0 decimal places, unit: 0/1 (0: output off upon power on, 1: output on upon power on), Read and Write

// ============================================================================
// RTC / Weather block (0x0200 - 0x0214) - discovered by sniffing the Sinilink
// XY-WFPOW WiFi module against an XY-SK150S. The module (acting as Modbus MASTER,
// slave addr 0x01) writes this whole 21-register / 42-byte block with function
// 0x10 (write multiple) roughly every 10 seconds:
//
//   01 10 02 00 00 15 2A <42 bytes> CRC
//
// The 42 bytes are byte-for-byte the weather struct (DAT_40202894) filled by the
// module's JSON parser. NOTE: this is the REAL register address used by the WiFi
// module - NOT 0x0100/0x0103 RTC or 0x0110/0x011D weather from the XY-SK120 docs.
// ============================================================================
#define REG_RTC_TIME_LO    0x0200  // Unix epoch seconds, low  16 bits (time & 0xFFFF)
#define REG_RTC_TIME_HI    0x0201  // Unix epoch seconds, high 16 bits (time >> 16)
#define REG_RTC_STATUS     0x0202  // Time/weather sync status, 0x0001=connecting, 0x0002=IP obtained, 0x0003=time synced
#define REG_WX_TODAY_CODE  0x0203  // Today weather condition code (see icon map below).
                                   // Icon map (discovered by writing one code at a time):
                                   //   0=луна           1=два облака         2=облако
                                   //   3=два обл.+солнце 4=луна за облаком   5=солнце
                                   //   6=солнце за 2 обл. 7=солнце за обл.   8=обл.4капли+луна
                                   //   9=обл.4капли+солн. 10=обл.2кап.+гроза  11=обл.4кап.+гроза
                                   //  12=пусто          13=обл.1точка        14=обл.3точки
                                   //  15=обл.5точек     16=6 точек           17=обл.5точек
                                   //  18=обл.7точек     19=обл.4лин.+3т.     20=обл.7палок
                                   //  21=обл.5точек     22=обл.2т.+1палка    23=обл.3т.+1палка
                                   //  24=обл.4т.+палка  25=обл.4т.+палка     26=5 палок
                                   //  27=обл.луна+1т.   28=обл.луна+5т.      29=три капли
                                   //  30=обл.1снеж.     31=обл.3снеж.        32=обл.5снеж.
                                   //  33=обл.7снеж.     34=обл.3кап.+2снеж.  35=снеж./капля
                                   //  36=луна обл.снеж. 37=3 снежинки        38=обл.снеж./снеж.
                                   //  39=обл.снеж./2снеж. 40=обл.2снеж./2снеж. 41=обл.луна кап. снеж.
                                   //  42=луна обл.снеж. 43=3 снежинки        44=круг 3 гориз.палки
                                   //  45=3 гориз.палки  46=бесконечность     47=ураган
                                   //  48=6т.+1 гориз.п. 49=ветер три         50=смерч
                                   //  51=смерч          52=5 гориз.п.с прорез. 53=беск.+4т.
                                   //  54=беск.+6т.      55=беск.+8т.         56=5 гориз.палок
                                   //  57=5 гор.палок смещ. 58=градусник+     59=градусник-
                                   //  60=N/A (0x3C error/unset)
#define REG_WX_TODAY_TEMPH 0x0204  // Today high temperature (short, parser, °C)
#define REG_WX_TODAY_TEMPL 0x0205  // Today low temperature (short, parser, °C)
#define REG_WX_TODAY_FLAG  0x0206  // Discovered: rendered as "NNc" next to high/low on screen -> current temp (°C)
#define REG_WX_TODAY_WCODE 0x0207  // Discovered: rendered as "NN%" on screen -> humidity (%)
#define REG_WX_TODAY_RES0  0x0208  // Today reserved (parser offset +0x10)
#define REG_WX_TODAY_WLEVEL 0x0209 // Today wind level (windLevel)
#define REG_WX_TODAY_WL_HI 0x020A  // Today wind level, high word
#define REG_WX_TODAY_WL_LO 0x020B  // Today wind level, low word
#define REG_WX_DAY1_CODE   0x020C  // Day 1 forecast weather code
#define REG_WX_DAY1_TEMPH  0x020D  // Day 1 forecast high temp
#define REG_WX_DAY1_TEMPL  0x020E  // Day 1 forecast low temp
#define REG_WX_DAY2_CODE   0x020F  // Day 2 forecast weather code
#define REG_WX_DAY2_TEMPH  0x0210  // Day 2 forecast high temp
#define REG_WX_DAY2_TEMPL  0x0211  // Day 2 forecast low temp
#define REG_WX_DAY3_CODE   0x0212  // Day 3 forecast weather code
#define REG_WX_DAY3_TEMPH  0x0213  // Day 3 forecast high temp
#define REG_WX_DAY3_TEMPL  0x0214  // Day 3 forecast low temp

// Legacy note: XY-SK120 docs mention RTC at 0x0100-0x0103 and weather at
// 0x0110-0x011D, but the real XY-WFPOW module writes these into 0x0200-0x0214.

/* Below are undocumented registers, available in the XY-SK120 manual and OSD (On-Screen Display) 
but not in the Modbus register map documentation
*/

#define REG_S_ETP           0x005E    // WIP: Not implemented.
// REG_S_ETP: both 0x005E and 0x005F stores the ETP (External Temperature Protection) value, but 0x005E is read-write and 0x005F is read-only and both values seems to be mirrored. 
// However, writing to 0x005E does not seem to have any effect on the device, so it's likely not implemented or used.

// beeper settings (beeper enable)
#define REG_BEEPER          0x001C       // Beeper enable/disable, 2 bytes, 0 decimal places, unit: 0/1, Read and Write

// RET setting (Restore factory settings) (discovered through testing)
#define REG_FACTORY_RESET   0x0025 // Factory reset, write 0x1 to trigger reset to defaults

// FET setting (quick adjustment of voltage, current or power with the rotary encoder knob)
// Cannot probe the register for FET setting despite the best effort, so it's not implemented

// PPT Setting (MPPT Solar Charging Settings)
#define REG_MPPT_ENABLE     0x001F  // MPPT enable/disable, 2 bytes, 0 decimal places, unit: 0/1, Read and Write
#define REG_MPPT_THRESHOLD  0x0020 // MPPT threshold percentage, 2 bytes, 2 decimal places, unit: ratio (0.00-1.00), Read and Write

// BTF setting (Battery Full)
#define REG_BTF             0x0021 // Battery charge cut off current, 2 bytes, 3 decimal places, unit: A, Read and Write, set 0 to turn off

// CP Setting (Constant Power Mode)
#define REG_CP_ENABLE     0x0022  // Constant Power mode enable/disable, 2 bytes, 0 decimal places, unit: 0/1, Read and Write
#define REG_CP_SET        0x0023  // Constant Power setting, 2 bytes, 1 decimal place, unit: W, Read and Write

// BCH/BTF/CLOF settings (battery charging / output-off on group change)
// Discovered from SK150/SK150S ESPHome integration, not in the official XY-SK120 register map
#define REG_BCH_ENABLE     0x0029  // BCH enable (battery charging), 0/1, Read and Write
#define REG_BCH_THRESHOLD  0x002A  // BCH threshold, 2 decimal places, unit: V, Read and Write
#define REG_BTF_ENABLE     0x002B  // BTF enable (battery cutoff), 0/1, Read and Write
#define REG_BTF_CUTOFF     0x002C  // BTF cutoff current, 3 decimal places, unit: A, Read and Write
#define REG_CLOF_ENABLE    0x002D  // CLOF (force output off on memory group switch), 0/1, Read and Write

// Host / WiFi module registers (Sinilink ESP8285H16 / XY-WFPOW). Writing
// {0x3B3A, 2, 4, ip_hi, ip_lo} to 0x0030-0x0034 activates the WiFi host.
#define REG_MASTER     0x0030  // Host type, 0x3B3A = WiFi host, Read and Write
#define REG_WIFI_CONFIG 0x0031 // WiFi configuration status, 0=Invalid, 1=Pairing, 2=Valid, Read and Write
#define REG_WIFI_STATUS 0x0032 // WiFi status, 0=Invalid, 1=Router, 2=Server, 3=Touch, 4=AP/Connected, 5=Online, Read and Write
#define REG_IPV4_H      0x0033  // IP address high word (octet1<<8 | octet2), Read and Write
#define REG_IPV4_L      0x0034  // IP address low word (octet3<<8 | octet4), Read and Write

// BCH setting (Battery Charging)


// CLU setting (Calibrate output voltage)

// CLA setting (Calibrate output current)

// Zero setting (Current zero calibration)

// CLOF setting (Force power output off when switching data sets)
// Cannot probe the register for CLOF setting despite the best effort, so it's not implemented

// POFF (Shutdown function) (On: Enable the shutdown function by pressing the OSD power button for 5 seconds, Off: Disable the shutdown function, cannot locate register)
// Cannot probe the register for POFF setting despite the best effort, so it's not implemented

// Device status cache structure
struct DeviceStatus {
  // Output measurements
  float outputVoltage;     // Current output voltage (V)
  float outputCurrent;     // Current output current (A)
  float outputPower;       // Current output power (W)
  float inputVoltage;      // Input voltage (V)
  
  // Energy measurements
  // WIP: understand why a low and high registers for amp-hours and watt-hours is needed
  uint32_t ampHours;       // Accumulated amp-hours (mAh)
  uint32_t wattHours;      // Accumulated watt-hours (mWh)
  uint32_t outputTime;     // Output time in seconds
  
  // Temperature readings
  float internalTemp;      // Internal temperature (°C/°F)
  float externalTemp;      // External temperature (°C/°F)
  
  // Device state
  bool outputEnabled;      // Output state (on/off)
  bool keyLocked;          // Key lock status
  uint16_t protectionStatus; // Protection status
  uint16_t cvccMode;       // CC/CV mode (0: CV, 1: CC)
  uint16_t systemStatus;   // System status
  
  // Device settings
  float setVoltage;        // Set voltage (V)
  float setCurrent;        // Set current (A)
  uint8_t backlightLevel;  // Backlight level
  uint8_t sleepTimeout;    // Sleep timeout in minutes

  // Add Constant Power mode related fields
  bool cpModeEnabled;       // Constant Power mode enabled state
  float constantPower;      // Constant Power setting (W)
};

// Protection settings cache structure
struct ProtectionSettings {
  // Constant Voltage/Current settings
  float constantVoltage;    // CV setting (V)
  float constantCurrent;    // CC setting (A)
  
  // Protection thresholds
  float lowVoltageProtection;   // Input low voltage protection (LVP) (V)
  float overVoltageProtection;   // Output over voltage protection (V)
  float overCurrentProtection;   // Output over current protection (A)
  float overPowerProtection;     // Output over power protection (W)
  
  // Time-based protection
  uint16_t highPowerHours;       // Output high power protection hours
  uint16_t highPowerMinutes;     // Output high power protection minutes
  
  // Energy-based protection
  uint16_t overAmpHoursLow;      // Over-amp-hour protection low
  uint16_t overAmpHoursHigh;     // Over-amp-hour protection high
  uint16_t overWattHoursLow;     // Over-watt-hour protection low
  uint16_t overWattHoursHigh;    // Over-watt-hour protection high
  
  // Temperature protection
  float overTemperature;         // Over temperature protection (°C/°F)
  
  // Initialization setting
  bool outputOnAtStartup;        // Power-on initialization setting
  
  // Battery settings
  float batteryCutoffCurrent;    // Battery charge cutoff current (A)
};

// Operating mode enum
enum OperatingMode {
  MODE_CV = 0,  // Constant Voltage
  MODE_CC = 1,  // Constant Current
  MODE_CP = 2   // Constant Power
};

class XY_SKxxx {
public:
  XY_SKxxx(uint8_t rxPin, uint8_t txPin, uint8_t slaveID);
  void begin(long baudRate);
  bool testConnection();
  
  // Basic device information
  uint16_t getModel();
  uint16_t getVersion();
  
  // Status cache methods
  bool updateAllStatus(bool force = false);
  bool updateOutputStatus(bool force = false);
  bool updateDeviceSettings(bool force = false);
  bool updateEnergyMeters(bool force = false);
  bool updateTemperatures(bool force = false);
  bool updateDeviceState(bool force = false);
  
  // Cached value access methods
  float getOutputVoltage(bool refresh = false);
  float getOutputCurrent(bool refresh = false);
  float getOutputPower(bool refresh = false);
  float getInputVoltage(bool refresh = false);
  uint32_t getAmpHours(bool refresh = false);
  uint32_t getWattHours(bool refresh = false);
  uint32_t getOutputTime(bool refresh = false);
  float getInternalTemperature(bool refresh = false);
  float getExternalTemperature(bool refresh = false);
  bool isOutputEnabled(bool refresh = false);
  bool isKeyLocked(bool refresh = false);
  uint16_t getProtectionStatus(bool refresh = false);
  bool isInConstantCurrentMode(bool refresh = false);
  bool isInConstantVoltageMode(bool refresh = false);
  float getSetVoltage(bool refresh = false);
  float getSetCurrent(bool refresh = false);
  
  // Output settings
  bool setVoltage(float voltage);
  bool setCurrent(float current);
  bool getOutput(float &voltage, float &current, float &power);
  
  // Combined measurement methods for convenience
  bool getMeasurements(float &outVoltage, float &outCurrent, float &outPower, 
                      float &inVoltage, bool refresh = true);
  bool getEnergyMeasurements(uint32_t &ampHours, uint32_t &wattHours, 
                           uint32_t &outputTime, bool refresh = true);
  bool getTemperatures(float &internalTemp, float &externalTemp, bool refresh = true);
  
  // System control
  bool setKeyLock(bool lock);
  uint16_t getCVCCState(bool refresh = false);
  bool setOutputState(bool on);

  // Device settings - direct register access without caching
  bool setBacklightBrightness(uint8_t level);
  uint8_t getBacklightBrightness();
  bool setSleepTimeout(uint8_t minutes);
  uint8_t getSleepTimeout();
  bool setSlaveAddress(uint8_t address);
  bool getSlaveAddress(uint8_t &address);
  bool setBaudRate(uint8_t baudRate);
  uint8_t getBaudRateCode();
  long getActualBaudRate();
  bool setBeeper(bool enabled);
  bool getBeeper(bool &enabled);
  bool setTemperatureUnit(bool celsius);
  bool getTemperatureUnit(bool &celsius);
  
  // MPPT (Maximum Power Point Tracking) settings
  bool setMPPTEnable(bool enabled);
  bool getMPPTEnable(bool &enabled);
  bool setMPPTThreshold(float threshold);
  bool getMPPTThreshold(float &threshold);
  
  // Temperature calibration
  bool setInternalTempCalibration(float offset);
  bool setExternalTempCalibration(float offset);
  float getInternalTempCalibration(bool refresh = false);
  float getExternalTempCalibration(bool refresh = false);

  // System status
  uint16_t getSystemStatus(bool refresh = false);
  bool setProtectionStatus(uint16_t status);
  bool setSystemStatus(uint16_t status);
  
  // Higher-level convenience methods with built-in timing
  bool setVoltageAndCurrent(float voltage, float current);
  bool turnOutputOn();
  bool turnOutputOff();
  bool getOutputStatus(float &voltage, float &current, float &power, bool &isOn);
  
  // Improved Modbus RTU timing methods
  unsigned long silentInterval(unsigned long baudRate);
  void waitForSilentInterval();
  bool preTransmission();
  bool postTransmission();
  
  // Protection settings methods
  bool setOverVoltageProtection(float voltage);
  bool setOverCurrentProtection(float current);
  bool setOverPowerProtection(float power);
  bool setLowVoltageProtection(float voltage);
  
  bool getOverVoltageProtection(float &voltage);
  bool getOverCurrentProtection(float &current);
  bool getOverPowerProtection(float &power);
  bool getLowVoltageProtection(float &voltage);
  
  // Amp-hour protection methods
  bool setOverAmpHourProtection(uint16_t ampHoursLow, uint16_t ampHoursHigh);
  bool getOverAmpHourProtection(uint16_t &ampHoursLow, uint16_t &ampHoursHigh);

  // Watt-hour protection methods
  bool setOverWattHourProtection(uint16_t wattHoursLow, uint16_t wattHoursHigh);
  bool getOverWattHourProtection(uint16_t &wattHoursLow, uint16_t &wattHoursHigh);

  // High power protection time methods
  bool setHighPowerProtectionTime(uint16_t hours, uint16_t minutes);
  bool getHighPowerProtectionTime(uint16_t &hours, uint16_t &minutes);

  // Over-temperature protection methods
  bool setOverTemperatureProtection(float temperature);
  bool getOverTemperatureProtection(float &temperature);

  // Power-on initialization setting methods
  bool setPowerOnInitialization(bool outputOnAtStartup);
  bool getPowerOnInitialization(bool &outputOnAtStartup);

  // Constant Voltage (CV) and Constant Current (CC) mode methods
  bool setConstantVoltage(float voltage);
  bool getConstantVoltage(float &voltage);
  bool setConstantCurrent(float current);
  bool getConstantCurrent(float &current);
  
  // Constant Power (CP) mode methods
  bool setConstantPowerMode(bool enabled);
  bool getConstantPowerMode(bool &enabled);
  bool isConstantPowerModeEnabled(bool refresh = false);
  
  bool setConstantPower(float power);
  bool getConstantPower(float &power);
  float getCachedConstantPower(bool refresh = false);
  
  // Protection cache methods
  bool updateAllProtectionSettings(bool force = false);
  bool updateConstantVoltageCurrentSettings(bool force = false);
  bool updateVoltageCurrentProtection(bool force = false);
  bool updatePowerProtection(bool force = false);
  bool updateEnergyProtection(bool force = false);
  bool updateTemperatureProtection(bool force = false);
  bool updateStartupSetting(bool force = false);
  
  // Cached protection value access methods
  float getCachedConstantVoltage(bool refresh = false);
  float getCachedConstantCurrent(bool refresh = false);
  float getCachedLowVoltageProtection(bool refresh = false);
  float getCachedOverVoltageProtection(bool refresh = false);
  float getCachedOverCurrentProtection(bool refresh = false);
  float getCachedOverPowerProtection(bool refresh = false);
  void getCachedHighPowerProtectionTime(uint16_t &hours, uint16_t &minutes, bool refresh = false);
  void getCachedOverAmpHourProtection(uint16_t &ampHoursLow, uint16_t &ampHoursHigh, bool refresh = false);
  void getCachedOverWattHourProtection(uint16_t &wattHoursLow, uint16_t &wattHoursHigh, bool refresh = false);
  float getCachedOverTemperatureProtection(bool refresh = false);
  bool getCachedPowerOnInitialization(bool refresh = false);

  // Add direct register access methods for memory groups
  bool readRegisters(uint16_t addr, uint16_t count, uint16_t* buffer);
  bool readRegister(uint16_t addr, uint16_t& value); // Add this method
  bool writeRegister(uint16_t addr, uint16_t value);
  bool writeRegisters(uint16_t addr, uint16_t count, uint16_t* buffer);

  // Make ModbusMaster available to external code
  ModbusMaster modbus;

  // Debug functions for direct register access
  bool debugReadRegisters(uint16_t addr, uint8_t count, uint16_t* values);
  bool debugWriteRegister(uint16_t addr, uint16_t value);
  bool debugWriteRegisters(uint16_t addr, uint8_t count, const uint16_t* values);

  // Read input registers (Modbus function code 0x04). Used to probe for
  // RTC/weather areas (0x0100+) which may live in the input-register space.
  bool readInputRegisters(uint16_t addr, uint16_t count, uint16_t* buffer);

  // Memory Group Methods
  
  /**
   * Read all registers from a memory group
   * 
   * @param group Memory group to read from
   * @param data Array to store the read data (must be able to hold DATA_GROUP_REGISTERS values)
   * @param force Force read from device even if cache is valid
   * @return true if successful
   */
  bool readMemoryGroup(xy_sk::MemoryGroup group, uint16_t* data, bool force = false);
  
  /**
   * Write all registers to a memory group
   * 
   * @param group Memory group to write to
   * @param data Array containing the data to write (must contain DATA_GROUP_REGISTERS values)
   * @return true if successful
   */
  bool writeMemoryGroup(xy_sk::MemoryGroup group, const uint16_t* data);
  
  /**
   * Call a memory group to make it active (copy to M0)
   * 
   * @param group Memory group to call (M1-M9, M0 is ignored)
   * @return true if successful
   */
  bool callMemoryGroup(xy_sk::MemoryGroup group);
  
  /**
   * Read a specific register from a memory group
   * 
   * @param group Memory group to read from
   * @param regOffset Specific register offset from GroupRegisterOffset enum
   * @param value Reference to store the read value
   * @return true if successful
   */
  bool readGroupRegister(xy_sk::MemoryGroup group, xy_sk::GroupRegisterOffset regOffset, uint16_t& value);
  
  /**
   * Write to a specific register in a memory group
   * 
   * @param group Memory group to write to
   * @param regOffset Specific register offset from GroupRegisterOffset enum
   * @param value Value to write
   * @return true if successful
   */
  bool writeGroupRegister(xy_sk::MemoryGroup group, xy_sk::GroupRegisterOffset regOffset, uint16_t value);
  
  /**
   * Get memory group data from cache, optionally refreshing from device
   * 
   * @param group Memory group to get
   * @param data Array to store the group data
   * @param refresh Whether to refresh the cache from device
   * @return true if successful
   */
  bool getCachedMemoryGroup(xy_sk::MemoryGroup group, uint16_t* data, bool refresh = false);
  
  /**
   * Update the memory group cache from the device
   * 
   * @param group Memory group to update
   * @param force Force update even if cache is still valid
   * @return true if successful
   */
  bool updateMemoryGroupCache(xy_sk::MemoryGroup group, bool force = false);

  // Factory reset method
  bool restoreFactoryDefaults();

  // Battery cutoff current methods
  bool setBatteryCutoffCurrent(float current);
  bool getBatteryCutoffCurrent(float &current);
  float getCachedBatteryCutoffCurrent(bool refresh = false);
  bool updateBatteryCutoffCurrent(bool force = false);

  // Battery charging / output-off (BCH/BTF/CLOF) settings methods
  bool setBatteryChargingEnable(bool enabled);
  bool getBatteryChargingEnable(bool &enabled);
  bool setBatteryChargingThreshold(float threshold);
  bool getBatteryChargingThreshold(float &threshold);
  bool setBatteryCutoffEnable(bool enabled);
  bool getBatteryCutoffEnable(bool &enabled);
  bool setBatteryCutoffCurrentBtf(float current);
  bool getBatteryCutoffCurrentBtf(float &current);
  bool setOutputOffOnGroupChange(bool enabled);
  bool getOutputOffOnGroupChange(bool &enabled);

  // Host / WiFi module methods
  bool setWifiHostInfo(uint16_t hostType, uint16_t wifiConfig, uint16_t wifiStatus,
                       uint32_t ipv4);
  bool getWifiHostInfo(uint16_t &hostType, uint16_t &wifiConfig, uint16_t &wifiStatus,
                       uint32_t &ipv4);
  bool activateWifiModule(uint32_t ipv4);
  bool setMasterCode(uint16_t hostType);

  // Operating mode access method
  OperatingMode getOperatingMode(bool refresh = false);

private:
  uint8_t _rxPin;
  uint8_t _txPin;
  uint8_t _slaveID;
  unsigned long _baudRate;
  unsigned long _lastCommsTime;
  unsigned long _silentIntervalTime;
  
  // Cache management
  DeviceStatus _status;
  ProtectionSettings _protection;
  unsigned long _lastOutputUpdate;
  unsigned long _lastSettingsUpdate;
  unsigned long _lastEnergyUpdate;
  unsigned long _lastTempUpdate;
  unsigned long _lastStateUpdate;
  unsigned long _cacheTimeout;
  bool _cacheValid;
  unsigned long _lastConstantVCUpdate;
  unsigned long _lastVoltageCurrentProtectionUpdate;
  unsigned long _lastPowerProtectionUpdate;
  unsigned long _lastEnergyProtectionUpdate;
  unsigned long _lastTempProtectionUpdate;
  unsigned long _lastStartupSettingUpdate;
  
  // Static members for callbacks
  static XY_SKxxx* _instance;
  static void staticPreTransmission();
  static void staticPostTransmission();

  // Additional cache fields 
  float _internalTempCalibration;
  float _externalTempCalibration;
  bool _beeperEnabled;
  uint8_t _selectedDataGroup;
  bool _mpptEnabled;         // Add MPPT enable state cache
  float _mpptThreshold;      // Add MPPT threshold cache
  unsigned long _lastCalibrationUpdate;
  
  // Additional cache timestamps
  unsigned long _lastBatteryCutoffUpdate;
  
  // Communication settings cache
  uint8_t _cachedSlaveAddress;
  uint8_t _cachedBaudRateCode;
  unsigned long _lastCommunicationSettingsUpdate;

  // Update methods for new cached values
  bool updateCalibrationSettings(bool force = false);
  bool updateCommunicationSettings(bool force = false);

  // Memory group cache to avoid repeated reads
  xy_sk::MemoryGroupData groupCache[10]; // 10 groups: M0-M9

  // Add CP mode cache management
  bool updateConstantPowerSettings(bool force = false);
  unsigned long _lastConstantPowerUpdate;
};

#endif // XY_SKXXX_H