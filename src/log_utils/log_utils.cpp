#include "log_utils.h"

// Initialize NTP status flags
bool ntpConfigured = false;
bool ntpSynced = false;
long gmtOffset_sec = 0;
int daylightOffset_sec = 0;
String tzLabel = "UTC";

// We only need this file to define the global variables
// The inline functions are defined in the header
