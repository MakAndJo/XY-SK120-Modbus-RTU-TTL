#include "captive_portal.h"
#include "wifi_native.h"
#include "wifi_settings.h"
#include "modbus/psu_service.h"
#include "mqtt/mqtt_manager.h"
#include "webui/embedded_client.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

extern XY_SKxxx* powerSupply; // defined in main.cpp

static AsyncWebServer asyncServer(80);
static AsyncWebSocket ws("/ws");
static DNSServer dnsServer;
static const byte DNS_PORT = 53;

static volatile bool portalRunning = false;  // AP provisioning mode
static volatile bool serveModeAp = false;
static bool portalTimeout = true;            // boot-time 10min safety timeout
static bool serverStarted = false;           // async server + task running
static bool dnsRunning = false;

// ---- Embedded client (gzipped PROGMEM) -------------------------------------

static void serveEmbedded(AsyncWebServerRequest* request, const char* path) {
  for (size_t i = 0; i < embeddedFilesCount; i++) {
    if (strcmp(embeddedFiles[i].path, path) == 0) {
      AsyncWebServerResponse* r = request->beginResponse_P(
          200, embeddedFiles[i].mime, embeddedFiles[i].data,
          embeddedFiles[i].len);
      r->addHeader("Content-Encoding", "gzip");
      r->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
      request->send(r);
      return;
    }
  }
  request->send(404, "text/plain", "Not found");
}

// ---- WebSocket: status pushes + command channel ----------------------------

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(buildLocalStatusJSON()); // fresh state right away
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT && !info->index && info->final && info->len == len) {
      String payload;
      payload.reserve(len + 1);
      for (size_t i = 0; i < len; i++) payload += (char)data[i];
      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, payload)) return;
      String action = doc["action"] | "";
      if (action.length() == 0) return;
      String response = handleMqttAction(action, payload.c_str());
      if (response.length() > 0) client->text(response);
    }
  }
}

// ---- HTTP config endpoints -------------------------------------------------

// AsyncWebServer delivers the raw POST body through the onBody callback. We
// accumulate it into request->_tempObject, then read it in the request handler.
static void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                        size_t index, size_t total) {
  (void)index; (void)total;
  if (!request->_tempObject) request->_tempObject = new String();
  ((String*)request->_tempObject)->concat((char*)data, len);
}

static String takeBody(AsyncWebServerRequest* request) {
  if (request->_tempObject) {
    String* s = (String*)request->_tempObject;
    String out = *s;
    delete s;
    request->_tempObject = nullptr;
    return out;
  }
  return request->arg("plain");
}

static void handleApiWifiPost(AsyncWebServerRequest* request) {
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, takeBody(request))) {
    request->send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  String ssid = doc["ssid"] | "";
  String pass = doc["password"] | "";
  int priority = doc["priority"] | -1;
  ssid.trim();
  if (ssid.length() == 0) {
    request->send(400, "application/json", "{\"error\":\"missing ssid\"}");
    return;
  }
  bool ok = saveWiFiCredentialsToNVS(ssid, pass, priority);
  if (ok) connectToSavedNetworks();
  request->send(ok ? 200 : 500, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
}

static void handleApiMqttPost(AsyncWebServerRequest* request) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, takeBody(request))) {
    request->send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  String host = doc["host"] | "";
  host.trim();
  if (host.length() == 0) {
    request->send(400, "application/json", "{\"error\":\"missing host\"}");
    return;
  }
  uint16_t port = (uint16_t)(doc["port"] | 1883);
  String user = doc["user"] | "";
  String pass = doc["pass"] | "";
  bool enable = doc["enable"] | false;
  mqttSaveConfig(host, port, user, pass);
  mqttSetEnabled(enable);
  request->send(200, "application/json", "{\"success\":true}");
}

static void handleApiModePost(AsyncWebServerRequest* request) {
  DynamicJsonDocument doc(64);
  deserializeJson(doc, takeBody(request));
  int want = doc["mode"] | 0;
  if (want != 0) {
    request->send(400, "application/json", "{\"error\":\"unsupported mode\"}");
    return;
  }
  // Exit AP from the panel: back to station + normal mode. Also write
  // REG_WIFI_CONFIG back to None so the block leaves the mode.
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
  mqttStop();
  request->send(200, "application/json", "{\"success\":true}");
}

