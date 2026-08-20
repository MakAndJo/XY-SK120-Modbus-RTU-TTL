#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

// SoftAP SSID used for first-boot / provisioning mode
#define PORTAL_AP_SSID "XY-SK150-Setup"

// Local web server: serves the full embedded client (index.html/main.js/
// style.css, gzipped from PROGMEM) plus the /api/* endpoints, in both STA mode
// (reachable on the LAN IP) and AP mode (provisioning). The boot-time probe
// fetch('/api/status') in the client decides "local" vs "server" mode.

// Start the AP + captive portal in a background task. The device serves the
// client + a provisioning form for the home network. The portal stops itself
// once the user saves working credentials (or after a safety timeout).
void startCaptivePortal();

// AP mode requested from the block (REG_WIFI_CONFIG=2): like the portal but
// without the boot-time safety timeout, since the user drives the mode.
void startApPortal();

// Stop the portal, shut down the AP and return to station mode.
void stopCaptivePortal();

// True while the portal is running (AP up + HTTP serving).
bool captivePortalActive();

// React to REG_WIFI_CONFIG changes made on the block:
//   2 -> switch to AP mode; 1 -> touch pairing (fresh code + server re-pair);
//   0 -> normal mode. Call periodically from loop().
void checkWifiConfigMode();

// Start the local server in STA mode (serves the client + API on the LAN IP).
void startLocalServer();

// Stop the STA local server.
void stopLocalServer();

// True while the STA local server is serving.
bool localServerActive();

#endif // CAPTIVE_PORTAL_H