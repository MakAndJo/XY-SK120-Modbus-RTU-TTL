#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

// SoftAP SSID used for first-boot / provisioning mode
#define PORTAL_AP_SSID "XY-SK150-Setup"

// Start the AP + captive portal in a background task. The device serves a
// minimal setup page (SSID/password for the home network + deviceId + bind
// key). The portal stops itself once the user saves working credentials.
void startCaptivePortal();

// Stop the portal, shut down the AP and return to station mode.
void stopCaptivePortal();

// True while the portal is running (AP up + HTTP serving).
bool captivePortalActive();

#endif // CAPTIVE_PORTAL_H