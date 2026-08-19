#ifndef WEB_INTERFACE_LOG_UTILS_H
#define WEB_INTERFACE_LOG_UTILS_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

// NTP server details
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"

// Time zone constants
#define TZ_UTC       0
#define TZ_EST      -5 // Eastern Time
#define TZ_CST      -6 // Central Time
#define TZ_MST      -7 // Mountain Time
#define TZ_PST      -8 // Pacific Time
#define TZ_GMT       0 // Greenwich Mean Time
#define TZ_CET       1 // Central European Time
#define TZ_EET       2 // Eastern European Time
#define TZ_MSK       3 // Moscow Time
#define TZ_SAMT      4 // Samara Time
#define TZ_YEKT      5 // Yekaterinburg Time
#define TZ_OMST      6 // Omsk Time
#define TZ_KRAT      7 // Krasnoyarsk Time
#define TZ_IRKT      8 // Irkutsk Time
#define TZ_JST       9 // Japan Standard Time
#define TZ_AEST     10 // Australian Eastern Standard Time
#define TZ_MAGT     11 // Magadan Time
#define TZ_PETT     12 // Kamchatka Time

// Flags to track NTP configuration
extern bool ntpConfigured;
extern bool ntpSynced;
extern long gmtOffset_sec;
extern int daylightOffset_sec;
extern String tzLabel;

/**
 * Returns a formatted timestamp string for logging
 * Format: [HH:MM:SS.mmm] or [YYYY-MM-DD HH:MM:SS] if NTP synced
 * 
 * Using 'inline' to prevent multiple definition errors when included in multiple files
 * IMPORTANT: This function must be defined before any other functions that use it!
 */
inline String getLogTimestamp() {
    char buffer[32];
    
    if (ntpConfigured && !ntpSynced) {
        // Check if NTP time is available
        time_t now;
        struct tm timeinfo;
        if (time(&now) && getLocalTime(&timeinfo)) {
            ntpSynced = true;
            
            Serial.print("[INFO] NTP time synchronized. Current time: ");
            Serial.print(timeinfo.tm_year + 1900);
            Serial.print("-");
            Serial.print(timeinfo.tm_mon + 1);
            Serial.print("-");
            Serial.print(timeinfo.tm_mday);
            Serial.print(" ");
            Serial.print(timeinfo.tm_hour);
            Serial.print(":");
            Serial.print(timeinfo.tm_min);
            Serial.print(":");
            Serial.println(timeinfo.tm_sec);
        }
    }
    
    if (ntpSynced) {
        // Use real-time clock if NTP is synced
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // Format as [YYYY-MM-DD HH:MM:SS]
        sprintf(buffer, "[%04d-%02d-%02d %02d:%02d:%02d] ", 
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        // Fallback to millis-based time if NTP not synced
        unsigned long ms = millis();
        unsigned long seconds = ms / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        
        // Format as [HH:MM:SS.mmm]
        sprintf(buffer, "[%02lu:%02lu:%02lu.%03lu] ",
                hours % 24, minutes % 60, seconds % 60, ms % 1000);
    }
    
    return String(buffer);
}

// Standard logging macros - defined early so they can be used throughout the file
#define LOG_INFO(msg) Serial.print(getLogTimestamp()); Serial.println(msg)
#define LOG_ERROR(msg) Serial.print(getLogTimestamp()); Serial.print("ERROR: "); Serial.println(msg)
#define LOG_DEBUG(msg) Serial.print(getLogTimestamp()); Serial.print("DEBUG: "); Serial.println(msg)
#define LOG_WARNING(msg) Serial.print(getLogTimestamp()); Serial.print("WARNING: "); Serial.println(msg)

// WebSocket-specific logging with source and destination IPs
#define LOG_WS(srcIP, dstIP, msg) Serial.println(formatWebSocketLog(srcIP, dstIP, msg))

// Time zone structure for configuration
struct TimeZoneConfig {
    long gmtOffset;       // Offset in seconds from GMT
    int daylightOffset;   // Daylight saving time offset in seconds
    const char* label;    // Human-readable label
};

// Common time zones - can be expanded as needed
const TimeZoneConfig TIME_ZONES[] = {
    {TZ_UTC * 3600, 0, "UTC"},
    {TZ_GMT * 3600, 0, "GMT"},
    {TZ_EST * 3600, 3600, "EST (UTC-5)"},
    {TZ_CST * 3600, 3600, "CST (UTC-6)"},
    {TZ_MST * 3600, 3600, "MST (UTC-7)"},
    {TZ_PST * 3600, 3600, "PST (UTC-8)"},
    {TZ_CET * 3600, 3600, "CET (UTC+1)"},
    {TZ_EET * 3600, 3600, "EET (UTC+2)"},
    {TZ_MSK * 3600, 0, "Moscow (UTC+3)"},
    {TZ_SAMT * 3600, 0, "Samara (UTC+4)"},
    {TZ_YEKT * 3600, 0, "Yekaterinburg (UTC+5)"},
    {TZ_OMST * 3600, 0, "Omsk (UTC+6)"},
    {TZ_KRAT * 3600, 0, "Krasnoyarsk (UTC+7)"},
    {TZ_IRKT * 3600, 0, "Irkutsk (UTC+8)"},
    {TZ_JST * 3600, 0, "JST (UTC+9)"},
    {TZ_AEST * 3600, 3600, "AEST (UTC+10)"},
    {TZ_MAGT * 3600, 0, "Magadan (UTC+11)"},
    {TZ_PETT * 3600, 0, "Kamchatka (UTC+12)"}
};

/**
 * Format a WebSocket message log with source and destination IP addresses
 * Format: [timestamp] (src_ip) > (dst_ip) message
 */
inline String formatWebSocketLog(const IPAddress& srcIP, const IPAddress& dstIP, const String& message) {
    String timestamp = getLogTimestamp();
    String srcIPStr = srcIP.toString();
    String dstIPStr = dstIP.toString();
    
    return timestamp + "(" + srcIPStr + ") > (" + dstIPStr + ") " + message;
}

/**
 * Load time zone settings from persistent storage
 */
inline void loadTimeZoneSettings() {
    Preferences prefs;
    prefs.begin("timezone", true); // Read-only mode
    
    // Default to UTC if not set
    gmtOffset_sec = prefs.getLong("gmtOffset", 0);
    daylightOffset_sec = prefs.getInt("dstOffset", 0);
    tzLabel = prefs.getString("tzLabel", "UTC");
    
    prefs.end();
}

/**
 * Save time zone settings to persistent storage
 */
inline void saveTimeZoneSettings(long gmt_offset, int dst_offset, const String& label) {
    Preferences prefs;
    prefs.begin("timezone", false); // Read-write mode
    
    prefs.putLong("gmtOffset", gmt_offset);
    prefs.putInt("dstOffset", dst_offset);
    prefs.putString("tzLabel", label);
    
    prefs.end();
    
    // Update global variables
    gmtOffset_sec = gmt_offset;
    daylightOffset_sec = dst_offset;
    tzLabel = label;
}

/**
 * Set time zone by index from the TIME_ZONES array
 * @return True if successful
 */
inline bool setTimeZoneByIndex(int index) {
    int numTimeZones = sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);
    
    if (index >= 0 && index < numTimeZones) {
        saveTimeZoneSettings(
            TIME_ZONES[index].gmtOffset,
            TIME_ZONES[index].daylightOffset,
            TIME_ZONES[index].label
        );
        
        // Reconfigure time with new settings
        if (ntpConfigured) {
            configTime(gmtOffset_sec, daylightOffset_sec, NTP_SERVER1, NTP_SERVER2);
            // Reset ntpSynced so it will check again
            ntpSynced = false;
        }
        
        return true;
    }
    
    return false;
}

