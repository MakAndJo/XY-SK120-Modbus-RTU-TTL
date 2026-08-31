#include "mqtt_manager.h"
#include "wifi_interface/wifi_native.h"
#include "modbus/psu_service.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>

static WiFiClient mqttWifiClient;
static PubSubClient mqttClient(mqttWifiClient);
static volatile bool mqttTaskRunning = false;
static String mqttDeviceIdStr;

// PubSubClient::setServer(const char*) stores a pointer, it does NOT copy the
// string. mqttHost() returns a temporary String, so passing host.c_str() there
// would leave _domain dangling after the call. Keep a stable buffer instead.
static char mqttHostBuf[96] = {0};

static const char* ONLINE_TOPIC_TPL  = "xysk/%s/online";
static const char* INFO_TOPIC_TPL    = "xysk/%s/info";
static const char* STATUS_TOPIC_TPL  = "xysk/%s/status";
static const char* COMMAND_TOPIC_TPL = "xysk/%s/command";
static const char* RESPONSE_TOPIC_TPL = "xysk/%s/response";

String mqttDeviceId() {
  if (mqttDeviceIdStr.length() == 0) {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mqttDeviceIdStr = "dev_" + mac.substring(mac.length() - 6);
  }
  return mqttDeviceIdStr;
}

bool mqttConfigLoaded() {
  Preferences prefs;
  bool ok = prefs.begin(MQTT_NAMESPACE, true);
  if (!ok) return true; // NVS unavailable -> fall back to the compile-time default
  String host = prefs.getString(MQTT_HOST_KEY, "");
  prefs.end();
  return host.length() > 0 || strlen(MQTT_DEFAULT_HOST) > 0;
}

String mqttHost() {
  Preferences prefs;
  String host = MQTT_DEFAULT_HOST;
  if (prefs.begin(MQTT_NAMESPACE, true)) {
    host = prefs.getString(MQTT_HOST_KEY, MQTT_DEFAULT_HOST);
    prefs.end();
  }
  return host;
}

uint16_t mqttPort() {
  Preferences prefs;
  uint16_t port = MQTT_DEFAULT_PORT;
  if (prefs.begin(MQTT_NAMESPACE, true)) {
    port = prefs.getUShort(MQTT_PORT_KEY, MQTT_DEFAULT_PORT);
    prefs.end();
  }
  return port;
}

String mqttUser() {
  Preferences prefs;
  String user = "";
  if (prefs.begin(MQTT_NAMESPACE, true)) {
    user = prefs.getString(MQTT_USER_KEY, "");
    prefs.end();
  }
  return user;
}

String mqttPass() {
  Preferences prefs;
  String pass = "";
  if (prefs.begin(MQTT_NAMESPACE, true)) {
    pass = prefs.getString(MQTT_PASS_KEY, "");
    prefs.end();
  }
  return pass;
}

String mqttDeviceName() {
  Preferences prefs;
  String name = "";
  if (prefs.begin(MQTT_NAMESPACE, true)) {
    name = prefs.getString(MQTT_NAME_KEY, "");
    prefs.end();
  }
  if (name.length() == 0) name = "XY-SK150S";
  return name;
}

void mqttSaveConfig(const String& host, uint16_t port,
                    const String& user, const String& pass) {
  Preferences prefs;
  prefs.begin(MQTT_NAMESPACE, false);
  prefs.putString(MQTT_HOST_KEY, host);
  prefs.putUShort(MQTT_PORT_KEY, port);
  prefs.putString(MQTT_USER_KEY, user);
  prefs.putString(MQTT_PASS_KEY, pass);
  prefs.end();
  Serial.printf("[MQTT] Config saved: %s:%d user='%s'\n",
                host.c_str(), port, user.c_str());
}

void mqttSetEnabled(bool on) {
  Preferences prefs;
  prefs.begin(MQTT_NAMESPACE, false);
  prefs.putChar(MQTT_ENABLE_KEY, on ? 1 : 0);
  prefs.end();
  Serial.printf("[MQTT] %s\n", on ? "enabled" : "disabled");
}

bool mqttEnabled() {
  Preferences prefs;
  int8_t on = 0;
  if (prefs.begin(MQTT_NAMESPACE, true)) {
    on = prefs.getChar(MQTT_ENABLE_KEY, 0);
    prefs.end();
  }
  return on == 1;
}

// Connection gate: AP mode (REG_WIFI_CONFIG=2) never enables MQTT; otherwise
// only when the user enabled MQTT AND explicitly configured a broker host
// (the compile-time default alone is not enough — "no IP = no connection").
bool mqttShouldConnect() {
  if (getWifiConfigState() == 2) return false; // AP: local AP panel only
  if (!mqttEnabled()) return false;
  Preferences prefs;
  String host = "";
  if (prefs.begin(MQTT_NAMESPACE, true)) {
    host = prefs.getString(MQTT_HOST_KEY, "");
    prefs.end();
  }
  return host.length() > 0;
}

