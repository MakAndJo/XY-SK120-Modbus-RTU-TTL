#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>

// NVS namespace for MQTT config
#define MQTT_NAMESPACE "mqttc"
#define MQTT_HOST_KEY "host"
#define MQTT_PORT_KEY "port"
#define MQTT_NAME_KEY "name"
#define MQTT_PAIR_KEY "pcode"
#define MQTT_BOUND_KEY "bound"

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
String mqttDeviceName();

// Persist MQTT config (used by serial 'mqtt set' and the local web UI).
// The broker is open (no user/password); only clientId xy-<deviceId> is used.
void mqttSaveConfig(const String& host, uint16_t port);

// Pair code shown on the PSU screen until the device is bound to a server
// account. Stored in NVS so it survives reboots. Empty string = bound / no code.
String mqttGetPairCode();
void mqttSetPairCode(const String& code);

// Bound state derived from the pair code: a device with a code is unbound.
// The server clears the code on bind and issues a fresh one on unbind.
bool mqttGetBound();

// Touch pairing requested from the block: generate a fresh pair code on the
// device immediately (it starts showing in the PSU IP field right away) and
// tell the server to drop the old binding and record the new code.
void mqttRequestRepair();
void mqttCancelRepair();

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