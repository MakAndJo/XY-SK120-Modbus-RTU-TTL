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

// Stop the portal, shut down the AP and return to station mode.
void stopCaptivePortal();

// True while the portal is running (AP up + HTTP serving).
bool captivePortalActive();

// Start the local server in STA mode (serves the client + API on the LAN IP).
void startLocalServer();

// Stop the STA local server.
void stopLocalServer();

// True while the STA local server is serving.
bool localServerActive();

#endif // CAPTIVE_PORTAL_H