static void publishInfo() {
  String infoTopic = String(INFO_TOPIC_TPL);
  infoTopic.replace("%s", mqttDeviceId());

  DynamicJsonDocument doc(256);
  doc["deviceId"] = mqttDeviceId();
  doc["name"] = mqttDeviceName();
  doc["model"] = "XY-SK150S";
  String json;
  serializeJson(doc, json);
  mqttClient.publish(infoTopic.c_str(), json.c_str(), true);
}

// ---- Home Assistant MQTT discovery ------------------------------------------

// Publish retained discovery configs so HASS auto-creates all entities for this
// device. Each config points at the shared status/command topics; value and
// command templates map the fields.

static void addDiscDevice(DynamicJsonDocument& doc) {
  JsonObject dev = doc.createNestedObject("device");
  JsonArray ids = dev.createNestedArray("identifiers");
  ids.add("xysk_" + mqttDeviceId());
  dev["name"] = mqttDeviceName() + " (" + mqttDeviceId() + ")";
  dev["model"] = "XY-SK150S";
  dev["manufacturer"] = "XY";
}

static void pubDisc(const String& component, const String& key, DynamicJsonDocument& doc) {
  String topic = String("homeassistant/") + component + "/xysk_" + mqttDeviceId() + "_" + key + "/config";
  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

static void discSensor(const String& key, const String& name,
                       const String& statusTopic, const char* tmpl,
                       const char* unit, const char* devClass, const char* stateClass) {
  DynamicJsonDocument doc(512);
  doc["name"] = name;
  doc["unique_id"] = "xysk_" + mqttDeviceId() + "_" + key;
  doc["state_topic"] = statusTopic;
  doc["value_template"] = tmpl;
  if (unit && unit[0]) doc["unit_of_measurement"] = unit;
  if (devClass && devClass[0]) doc["device_class"] = devClass;
  if (stateClass && stateClass[0]) doc["state_class"] = stateClass;
  addDiscDevice(doc);
  pubDisc("sensor", key, doc);
}

static void discSwitch(const String& key, const String& name,
                       const String& statusTopic, const String& commandTopic,
                       const char* stateExpr, const char* onCmd, const char* offCmd) {
  DynamicJsonDocument doc(512);
  doc["name"] = name;
  doc["unique_id"] = "xysk_" + mqttDeviceId() + "_" + key;
  doc["state_topic"] = statusTopic;
  doc["value_template"] = stateExpr;
  doc["command_topic"] = commandTopic;
  doc["payload_on"] = onCmd;
  doc["payload_off"] = offCmd;
  doc["state_on"] = "ON";
  doc["state_off"] = "OFF";
  addDiscDevice(doc);
  pubDisc("switch", key, doc);
}

static void discNumber(const String& key, const String& name,
                       const String& statusTopic, const String& commandTopic,
                       const char* tmpl, const char* cmdTmpl,
                       float min, float max, float step, const char* unit) {
  DynamicJsonDocument doc(512);
  doc["name"] = name;
  doc["unique_id"] = "xysk_" + mqttDeviceId() + "_" + key;
  doc["state_topic"] = statusTopic;
  doc["value_template"] = tmpl;
  doc["command_topic"] = commandTopic;
  doc["command_template"] = cmdTmpl;
  doc["min"] = min;
  doc["max"] = max;
  doc["step"] = step;
  doc["mode"] = "box";
  if (unit && unit[0]) doc["unit_of_measurement"] = unit;
  addDiscDevice(doc);
  pubDisc("number", key, doc);
}

static void discSelect(const String& key, const String& name,
                       const String& statusTopic, const String& commandTopic,
                       const char* stateTmpl, const char* cmdTmpl, const char* options) {
  DynamicJsonDocument doc(512);
  doc["name"] = name;
  doc["unique_id"] = "xysk_" + mqttDeviceId() + "_" + key;
  doc["state_topic"] = statusTopic;
  doc["value_template"] = stateTmpl;
  doc["command_topic"] = commandTopic;
  doc["command_template"] = cmdTmpl;
  JsonArray opts = doc.createNestedArray("options");
  String s = options;
  int start = 0;
  for (;;) {
    int c = s.indexOf(',', start);
    if (c < 0) { opts.add(s.substring(start)); break; }
    opts.add(s.substring(start, c));
    start = c + 1;
  }
  addDiscDevice(doc);
  pubDisc("select", key, doc);
}

static void discButton(const String& key, const String& name,
                       const String& commandTopic, const char* pressCmd) {
  DynamicJsonDocument doc(256);
  doc["name"] = name;
  doc["unique_id"] = "xysk_" + mqttDeviceId() + "_" + key;
  doc["command_topic"] = commandTopic;
  doc["payload_press"] = pressCmd;
  addDiscDevice(doc);
  pubDisc("button", key, doc);
}

static void publishDiscovery() {
  String devId = mqttDeviceId();
  String statusTopic = String("xysk/") + devId + "/status";
  String commandTopic = String("xysk/") + devId + "/command";

  // Sensors (history/graphs via state_class)
  discSensor("voltage",        "PSU Voltage",        statusTopic, "{{ value_json.voltage }}",      "V",  "voltage", "measurement");
  discSensor("current",        "PSU Current",        statusTopic, "{{ value_json.current }}",      "A",  "current", "measurement");
  discSensor("power",          "PSU Power",          statusTopic, "{{ value_json.power }}",        "W",  "power",   "measurement");
  discSensor("input_voltage",  "PSU Input Voltage",  statusTopic, "{{ value_json.inputVoltage }}", "V",  "voltage", "measurement");
  discSensor("amp_hours",      "PSU Amp-hours",      statusTopic, "{{ value_json.ampHours }}",     "Ah", "", "total_increasing");
  discSensor("watt_hours",     "PSU Watt-hours",     statusTopic, "{{ value_json.wattHours }}",    "Wh", "", "total_increasing");
  discSensor("output_time",    "PSU Output Time",    statusTopic, "{{ value_json.outputTime }}",   "s",  "duration", "measurement");
  discSensor("internal_temp",  "PSU Internal Temp",  statusTopic, "{{ value_json.internalTemp }}", "°C", "temperature", "measurement");
  discSensor("external_temp",  "PSU External Temp",  statusTopic, "{{ value_json.externalTemp }}", "°C", "temperature", "measurement");
  discSensor("mode",           "PSU Mode",           statusTopic, "{{ value_json.operatingMode }}", "", "", "");
  discSensor("protection",     "PSU Protection",     statusTopic, "{{ value_json.protectionStatus }}", "", "", "");

  // Switches
  discSwitch("output", "PSU Output", statusTopic, commandTopic,
             "{{ 'ON' if value_json.outputEnabled else 'OFF' }}",
             "{\"action\":\"powerOutput\",\"enable\":true}",
             "{\"action\":\"powerOutput\",\"enable\":false}");
  discSwitch("keylock", "PSU Key Lock", statusTopic, commandTopic,
             "{{ 'ON' if value_json.keyLockEnabled else 'OFF' }}",
             "{\"action\":\"setKeyLock\",\"lock\":true}",
             "{\"action\":\"setKeyLock\",\"lock\":false}");

  // Numbers (setpoints)
  discNumber("vset", "PSU Voltage Set", statusTopic, commandTopic,
             "{{ value_json.voltageSet }}",
             "{\"action\":\"setVoltage\",\"voltage\":{{ value }}}",
             0, 150, 0.01, "V");
  discNumber("iset", "PSU Current Set", statusTopic, commandTopic,
             "{{ value_json.currentSet }}",
             "{\"action\":\"setCurrent\",\"current\":{{ value }}}",
             0, 65, 0.001, "A");
  discNumber("pset", "PSU Power Set", statusTopic, commandTopic,
             "{{ value_json.powerSet }}",
             "{\"action\":\"setPower\",\"power\":{{ value }}}",
             0, 150, 0.1, "W");

  // Backlight (screen brightness)
  discNumber("backlight", "PSU Backlight", statusTopic, commandTopic,
             "{{ value_json.backlight }}",
             "{\"action\":\"setBacklight\",\"level\":{{ value }}}",
             1, 5, 1, "");

  // Screensaver type per PSU state (0 off, 1 clock, 2+ clock+weather)
  discSelect("ss_idle", "PSU Screensaver Idle", statusTopic, commandTopic,
             "{{ value_json.screensaverIdle }}",
             "{\"action\":\"setScreensaver\",\"state\":\"idle\",\"type\":{{ value }}}",
             "0,1,2");
  discSelect("ss_suspend", "PSU Screensaver Suspend", statusTopic, commandTopic,
             "{{ value_json.screensaverSuspend }}",
             "{\"action\":\"setScreensaver\",\"state\":\"suspend\",\"type\":{{ value }}}",
             "0,1,2");

  // Weather fetch on/off + coordinates
  discSwitch("weather", "PSU Weather", statusTopic, commandTopic,
             "{{ 'ON' if value_json.weatherEnabled else 'OFF' }}",
             "{\"action\":\"setWeather\",\"enabled\":true}",
             "{\"action\":\"setWeather\",\"enabled\":false}");
  discNumber("lat", "PSU Latitude", statusTopic, commandTopic,
             "{{ value_json.weatherLat }}",
             "{\"action\":\"setWeather\",\"lat\":{{ value }}}",
             -90, 90, 0.0001, "");
  discNumber("lon", "PSU Longitude", statusTopic, commandTopic,
             "{{ value_json.weatherLon }}",
             "{\"action\":\"setWeather\",\"lon\":{{ value }}}",
             -180, 180, 0.0001, "");

  // Actions
  discButton("wake", "PSU Wake", commandTopic, "{\"action\":\"wakeUp\"}");
  discButton("reset_energy", "PSU Reset Energy", commandTopic, "{\"action\":\"resetEnergy\"}");

  Serial.println("[MQTT] Discovery configs published");
}

static String mqttLastStatus = "";
static bool mqttStatusFresh = false;

void mqttPublishStatus() {
  if (!mqttConnected()) return;
  String statusTopic = String(STATUS_TOPIC_TPL);
  statusTopic.replace("%s", mqttDeviceId());
  PSUStatusData data;
  if (readPSUStatusBatched(data)) {
    String json = buildStatusJSON(data);
    // Diff logic: only publish when the status actually changed, so the broker
    // doesn't get a retained-status flood every poll cycle.
    if (mqttStatusFresh && json == mqttLastStatus) return;
    mqttLastStatus = json;
    mqttStatusFresh = true;
    mqttClient.publish(statusTopic.c_str(), json.c_str(), true);
  }
}

static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  String payloadStr;
  payloadStr.reserve(len + 1);
  for (unsigned int i = 0; i < len; i++) payloadStr += (char)payload[i];

  // Extract the action field. Commands arrive as {"action":"setVoltage", ...}
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, payloadStr);
  if (error) {
    Serial.println("[MQTT] Bad command JSON");
    return;
  }
  String action = doc["action"] | "";
  if (action.length() == 0) {
    Serial.println("[MQTT] Command without action");
    return;
  }

  Serial.printf("[MQTT] command: %s\n", action.c_str());
  String response = handleMqttAction(action, payloadStr.c_str());
  if (response.length() == 0) return;

  String responseTopic = String(RESPONSE_TOPIC_TPL);
  responseTopic.replace("%s", mqttDeviceId());
  mqttClient.publish(responseTopic.c_str(), response.c_str(), false);
}

