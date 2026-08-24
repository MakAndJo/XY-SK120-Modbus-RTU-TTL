#include "captive_portal.h"
#include "wifi_native.h"
#include "wifi_settings.h"
#include "modbus/psu_service.h"
#include "mqtt/mqtt_manager.h"
#include "webui/embedded_client.h"
#include <WiFi.h>
#include <ArduinoJson.h>

extern XY_SKxxx* powerSupply; // defined in main.cpp

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

static void handleApiPair() {
  if (webServer.method() == HTTP_GET) {
    DynamicJsonDocument out(128);
    out["pairing"] = mqttPairingActive();
    out["bound"] = mqttGetBound();
    out["pairCode"] = mqttGetPairCode();
    String json;
    serializeJson(out, json);
    webServer.send(200, "application/json", json);
    return;
  }
  DynamicJsonDocument doc(128);
  deserializeJson(doc, webServer.arg("plain"));
  bool active = doc["active"] | true;
  if (active) {
    mqttRequestRepair();
    mqttStart();
  } else {
    resetWifiModeLatch();
    mqttCancelRepair();
    mqttStop();
  }
  DynamicJsonDocument r(64);
  r["success"] = true;
  r["pairing"] = mqttPairingActive();
  String json;
  serializeJson(r, json);
  webServer.send(200, "application/json", json);
}

static void handleApiMode() {
  if (webServer.method() == HTTP_GET) {
    DynamicJsonDocument out(32);
    out["mode"] = localWifiMode();
    String json;
    serializeJson(out, json);
    webServer.send(200, "application/json", json);
    return;
  }
  DynamicJsonDocument doc(64);
  deserializeJson(doc, webServer.arg("plain"));
  int want = doc["mode"] | 0;
  if (want == 0) {
    // Exit AP / pairing from the panel: back to station + normal mode. Also
    // write REG_WIFI_CONFIG back to None so the block leaves the mode even if
    // it kept the register latched at 2 after our status acknowledgment.
    resetWifiModeLatch();
    if (powerSupply) {
      lockModbus();
      powerSupply->writeRegister(REG_WIFI_CONFIG, 0);
      unlockModbus();
    }
    if (captivePortalActive()) {
      stopCaptivePortal();
      WiFi.mode(WIFI_STA);
      connectToSavedNetworks();
    }
    mqttCancelRepair();
    mqttStop();
    DynamicJsonDocument r(32);
    r["success"] = true;
    String json;
    serializeJson(r, json);
    webServer.send(200, "application/json", json);
    return;
  }
  webServer.send(400, "application/json", "{\"error\":\"unsupported mode\"}");
}

static void handleNotFound() {
  // Captive portal in both modes: any hostname resolves to us and redirects to
  // the panel (catch-all *.local, connectivity probes, etc.).
  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/html", "");
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
  webServer.on("/api/pair", handleApiPair);
  webServer.on("/api/mode", handleApiMode);
  webServer.onNotFound(handleNotFound);
}

static void webTask(void* param) {
  (void)param;
  webTaskActive = true;
  registerRoutes();
  webServer.begin();
  // Captive-portal DNS in both modes: in AP it points at the softAP, in STA at
  // our LAN IP, so any hostname / connectivity probe lands on the panel.
  IPAddress dnsTarget = serveModeAp ? IPAddress(192, 168, 4, 1) : WiFi.localIP();
  dnsServer.start(DNS_PORT, "*", dnsTarget);

  unsigned long startMs = millis();
  while (portalRunning || staRunning) {
    dnsServer.processNextRequest();
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

  xTaskCreate(webTask, "web", 8192, NULL, 1, NULL);
}

void stopCaptivePortal() {
  portalRunning = false;
}

bool captivePortalActive() {
  return portalRunning;
}

// ---- REG_WIFI_CONFIG mode watcher -------------------------------------------

// The block's REG_WIFI_CONFIG is a ONE-SHOT trigger: it holds 2/1 just long
// enough for the module to notice, then the block clears it back to 0 once the
// module has entered the mode. So 0 must NOT be read as "go to None" — it means
// "no pending command". We latch the active mode ourselves; 0 is ignored.
// Exits: reboot (RAM latch resets), or the local panel ("/api/mode" button).

static int activeWifiMode = 0; // 0=normal, 1=touch/pairing, 2=ap
int localWifiMode() { return activeWifiMode; }
void resetWifiModeLatch() {
  activeWifiMode = 0;
  setWifiConfigState(0);
}

// Debounce helpers: only act on a register value seen twice in a row.
static int lastModeCfg = -1;
static int pendingMode = -1;
static int pendingCount = 0;

void checkWifiConfigMode() {
  int cfg = getWifiConfigState();

  if (cfg != lastModeCfg) {
    if (cfg != pendingMode) {
      pendingMode = cfg;
      pendingCount = 1;
      return;
    }
    pendingCount++;
    if (pendingCount < 2) return;
    lastModeCfg = cfg;
    pendingMode = -1;
    pendingCount = 0;
    Serial.printf("[WEB] REG_WIFI_CONFIG -> %d\n", cfg);
  } else {
    pendingMode = -1;
    pendingCount = 0;
  }

  if (cfg == 2 && activeWifiMode != 2) {
    // AP mode requested from the block: local AP panel only, MQTT never runs.
    activeWifiMode = 2;
    stopLocalServer();
    mqttCancelRepair();
    mqttStop();
    startApPortal();
  } else if (cfg == 1 && activeWifiMode != 1) {
    // Touch pairing: leave AP if active, then break the old server binding and
    // request a fresh pair code (shown on the PSU screen immediately).
    activeWifiMode = 1;
    if (captivePortalActive()) {
      stopCaptivePortal();
      WiFi.mode(WIFI_STA);
      connectToSavedNetworks();
    }
    mqttRequestRepair();
    mqttStart();
  }
  // cfg == 0: "no pending command" — keep whatever mode we latched.
}

void startLocalServer() {
  if (portalRunning || staRunning) return;
  unsigned long waitMs = millis();
  while (webTaskActive && millis() - waitMs < 2000) vTaskDelay(20 / portTICK_PERIOD_MS);
  if (webTaskActive) {
    Serial.println("[WEB] Old server task did not exit, aborting STA start");
    return;
  }
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