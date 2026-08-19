#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>

// NVS namespace for MQTT config
#define MQTT_NAMESPACE "mqttc"
#define MQTT_HOST_KEY "host"
#define MQTT_PORT_KEY "port"
#define MQTT_USER_KEY "user"
#define MQTT_PASS_KEY "pass"
#define MQTT_NAME_KEY "name"

// Compile-time default broker. Used when nothing is saved in NVS yet, so the
// device connects right after flashing. Override with serial 'mqtt set ...'.
#define MQTT_DEFAULT_HOST   "192.168.0.200"
#define MQTT_DEFAULT_PORT   1883

// Topic root: xysk/<deviceId>/...
String mqttDeviceId();

// Load MQTT config from NVS. Returns true if a host is configured.
bool mqttConfigLoaded();
String mqttHost();
uint16_t mqttPort();
String mqttUser();
String mqttPass();
String mqttDeviceName();

// Persist MQTT config (used by serial 'mqtt set').
void mqttSaveConfig(const String& host, uint16_t port,
                    const String& user, const String& pass,
                    const String& deviceName);

// Start/stop the MQTT client background task. start() spawns a task that
// connects, publishes retained info/status, subscribes to <device>/command and
// publishes responses. stop() disconnects and halts the task.
void mqttStart();
void mqttStop();

// True while the client is connected to the broker.
bool mqttConnected();

// Publish the current status JSON as retained <device>/status.
void mqttPublishStatus();

#endif // MQTT_MANAGER_H