#include "weather_api.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// Default location: Tyumen, RU (matches the sample request).
static double meteoLat = 57.1522;
static double meteoLon = 65.5272;

// Cached results, refreshed in the background by startWeatherClient().
static WeatherNow cachedNow = {0, 0, 60};
static WeatherDay cachedDays[3] = {{60,-100,-100},{60,-100,-100},{60,-100,-100}};
static unsigned long lastFetchMs = 0;
static bool haveData = false;

void setWeatherMeteoConfig(double lat, double lon) {
  meteoLat = lat;
  meteoLon = lon;
}

// Map Open-Meteo WMO code (https://open-meteo.com/en/docs#weathervariables) to
// the icon code discovered on the XY-SK150 screensaver. Day/night picked from
// the moon/sun icon pairs. Falls back to 60 (N/A / "no data").
//
// Known PSU icons (see XY-SKxxx.h):
//  0 луна, 1 два облака, 2 облако, 3 два обл.+солнце, 4 луна за облаком,
//  5 солнце, 6 солнце за 2 обл., 7 солнце за облаком, 8 обл.4кап.+луна,
//  9 обл.4кап.+солнце, 10 обл.2кап.+гроза, 11 обл.4кап.+гроза, 29 три капли,
//  30-33 обл.+снежинки, 34 обл.3кап.+2снеж., 37 три снежинки, 46 мути,
//  47 ураган, 60 N/A
uint16_t wmoToPsuIcon(int16_t code, bool isNight) {
  switch (code) {
    // Clear / mainly clear
    case 0:  return isNight ? 0 : 5;             // луна / солнце
    case 1:  return isNight ? 4 : 7;             // луна за облаком / солнце за облаком
    // Partly cloudy / overcast
    case 2:  return 3;                           // два облака + солнце
    case 3:  return isNight ? 1 : 2;             // ночью два облака / днём облако
    // Fog / freezing fog
    case 45:
    case 48: return 46;                          // туман -> мути/дымка
    // Drizzle
    case 51:
    case 53:
    case 55: return 29;                          // три капли
    case 56:
    case 57: return 34;                          // ледяная морось -> капли+снежинки
    // Rain
    case 61:
    case 80: return isNight ? 8 : 9;             // слабый дождь: облако 4 капли
    case 63:
    case 81: return isNight ? 8 : 9;             // обычный дождь
    case 65:
    case 82: return isNight ? 8 : 9;             // сильный дождь (нет более сильной иконки)
    // Freezing rain
    case 66:
    case 67: return 34;
    // Snow
    case 71: return 30;                          // облако + 1 снежинка
    case 73: return 31;                          // облако + 3 снежинки
    case 75: return 33;                          // облако + 7 снежинок
    case 77: return 37;                          // снежная крупа -> три снежинки
    case 85: return 32;                          // снежные заряды
    case 86: return 33;
    // Thunderstorm
    case 95: return 10;                          // облако + 2 капли + гроза
    case 96:
    case 99: return 11;                          // + град -> облако + 4 капли + гроза
    default: return 60;                          // N/A
  }
}

// Do an actual HTTP fetch and push results into the cache. Never call inline
// from the 10s sync loop - only from the background weather client task.
bool weatherRefreshCache() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  char url[256];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast"
           "?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&forecast_days=3&timezone=auto",
           meteoLat, meteoLon);

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url)) {
    return false;
  }

  int status = http.GET();
  String body = (status == HTTP_CODE_OK) ? http.getString() : "";
  http.end();
  if (status != HTTP_CODE_OK) {
    return false;
  }

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body) || !doc.containsKey("current") || !doc.containsKey("daily")) {
    return false;
  }

  JsonObject cur = doc["current"];
  cachedNow.wmoCode  = (int16_t)(int)cur["weather_code"];
  cachedNow.tNow     = (int16_t)(float)cur["temperature_2m"];
  cachedNow.humidity = (int16_t)(int)cur["relative_humidity_2m"];

  JsonArray w  = doc["daily"]["weather_code"];
  JsonArray mx = doc["daily"]["temperature_2m_max"];
  JsonArray mn = doc["daily"]["temperature_2m_min"];

  int n = min(3, (int)w.size());
  for (int i = 0; i < n; i++) {
    cachedDays[i].wmoCode = (int16_t)(int)w[i];
    cachedDays[i].tMax    = (int16_t)(float)mx[i];
    cachedDays[i].tMin    = (int16_t)(float)mn[i];
  }
  for (int i = n; i < 3; i++) {
    cachedDays[i] = cachedDays[i]; // keep whatever we had
  }

  lastFetchMs = millis();
  haveData = true;
  return true;
}

bool weatherCacheFresh() {
  if (!haveData) return false;
  // Refresh every 15 minutes.
  return (millis() - lastFetchMs) < 15UL * 60UL * 1000UL;
}

void weatherGetCached(WeatherNow& now, WeatherDay days[3]) {
  now = cachedNow;
  for (int i = 0; i < 3; i++) days[i] = cachedDays[i];
}