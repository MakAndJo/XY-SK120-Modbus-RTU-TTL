#include "rtc_weather.h"
#include <Arduino.h>
#include <time.h>
#include "XY-SKxxx.h"
#include "log_utils/log_utils.h"
#include "modbus/weather_api.h"

// Declare external power supply instance (defined in main.cpp)
extern XY_SKxxx* powerSupply;

// Recursive Modbus-bus mutex, defined in psu_service.cpp
#include "modbus/psu_service.h"

// Manual weather override (see header).
bool weatherManualMode = false;
uint16_t weatherManualRegs[18] = {0};

// ---------------------------------------------------------------------------
// Mock weather values (used only before the first real API fetch, or when the
// user puts weather into "manual" mode). See wmoToPsuIcon() for the icon map.
// ---------------------------------------------------------------------------
void fillMockWeather(uint16_t regs[21]) {
  // Today (0x0203-0x020B): code, high temp, low temp, current temp, humidity, -,
  // wind level (lo), wind level (hi), wind level extra
  regs[3]  = 0x0005;                                        // today code: sun
  regs[4]  = (uint16_t)(int16_t)25;                         // high temperature (°C)
  regs[5]  = (uint16_t)(int16_t)17;                         // low temperature (°C)
  regs[6]  = (uint16_t)(int16_t)26;                         // current temperature (°C), shown as "NNc"
  regs[7]  = 0x0042;                                        // humidity %, shown as "NN%"
  regs[8]  = 0x0000;                                        // reserved (not rendered)
  regs[9]  = 0x0002;                                        // wind level 2
  regs[10] = 0x0000;                                        // wind level high word
  regs[11] = 0x0000;                                        // wind level low word

  // Forecast days (0x020C-0x0214): code, high temp, low temp.
  regs[12] = 0x0002;  regs[13] = (uint16_t)(int16_t)24;  regs[14] = (uint16_t)(int16_t)16;  // day 1
  regs[15] = 0x0002;  regs[16] = (uint16_t)(int16_t)23;  regs[17] = (uint16_t)(int16_t)15;  // day 2
  regs[18] = 0x0002;  regs[19] = (uint16_t)(int16_t)22;  regs[20] = (uint16_t)(int16_t)14;  // day 3
}

void fillFromWeatherCache(uint16_t regs[21], WeatherNow now, WeatherDay days[3]) {
  // Day/night from the local hour.
  struct tm tinfo;
  time_t t = time(nullptr);
  localtime_r(&t, &tinfo);
  bool isNight = (tinfo.tm_hour < 6 || tinfo.tm_hour >= 21);

  // Today: weather code -> icon, high/low from today, current + humidity
  regs[3]  = wmoToPsuIcon(now.wmoCode, isNight);
  regs[4]  = (uint16_t)(int16_t)days[0].tMax;
  regs[5]  = (uint16_t)(int16_t)days[0].tMin;
  regs[6]  = (uint16_t)(int16_t)now.tNow;      // current temp -> "NNc"
  regs[7]  = (uint16_t)now.humidity;           // humidity -> "NN%"
  regs[8]  = 0x0000;
  regs[9]  = 0x0000;
  regs[10] = 0x0000;
  regs[11] = 0x0000;

  // Forecast days (each: icon for that day's code, max, min)
  for (int i = 0; i < 3; i++) {
    int base = 12 + i * 3;
    regs[base]     = wmoToPsuIcon(days[i].wmoCode, false);
    regs[base + 1] = (uint16_t)(int16_t)days[i].tMax;
    regs[base + 2] = (uint16_t)(int16_t)days[i].tMin;
  }
}

bool writeWeatherBlockManual(uint16_t weather[18]) {
  if (!powerSupply) return false;

  time_t now = time(nullptr);
  if (now < 1000000000) return false;

  uint32_t t = (uint32_t)(now + gmtOffset_sec + daylightOffset_sec);
  uint16_t regs[21] = {0};
  regs[0] = t & 0xFFFF;
  regs[1] = (t >> 16) & 0xFFFF;
  regs[2] = 0x0003;

  for (int i = 0; i < 18; i++) {
    regs[3 + i] = weather[i];
  }

  lockModbus();
  bool ok = powerSupply->writeRegisters(REG_RTC_TIME_LO, 21, regs);
  unlockModbus();
  return ok;
}

void syncRtcWeatherToPSU() {
  if (!powerSupply) return;

  time_t now = time(nullptr);
  if (now < 1000000000) return; // NTP not synced yet, don't push garbage

  // configTime() only sets the TZ env var - time() stays UTC. The PSU
  // screensaver shows the raw epoch as wall clock, so push LOCAL time.
  uint32_t t = (uint32_t)(now + gmtOffset_sec + daylightOffset_sec);
  uint16_t regs[21] = {0};
  regs[0] = t & 0xFFFF;          // low 16 bits
  regs[1] = (t >> 16) & 0xFFFF;  // high 16 bits
  regs[2] = 0x0003;              // status: time synced

  if (weatherManualMode) {
    for (int i = 0; i < 18; i++) {
      regs[3 + i] = weatherManualRegs[i];
    }
  } else if (weatherCacheFresh()) {
    WeatherNow wxNow;
    WeatherDay wxDays[3];
    weatherGetCached(wxNow, wxDays);
    fillFromWeatherCache(regs, wxNow, wxDays);
  } else {
    fillMockWeather(regs); // fallback until first real fetch
  }

  lockModbus();
  bool ok = powerSupply->writeRegisters(REG_RTC_TIME_LO, 21, regs);
  unlockModbus();
  if (!ok) {
    // Bus timeouts under contention happen; log at most once every 5 min.
    static unsigned long lastErrMs = 0;
    if (millis() - lastErrMs > 5UL * 60UL * 1000UL) {
      lastErrMs = millis();
      LOG_ERROR("RTC/weather block write failed");
    }
  }
}

// Background task: refresh the weather cache from Open-Meteo roughly every
// 15 minutes, without ever blocking the Modbus sync path.
void weatherClientTask(void* param) {
  (void)param;
  // First fetch soon after boot, then 15 min.
  vTaskDelay(2000 / portTICK_PERIOD_MS); // let WiFi finish connecting
  weatherRefreshCache();
  while (true) {
    vTaskDelay(15UL * 60UL * 1000UL / portTICK_PERIOD_MS);
    weatherRefreshCache();
  }
}

void startWeatherClient() {
  static bool started = false;
  if (!started && WiFi.status() == WL_CONNECTED) {
    started = true;
    xTaskCreatePinnedToCore(weatherClientTask, "wxClient", 8192, NULL, 1, NULL, 1);
  }
}