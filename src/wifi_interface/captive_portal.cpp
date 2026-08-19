#include "captive_portal.h"
#include "wifi_native.h"
#include "wifi_settings.h"
#include "modbus/psu_service.h"
#include <WiFi.h>
#include <mbedtls/md5.h>

static WebServer portalServer(80);
static DNSServer dnsServer;
static const byte DNS_PORT = 53;
static volatile bool portalRunning = false;

// Device identity: dev_ + last 6 hex chars of the MAC, e.g. "dev_A1B2C3"
static String deviceId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return "dev_" + mac.substring(mac.length() - 6);
}

// Bind key shown on first boot: md5(deviceId). Server looks the device up by
// this key and binds it to the user who enters it.
static String bindKey() {
  String id = deviceId();
  uint8_t digest[16];
  char hex[33];
  mbedtls_md5((const uint8_t*)id.c_str(), id.length(), digest);
  for (int i = 0; i < 16; i++) {
    sprintf(hex + i * 2, "%02x", digest[i]);
  }
  hex[32] = 0;
  return String(hex);
}

// Tiny one-page provisioning UI. Rendered from PROGMEM so no filesystem is
// needed. Shows deviceId + bind key + a form for the home WiFi.
static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>XY-SK150 Setup</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:420px;margin:0 auto;padding:20px}
h1{font-size:20px}code{background:#222;padding:2px 6px;border-radius:4px}
input{width:100%;box-sizing:border-box;padding:10px;margin:6px 0;border-radius:6px;border:1px solid #444;background:#1b1b1b;color:#eee;font-size:15px}
button{width:100%;padding:12px;background:#2f6fed;color:#fff;border:0;border-radius:6px;font-size:16px;margin-top:8px}
.card{background:#191919;border:1px solid #2a2a2a;border-radius:8px;padding:14px;margin:12px 0}
.ok{color:#4caf50;font-weight:bold}.err{color:#f44336;font-weight:bold}
</style>
</head>
<body>
<h1>XY-SK150 provisioning</h1>
<div class="card">
  <div>Device ID: <code id="devId"></code></div>
  <div style="margin-top:8px">Bind key: <code id="bindKey"></code></div>
  <div style="color:#aaa;font-size:13px;margin-top:8px">Enter this key in the XY-SK150 client to bind the device to your account.</div>
</div>
<div class="card">
  <h3 style="margin:0 0 8px">Connect to your WiFi</h3>
  <input id="ssid" placeholder="SSID">
  <input id="pass" type="password" placeholder="Password">
  <button onclick="save()">Save and connect</button>
  <div id="msg"></div>
</div>
<script>
const d='__DEVICE_ID__', k='__BIND_KEY__';
document.getElementById('devId').textContent=d;
document.getElementById('bindKey').textContent=k;
async function save(){
  const ssid=document.getElementById('ssid').value.trim();
  const pass=document.getElementById('pass').value;
  if(!ssid){document.getElementById('msg').className='err';document.getElementById('msg').textContent='Enter SSID';return;}
  const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid,pass})});
  const m=document.getElementById('msg');
  if(r.ok){m.className='ok';m.textContent='Saved. The device will reconnect - you can close this page.';}
  else{m.className='err';m.textContent='Failed to save. Try again.';}
}
</script>
</body>
</html>
)rawliteral";

static String renderPortalHtml() {
  String html = PORTAL_HTML;
  html.replace("__DEVICE_ID__", deviceId());
  html.replace("__BIND_KEY__", bindKey());
  return html;
}

static void handleRoot() {
  portalServer.send(200, "text/html", renderPortalHtml());
}

static void handleSave() {
  if (portalServer.method() != HTTP_POST) {
    portalServer.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, portalServer.arg("plain"));
  if (error) {
    portalServer.send(400, "text/plain", "Bad JSON");
    return;
  }
  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";
  ssid.trim();
  if (ssid.length() == 0) {
    portalServer.send(400, "text/plain", "Missing SSID");
    return;
  }
  if (!saveWiFiCredentialsToNVS(ssid, pass, 1)) {
    portalServer.send(500, "text/plain", "Save failed");
    return;
  }
  portalServer.send(200, "text/plain", "OK");
  Serial.printf("[PORTAL] Saved credentials for '%s', switching to STA...\n", ssid.c_str());

  // Give the response time to flush, then switch to station mode.
  delay(300);
  stopCaptivePortal();
  WiFi.mode(WIFI_STA);
  connectToSavedNetworks();
}

static void handleNotFound() {
  // Any hostname resolves to us via the captive portal DNS: redirect to /.
  portalServer.sendHeader("Location", "/", true);
  portalServer.send(302, "text/html", "");
}

static void portalTask(void* param) {
  (void)param;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(PORTAL_AP_SSID)) {
    Serial.println("[PORTAL] Failed to start softAP");
    portalRunning = false;
    vTaskDelete(NULL);
    return;
  }
  Serial.printf("[PORTAL] AP '%s' up, IP %s\n", PORTAL_AP_SSID, WiFi.softAPIP().toString().c_str());
  Serial.printf("[PORTAL] deviceId=%s bindKey=%s\n", deviceId().c_str(), bindKey().c_str());

  dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
  portalServer.on("/", handleRoot);
  portalServer.on("/save", handleSave);
  portalServer.onNotFound(handleNotFound);
  portalServer.begin();

  portalRunning = true;
  unsigned long startMs = millis();
  while (portalRunning) {
    dnsServer.processNextRequest();
    portalServer.handleClient();
    // 10 minute safety timeout so the device never sits in AP forever.
    if (millis() - startMs > 10UL * 60UL * 1000UL) {
      Serial.println("[PORTAL] Provisioning timeout, shutting down");
      break;
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }

  dnsServer.stop();
  portalServer.stop();
  WiFi.softAPdisconnect(true);
  portalRunning = false;
  Serial.println("[PORTAL] AP stopped");
  vTaskDelete(NULL);
}

void startCaptivePortal() {
  if (portalRunning) return;
  xTaskCreate(portalTask, "portal", 8192, NULL, 1, NULL);
}

void stopCaptivePortal() {
  portalRunning = false;
}

bool captivePortalActive() {
  return portalRunning;
}