#include "captive_portal.h"
#include "wifi_native.h"
#include "wifi_settings.h"
#include "modbus/psu_service.h"
#include "mqtt/mqtt_manager.h"
#include "webui/embedded_client.h"
#include <WiFi.h>
#include <ArduinoJson.h>

static WebServer webServer(80);
static DNSServer dnsServer;
static const byte DNS_PORT = 53;

static volatile bool portalRunning = false;  // AP provisioning mode
static volatile bool staRunning = false;     // STA local server mode
static volatile bool serveModeAp = false;
static bool portalTimeout = true;            // boot-time 10min safety timeout
static volatile bool webTaskActive = false;  // web task currently running

// ---- Embedded client (gzipped PROGMEM) -------------------------------------

static void serveEmbedded(const char* path) {
  for (size_t i = 0; i < embeddedFilesCount; i++) {
    if (strcmp(embeddedFiles[i].path, path) == 0) {
      webServer.sendHeader("Content-Encoding", "gzip");
      webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
      webServer.send_P(200, embeddedFiles[i].mime, (const char*)embeddedFiles[i].data,
                       embeddedFiles[i].len);
      return;
    }
  }
  webServer.send(404, "text/plain", "Not found");
}

static void handleRoot() {
  serveEmbedded("/index.html");
}

// ---- API -------------------------------------------------------------------

static void handleApiStatus() {
  webServer.send(200, "application/json", buildLocalStatusJSON());
}

static void handleApiInfo() {
  DynamicJsonDocument doc(512);
  doc["deviceId"] = mqttDeviceId();
  doc["name"] = mqttDeviceName();
  doc["model"] = "XY-SK150S";
  doc["bound"] = mqttGetBound();
  doc["pairCode"] = mqttGetPairCode();
  doc["mqttHost"] = mqttHost();
  doc["mqttPort"] = mqttPort();
  doc["mqttConnected"] = mqttConnected();
  doc["ssid"] = isWiFiConnected() ? getWiFiSSID() : "";
  doc["ip"] = getWiFiIP();
  doc["rssi"] = getWiFiRSSI();
  String json;
  serializeJson(doc, json);
  webServer.send(200, "application/json", json);
}

static void handleApiCmd() {
  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  String payload = webServer.arg("plain");
  if (payload.length() == 0) {
    webServer.send(400, "application/json", "{\"error\":\"empty body\"}");
    return;
  }
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    webServer.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  String action = doc["action"] | "";
  if (action.length() == 0) {
    webServer.send(400, "application/json", "{\"error\":\"missing action\"}");
    return;
  }
  String response = handleMqttAction(action, payload.c_str());
  if (response.length() == 0) {
    webServer.send(400, "application/json", "{\"error\":\"unknown action\"}");
    return;
  }
  webServer.send(200, "application/json", response);
}

static void handleApiWifi() {
  if (webServer.method() == HTTP_GET) {
    webServer.send(200, "application/json", getWifiStatus());
    return;
  }
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, webServer.arg("plain"));
  if (error) {
    webServer.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  String ssid = doc["ssid"] | "";
  String pass = doc["password"] | "";
  int priority = doc["priority"] | -1;
  ssid.trim();
  if (ssid.length() == 0) {
    webServer.send(400, "application/json", "{\"error\":\"missing ssid\"}");
    return;
  }
  bool ok = saveWiFiCredentialsToNVS(ssid, pass, priority);
  if (ok) connectToSavedNetworks();
  String json = ok ? "{\"success\":true}" : "{\"success\":false}";
  webServer.send(ok ? 200 : 500, "application/json", json);
}

static void handleApiMqtt() {
  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, webServer.arg("plain"));
  if (error) {
    webServer.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  String host = doc["host"] | "";
  host.trim();
  if (host.length() == 0) {
    webServer.send(400, "application/json", "{\"error\":\"missing host\"}");
    return;
  }
  uint16_t port = (uint16_t)(doc["port"] | 1883);
  mqttSaveConfig(host, port);
  webServer.send(200, "application/json", "{\"success\":true}");
}

// ---- Provisioning (AP mode only) -------------------------------------------

static void handleSave() {
  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, webServer.arg("plain"));
  if (error) {
    webServer.send(400, "text/plain", "Bad JSON");
    return;
  }
  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";
  ssid.trim();
  if (ssid.length() == 0) {
    webServer.send(400, "text/plain", "Missing SSID");
    return;
  }
  if (!saveWiFiCredentialsToNVS(ssid, pass, 1)) {
    webServer.send(500, "text/plain", "Save failed");
    return;
  }
  webServer.send(200, "text/plain", "OK");
  Serial.printf("[WEB] Saved credentials for '%s', switching to STA...\n", ssid.c_str());

  // Give the response time to flush, then switch to station mode.
  delay(300);
  stopCaptivePortal();
  WiFi.mode(WIFI_STA);
  connectToSavedNetworks();
}