static void registerRoutes() {
  asyncServer.on("/", HTTP_GET, [](AsyncWebServerRequest* r) { serveEmbedded(r, "/index.html"); });
  asyncServer.on("/index.html", HTTP_GET, [](AsyncWebServerRequest* r) { serveEmbedded(r, "/index.html"); });
  asyncServer.on("/main.js", HTTP_GET, [](AsyncWebServerRequest* r) { serveEmbedded(r, "/main.js"); });
  asyncServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* r) { serveEmbedded(r, "/style.css"); });

  asyncServer.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", getWifiStatus());
  });
  asyncServer.on("/api/wifi", HTTP_POST, handleApiWifiPost, NULL, collectBody);
  asyncServer.on("/api/mqtt", HTTP_POST, handleApiMqttPost, NULL, collectBody);

  asyncServer.on("/api/mode", HTTP_GET, [](AsyncWebServerRequest* r) {
    DynamicJsonDocument out(32);
    out["mode"] = localWifiMode();
    String json;
    serializeJson(out, json);
    r->send(200, "application/json", json);
  });
  asyncServer.on("/api/mode", HTTP_POST, handleApiModePost, NULL, collectBody);

  // Captive portal: any hostname resolves to us and redirects to the panel.
  asyncServer.onNotFound([](AsyncWebServerRequest* r) { r->redirect("/"); });

  ws.onEvent(onWsEvent);
  asyncServer.addHandler(&ws);
}

// ---- Background task: DNS + status broadcast --------------------------------

static void webTask(void* param) {
  (void)param;
  unsigned long startMs = millis();
  unsigned long lastStatusMs = 0;
  while (serverStarted) {
    if (dnsRunning) dnsServer.processNextRequest();
    if (millis() - lastStatusMs > 1000) {
      lastStatusMs = millis();
      // Only read/build status when someone is actually connected, so the
      // Modbus bus isn't loaded by an empty panel.
      if (ws.count() > 0) {
        ws.textAll(buildLocalStatusJSON());
        ws.cleanupClients();
      }
    }
    // Boot-time safety timeout so the device never sits in AP forever.
    // cfg-driven AP (user asked for it) has no timeout.
    if (serveModeAp && portalRunning && portalTimeout && millis() - startMs > 10UL * 60UL * 1000UL) {
      Serial.println("[WEB] Provisioning timeout, shutting down");
      stopCaptivePortal();
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}

static void ensureWebServer() {
  if (serverStarted) return;
  registerRoutes();
  asyncServer.begin();
  serverStarted = true;
  xTaskCreate(webTask, "web", 8192, NULL, 1, NULL);
  Serial.println("[WEB] Async server started");
}

// ---- AP / portal ------------------------------------------------------------

void startCaptivePortal() {
  if (portalRunning) return;
  portalTimeout = true;
  startApPortal();
}

void startApPortal() {
  if (portalRunning) return;
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
  Serial.printf("[WEB] deviceId=%s\n", mqttDeviceId().c_str());

  dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
  dnsRunning = true;
  ensureWebServer();
}

void stopCaptivePortal() {
  portalRunning = false;
  dnsRunning = false;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
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

static int activeWifiMode = 0; // 0=normal, 1=touch, 2=ap
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
    portalTimeout = false; // user-driven, no safety timeout
    mqttStop();
    startApPortal();
  } else if (cfg == 1 && activeWifiMode != 1) {
    // Touch: kept for the block; without a server it has no pairing action.
    // MQTT follows the enable flag (loop() handles start/stop).
    activeWifiMode = 1;
    if (captivePortalActive()) {
      stopCaptivePortal();
      WiFi.mode(WIFI_STA);
      connectToSavedNetworks();
    }
  }
  // cfg == 0: "no pending command" — keep whatever mode we latched.
}

// ---- STA local server -------------------------------------------------------

void startLocalServer() {
  if (captivePortalActive()) return; // AP is already serving
  serveModeAp = false;
  bool wasStarted = serverStarted;
  ensureWebServer();
  if (!wasStarted) Serial.printf("[WEB] Local server on %s\n", WiFi.localIP().toString().c_str());
}

void stopLocalServer() {
  // The async server is shared and keeps running; nothing to stop.
}

bool localServerActive() {
  return serverStarted;
}