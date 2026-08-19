#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <stdint.h>

// Open-Meteo weather data for one day.
struct WeatherDay {
  int16_t wmoCode;
  int16_t tMax;   // °C (int16, matches what the PSU shows)
  int16_t tMin;   // °C
};

// Current weather snapshot.
struct WeatherNow {
  int16_t tNow;   // °C
  int16_t humidity; // %
  int16_t wmoCode;
};

// Fetches the 3-day forecast + current conditions from Open-Meteo over HTTPS.
// Returns true on success. Coordinates/URL are compile-time defaults but can be
// overridden with the setters.
bool fetchOpenMeteo(WeatherNow& now, WeatherDay days[3]);

// Map an Open-Meteo WMO code to the XY-SK150 screensaver icon code.
// isNight picks the moon/sun variants. Returns 60 (N/A) for unknown codes.
uint16_t wmoToPsuIcon(int16_t wmoCode, bool isNight);

// Options for the API request (persisted defaults).
void setWeatherMeteoConfig(double lat, double lon);

// Background cache helpers - weather is fetched once by a client task and read
// from cache everywhere else, so the 10s Modbus sync never blocks on HTTP.
bool weatherRefreshCache();       // do one fetch now (call from background task)
bool weatherCacheFresh();         // true if a fetch succeeded within 15 min
void weatherGetCached(WeatherNow& now, WeatherDay days[3]);

#endif // WEATHER_API_H