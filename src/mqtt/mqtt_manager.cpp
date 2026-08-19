#include "mqtt_manager.h"
#include "wifi_interface/wifi_native.h"
#include "modbus/psu_service.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <mbedtls/md5.h>

static WiFiClient mqttWifiClient;
static PubSubClient mqttClient(mqttWifiClient);
static volatile bool mqttTaskRunning = false;
static volatile bool mqttEnabled = false;
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
                    const String& user, const String& pass,
                    const String& deviceName) {
  Preferences prefs;
  prefs.begin(MQTT_NAMESPACE, false);
  prefs.putString(MQTT_HOST_KEY, host);
  prefs.putUShort(MQTT_PORT_KEY, port);
  prefs.putString(MQTT_USER_KEY, user);
  prefs.putString(MQTT_PASS_KEY, pass);
  prefs.putString(MQTT_NAME_KEY, deviceName);
  prefs.end();
  Serial.println("[MQTT] Config saved");
}

static void publishInfo() {
  String infoTopic = String(INFO_TOPIC_TPL);
  infoTopic.replace("%s", mqttDeviceId());
  String infoKey = String("");
  uint8_t digest[16];
  char hex[33];
  // bind key = md5(deviceId)
  String id = mqttDeviceId();
  mbedtls_md5((const uint8_t*)id.c_str(), id.length(), digest);
  for (int i = 0; i < 16; i++) {
    sprintf(hex + i * 2, "%02x", digest[i]);
  }
  hex[32] = 0;
  infoKey = String(hex);

  DynamicJsonDocument doc(256);
  doc["deviceId"] = mqttDeviceId();
  doc["key"] = infoKey;
  doc["name"] = mqttDeviceName();
  doc["model"] = "XY-SK150S";
  String json;
  serializeJson(doc, json);
  mqttClient.publish(infoTopic.c_str(), json.c_str(), true);
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
      // Last-will: publish retained 0 to <device>/online if we drop off.
      mqttClient.setServer(mqttHostBuf, mqttPort());
      mqttClient.setCallback(onMqttMessage);
      mqttClient.setBufferSize(2048);
      bool connOk = mqttClient.connect(clientId.c_str(), mqttUser().c_str(), mqttPass().c_str(),
                                       onlineTopic.c_str(), 1, true, "0");
      if (connOk) {
        Serial.printf("[MQTT] Connected to %s:%d\n", mqttHost().c_str(), mqttPort());
        mqttClient.publish(onlineTopic.c_str(), "1", true);
        mqttStatusFresh = false; // force status republish on (re)connect
        mqttLastStatus = "";

        publishInfo();
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
  if (!mqttConfigLoaded()) {
    // First run: materialise the compile-time default into NVS so the namespace
    // exists and the reconnect loop doesn't spam nvs_open NOT_FOUND 4x/cycle.
    mqttSaveConfig(MQTT_DEFAULT_HOST, MQTT_DEFAULT_PORT, "", "", "XY-SK150S");
    Serial.printf("[MQTT] No config in NVS, using default host %s:%d\n",
                  MQTT_DEFAULT_HOST, MQTT_DEFAULT_PORT);
  }
  mqttTaskRunning = true;
  mqttEnabled = true;
  xTaskCreate(mqttTask, "mqtt", 8192, NULL, 1, NULL);
  Serial.println("[MQTT] Client task started");
}

void mqttStop() {
  mqttTaskRunning = false;
  mqttEnabled = false;
}

bool mqttConnected() {
  return mqttEnabled && mqttClient.connected();
}