/**
 * Configure NTP time synchronization with the saved timezone settings
 * Should be called after WiFi is connected
 */
inline bool configureNTP() {
    // Load time zone settings first
    loadTimeZoneSettings();
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Cannot configure NTP - WiFi not connected");
        return false;
    }
    
    // Configure NTP
    configTime(gmtOffset_sec, daylightOffset_sec, NTP_SERVER1, NTP_SERVER2);
    
    Serial.print(getLogTimestamp());
    Serial.print("NTP client configured with timezone: ");
    Serial.print(tzLabel);
    Serial.print(" (GMT");
    if (gmtOffset_sec >= 0) Serial.print("+");
    Serial.print(gmtOffset_sec / 3600);
    Serial.println(")");
    
    ntpConfigured = true;
    ntpSynced = false; // Will be set to true when first sync completes
    
    return true;
}

/**
 * Get a list of available time zones
 * @return JSON-formatted string with time zone options
 */
inline String getAvailableTimeZones() {
    int numTimeZones = sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);
    String result = "[";
    
    for (int i = 0; i < numTimeZones; i++) {
        if (i > 0) result += ",";
        result += "{\"index\":" + String(i) + 
                  ",\"label\":\"" + TIME_ZONES[i].label + 
                  "\",\"offset\":" + String(TIME_ZONES[i].gmtOffset / 3600) + "}";
    }
    
    result += "]";
    return result;
}

/**
 * Get the current time zone settings
 * @return JSON-formatted string with current time zone
 */
inline String getCurrentTimeZone() {
    // Find the current time zone in the array
    int currentIndex = -1;
    int numTimeZones = sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);
    
    for (int i = 0; i < numTimeZones; i++) {
        if (TIME_ZONES[i].gmtOffset == gmtOffset_sec && 
            TIME_ZONES[i].daylightOffset == daylightOffset_sec) {
            currentIndex = i;
            break;
        }
    }
    
    return "{\"index\":" + String(currentIndex) + 
           ",\"label\":\"" + tzLabel + 
           "\",\"offset\":" + String(gmtOffset_sec / 3600) + 
           ",\"synced\":" + String(ntpSynced ? "true" : "false") + "}";
}

#endif // WEB_INTERFACE_LOG_UTILS_H