static void handleNotFound() {
  if (serveModeAp) {
    // Any hostname resolves to us via the captive portal DNS: redirect to /.
    webServer.sendHeader("Location", "/", true);
    webServer.send(302, "text/html", "");
    return;
  }
  webServer.send(404, "text/plain", "Not found");
}

// ---- Server lifecycle ------------------------------------------------------

static void registerRoutes() {
  webServer.on("/", handleRoot);
  webServer.on("/index.html", [] { serveEmbedded("/index.html"); });
  webServer.on("/main.js", [] { serveEmbedded("/main.js"); });
  webServer.on("/style.css", [] { serveEmbedded("/style.css"); });
  webServer.on("/api/status", handleApiStatus);
  webServer.on("/api/info", handleApiInfo);
  webServer.on("/api/cmd", handleApiCmd);
  webServer.on("/api/wifi", handleApiWifi);
  webServer.on("/api/mqtt", handleApiMqtt);
  webServer.on("/save", handleSave);
  webServer.onNotFound(handleNotFound);
}

static void webTask(void* param) {
  (void)param;
  webTaskActive = true;
  registerRoutes();
  webServer.begin();

  unsigned long startMs = millis();
  while (portalRunning || staRunning) {
    if (serveModeAp) dnsServer.processNextRequest();
    webServer.handleClient();
    // Boot-time safety timeout so the device never sits in AP forever.
    // cfg-driven AP (user asked for it) has no timeout.
    if (serveModeAp && portalTimeout && millis() - startMs > 10UL * 60UL * 1000UL) {
      Serial.println("[WEB] Provisioning timeout, shutting down");
      break;
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }

  webServer.stop();
  dnsServer.stop();
  if (serveModeAp) {
    WiFi.softAPdisconnect(true);
  }
  portalRunning = false;
  staRunning = false;
  Serial.println("[WEB] Server stopped");
  webTaskActive = false;
  vTaskDelete(NULL);
}

void startCaptivePortal() {
  if (portalRunning || staRunning) return;
  portalTimeout = true;
  startApPortal();
}

void startApPortal() {
  if (portalRunning || staRunning) return;
  // Make sure a previous web task (STA local server) fully exited before we
  // touch the shared WebServer instance again.
  unsigned long waitMs = millis();
  while (webTaskActive && millis() - waitMs < 2000) vTaskDelay(20 / portTICK_PERIOD_MS);
  if (webTaskActive) {
    Serial.println("[WEB] Old server task did not exit, aborting AP start");
    return;
  }
  serveModeAp = true;
  portalRunning = true;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(PORTAL_AP_SSID)) {
    Serial.println("[WEB] Failed to start softAP");
    portalRunning = false;
    return;
  }
  Serial.printf("[WEB] AP '%s' up, IP %s\n", PORTAL_AP_SSID, WiFi.softAPIP().toString().c_str());
  Serial.printf("[WEB] deviceId=%s pairCode=%s\n", mqttDeviceId().c_str(), mqttGetPairCode().c_str());

  dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
  xTaskCreate(webTask, "web", 8192, NULL, 1, NULL);
}

void stopCaptivePortal() {
  portalRunning = false;
}

bool captivePortalActive() {
  return portalRunning;
}

// ---- REG_WIFI_CONFIG mode watcher -------------------------------------------

// React to the block selecting a WiFi config mode (0=None, 1=Touch, 2=AP).
// Called from loop(); transitions only fire when the register actually changes.
static int lastModeCfg = -1;

void checkWifiConfigMode() {
  int cfg = getWifiConfigState();
  if (cfg == lastModeCfg) return;
  lastModeCfg = cfg;
  Serial.printf("[WEB] REG_WIFI_CONFIG -> %d\n", cfg);

  if (cfg == 2) {
    // AP mode requested from the block: become an access point.
    stopLocalServer();
    mqttCancelRepair();
    startApPortal();
  } else if (cfg == 1) {
    // Touch pairing: leave AP if active, then break the old server binding and
    // request a fresh pair code (shown on the PSU screen immediately).
    if (captivePortalActive()) {
      stopCaptivePortal();
      WiFi.mode(WIFI_STA);
      connectToSavedNetworks();
    }
    mqttRequestRepair();
  } else {
    // None: normal operation — back to station + server mode.
    if (captivePortalActive()) {
      stopCaptivePortal();
      WiFi.mode(WIFI_STA);
      connectToSavedNetworks();
    }
    mqttCancelRepair();
    mqttStart();
  }
}

void startLocalServer() {
  if (portalRunning || staRunning) return;
  serveModeAp = false;
  staRunning = true;
  Serial.printf("[WEB] Local server on %s\n", WiFi.localIP().toString().c_str());
  xTaskCreate(webTask, "web", 8192, NULL, 1, NULL);
}

void stopLocalServer() {
  staRunning = false;
}

bool localServerActive() {
  return staRunning;
}