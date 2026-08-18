#ifndef MODBUS_RTC_WEATHER_H
#define MODBUS_RTC_WEATHER_H

#include <stdint.h>

// Push local Unix time + weather into the PSU RTC/weather block (0x0200-0x0214,
// 21 registers, fn 0x10) - mimics the Sinilink XY-WFPOW module write frame.
// The block is written roughly every 10s, but only after NTP has valid time.
void syncRtcWeatherToPSU();

// Manual weather override. When true, syncRtcWeatherToPSU() still pushes the
// time but fills the weather part (0x0203-0x0214) from weatherManualRegs[]
// instead of the built-in mock. Lets you poke registers from the serial menu
// without the 10s mock overwriting them.
extern bool weatherManualMode;
extern uint16_t weatherManualRegs[18]; // 18 weather registers (0x0203-0x0214)

// Write the whole 21-register block right now with a custom weather part.
// time/count/status are filled from the RTC like syncRtcWeatherToPSU does.
bool writeWeatherBlockManual(uint16_t weather[18]);

// Start the background Open-Meteo refresh task (safe to call repeatedly).
void startWeatherClient();

#endif // MODBUS_RTC_WEATHER_H