static void mqttTask(void* param) {
  (void)param;
  String id = mqttDeviceId();
  String onlineTopic = String(ONLINE_TOPIC_TPL);
  onlineTopic.replace("%s", id);

  while (mqttTaskRunning) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      continue;
    }
    if (!mqttClient.connected()) {
      String clientId = String("xy-") + id;
      // Copy into a stable buffer: PubSubClient::setServer stores the pointer.
      strlcpy(mqttHostBuf, mqttHost().c_str(), sizeof(mqttHostBuf));
      mqttClient.setServer(mqttHostBuf, mqttPort());
      mqttClient.setCallback(onMqttMessage);
      mqttClient.setBufferSize(2048);
      // Last-will: publish retained 0 to <device>/online if we drop off.
      String user = mqttUser();
      String pass = mqttPass();
      bool connOk = user.length() > 0
        ? mqttClient.connect(clientId.c_str(), user.c_str(), pass.c_str(),
                             onlineTopic.c_str(), 1, true, "0")
        : mqttClient.connect(clientId.c_str(),
                             onlineTopic.c_str(), 1, true, "0");
      if (connOk) {
        Serial.printf("[MQTT] Connected to %s:%d\n", mqttHost().c_str(), mqttPort());
        mqttClient.publish(onlineTopic.c_str(), "1", true);
        mqttStatusFresh = false; // force status republish on (re)connect
        mqttLastStatus = "";

        publishInfo();
        publishDiscovery();
        mqttPublishStatus();

        String commandTopic = String(COMMAND_TOPIC_TPL);
        commandTopic.replace("%s", id);
        mqttClient.subscribe(commandTopic.c_str());
        Serial.printf("[MQTT] Subscribed to %s\n", commandTopic.c_str());
      } else {
        Serial.printf("[MQTT] Connect failed rc=%d\n", mqttClient.state());
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        continue;
      }
    }
    mqttClient.loop();
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }

  // Clean shutdown: release LWT
  if (mqttClient.connected()) {
    mqttClient.publish(onlineTopic.c_str(), "0", true);
    mqttClient.disconnect();
  }
  vTaskDelete(NULL);
}

void mqttStart() {
  if (mqttTaskRunning) return;
  if (!mqttShouldConnect()) {
    Serial.println("[MQTT] connect declined (disabled or no host)");
    return;
  }
  mqttTaskRunning = true;
  xTaskCreate(mqttTask, "mqtt", 8192, NULL, 1, NULL);
  Serial.println("[MQTT] Client task started");
}

void mqttStop() {
  mqttTaskRunning = false;
}

bool mqttConnected() {
  return mqttTaskRunning && mqttClient.connected();
}