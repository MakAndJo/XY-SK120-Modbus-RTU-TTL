#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>

// SoftAP SSID used for first-boot / provisioning mode
#define PORTAL_AP_SSID "XY-SK150-Setup"

// Local web server: serves the full embedded client (index.html/main.js/
// style.css, gzipped from PROGMEM) plus a WebSocket channel (/ws) for status +
// commands, and a few one-off HTTP config endpoints (/api/wifi, /api/mqtt,
// /api/mode). Runs on ESPAsyncWebServer, single instance serving both STA
// (LAN IP) and AP (192.168.4.1) interfaces.

// Start the AP + captive portal in a background task. The device serves the
// client from the softAP. The portal stops itself once the user saves working
// credentials (or after a safety timeout).
void startCaptivePortal();

// AP mode requested from the block (REG_WIFI_CONFIG=2): like the portal but
// without the boot-time safety timeout, since the user drives the mode.
void startApPortal();

// Stop the portal, shut down the AP and return to station mode.
void stopCaptivePortal();

// True while the portal is running (AP up + HTTP serving).
bool captivePortalActive();

// React to REG_WIFI_CONFIG changes made on the block:
//   2 -> switch to AP mode; 1 -> touch (no-op now, kept for the block);
//   0 -> "no pending command" (register is a one-shot trigger, 0 is ignored).
// Call periodically from loop().
void checkWifiConfigMode();

// Current latched WiFi mode (0=normal, 1=touch, 2=ap).
int localWifiMode();
void resetWifiModeLatch();

// Start the local server in STA mode (serves the client + API on the LAN IP).
void startLocalServer();

// Stop the STA local server. (The async server is shared and keeps running;
// kept for API compatibility.)
void stopLocalServer();

// True while the STA local server is serving.
bool localServerActive();

#endif // CAPTIVE_PORTAL_H