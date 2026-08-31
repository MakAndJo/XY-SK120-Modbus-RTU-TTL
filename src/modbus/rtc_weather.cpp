#include "rtc_weather.h"
#include <Arduino.h>
#include <time.h>
#include <Preferences.h>
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

// ---- Clock / weather / screensaver settings (NVS) ---------------------------

#define RTCWX_NS "rtcwx"
#define RTCWX_SSIDLE "ssi"
#define RTCWX_SSSUSP "sss"
#define RTCWX_WXEN "wxen"
#define RTCWX_WXLAT "wxlat"
#define RTCWX_WXLON "wxlon"

void rtcSetScreensaver(uint8_t idleType, uint8_t suspendType) {
  Preferences p;
  p.begin(RTCWX_NS, false);
  p.putUChar(RTCWX_SSIDLE, idleType);
  p.putUChar(RTCWX_SSSUSP, suspendType);
  p.end();
  Serial.printf("[RTC] screensaver idle=%u suspend=%u\n", idleType, suspendType);
}
uint8_t rtcScreensaverIdle() {
  Preferences p; uint8_t v = 2;
  if (p.begin(RTCWX_NS, true)) { v = p.getUChar(RTCWX_SSIDLE, 2); p.end(); }
  return v;
}
uint8_t rtcScreensaverSuspend() {
  Preferences p; uint8_t v = 2;
  if (p.begin(RTCWX_NS, true)) { v = p.getUChar(RTCWX_SSSUSP, 2); p.end(); }
  return v;
}
void rtcSetWeather(bool enabled, double lat, double lon) {
  Preferences p;
  p.begin(RTCWX_NS, false);
  p.putBool(RTCWX_WXEN, enabled);
  if (lat != 0 || lon != 0) {
    p.putDouble(RTCWX_WXLAT, lat);
    p.putDouble(RTCWX_WXLON, lon);
  }
  p.end();
  double la = weatherLat(), lo = weatherLon();
  setWeatherMeteoConfig(la, lo);
  Serial.printf("[RTC] weather %s lat=%.4f lon=%.4f\n", enabled ? "on" : "off", la, lo);
}
bool weatherFetchEnabled() {
  Preferences p; bool v = true;
  if (p.begin(RTCWX_NS, true)) { v = p.getBool(RTCWX_WXEN, true); p.end(); }
  return v;
}
double weatherLat() {
  Preferences p; double v = 57.1522;
  if (p.begin(RTCWX_NS, true)) { v = p.getDouble(RTCWX_WXLAT, 57.1522); p.end(); }
  return v;
}
double weatherLon() {
  Preferences p; double v = 65.5272;
  if (p.begin(RTCWX_NS, true)) { v = p.getDouble(RTCWX_WXLON, 65.5272); p.end(); }
  return v;
}

// ---------------------------------------------------------------------------
// Mock weather values (used only before the first real API fetch, or when the
// user puts weather into "manual" mode). See wmoToPsuIcon() for the icon map.
// ---------------------------------------------------------------------------
void fillMockWeather(uint16_t regs[21]) {
  // Today (0x0203-0x020B): code, high temp, low temp, current temp, humidity, -,
  // wind level (lo), wind level (hi), wind level extra
  regs[3]  = 0x0005;                                        // today code: sun
  regs[4]  = (uint16_t)(int16_t)17;                         // low temperature (°C)  [block shows swapped]
  regs[5]  = (uint16_t)(int16_t)25;                         // high temperature (°C)
  regs[6]  = (uint16_t)(int16_t)26;                         // current temperature (°C), shown as "NNc"
  regs[7]  = 0x0042;                                        // humidity %, shown as "NN%"
  regs[8]  = 0x0000;                                        // reserved (not rendered)
  regs[9]  = 0x0002;                                        // wind level 2
  regs[10] = 0x0000;                                        // wind level high word
  regs[11] = 0x0000;                                        // wind level low word

  // Forecast days (0x020C-0x0214): code, high temp, low temp.
  regs[12] = 0x0002;  regs[13] = (uint16_t)(int16_t)16;  regs[14] = (uint16_t)(int16_t)24;  // day 1
  regs[15] = 0x0002;  regs[16] = (uint16_t)(int16_t)15;  regs[17] = (uint16_t)(int16_t)23;  // day 2
  regs[18] = 0x0002;  regs[19] = (uint16_t)(int16_t)14;  regs[20] = (uint16_t)(int16_t)22;  // day 3
}

void fillFromWeatherCache(uint16_t regs[21], WeatherNow now, WeatherDay days[3]) {
  // Day/night from the local hour.
  struct tm tinfo;
  time_t t = time(nullptr);
  localtime_r(&t, &tinfo);
  bool isNight = (tinfo.tm_hour < 6 || tinfo.tm_hour >= 21);

  // Today: weather code -> icon, high/low from today, current + humidity
  regs[3]  = wmoToPsuIcon(now.wmoCode, isNight);
  regs[4]  = (uint16_t)(int16_t)days[0].tMin;  // low first — block shows these swapped
  regs[5]  = (uint16_t)(int16_t)days[0].tMax;
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
    regs[base + 1] = (uint16_t)(int16_t)days[i].tMin;
    regs[base + 2] = (uint16_t)(int16_t)days[i].tMax;
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

  if (weatherManualMode) {
    for (int i = 0; i < 18; i++) {
      regs[3 + i] = weatherManualRegs[i];
    }
  } else if (weatherFetchEnabled()) {
    if (weatherCacheFresh()) {
      WeatherNow wxNow;
      WeatherDay wxDays[3];
      weatherGetCached(wxNow, wxDays);
      fillFromWeatherCache(regs, wxNow, wxDays);
    } else {
      fillMockWeather(regs); // fallback until first real fetch
    }
  }
  // weather disabled: weather regs stay zeroed

  lockModbus();
  // 0x0202 = screensaver type (0 off, 1 clock, 2+ clock+weather), chosen by the
  // PSU state (0x001E reads 0 when awake, non-zero when suspended).
  uint16_t sysStatus = 0;
  powerSupply->readRegister(REG_SYS_STATUS, sysStatus);
  regs[2] = (sysStatus == 0) ? rtcScreensaverIdle() : rtcScreensaverSuspend();
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
  if (weatherFetchEnabled()) weatherRefreshCache();
  while (true) {
    vTaskDelay(15UL * 60UL * 1000UL / portTICK_PERIOD_MS);
    if (weatherFetchEnabled()) weatherRefreshCache();
  }
}

void startWeatherClient() {
  static bool started = false;
  if (!started && WiFi.status() == WL_CONNECTED) {
    started = true;
    setWeatherMeteoConfig(weatherLat(), weatherLon()); // apply saved coords
    xTaskCreatePinnedToCore(weatherClientTask, "wxClient", 8192, NULL, 1, NULL, 1);
  }
}