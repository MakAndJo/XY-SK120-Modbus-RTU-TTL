# MQTT Protocol (XY-SK150 <-> Server <-> Client)

This document is the contract between the firmware, the server and the browser
client. Version 1.

## Overview

The device talks to the world **only over MQTT**. There is no HTTP server in the
firmware (except the temporary first-boot captive portal, see below). A central
server runs an MQTT broker and a WebSocket gateway for the browser UI. The
browser never speaks MQTT: it sends JSON frames over a native `WebSocket`. The
server does not run a second MQTT client either — the gateway hooks directly
into the in-process broker's publish stream (`broker.on('publish')` for device
traffic, `broker.publish()` for commands).

```
+----------------+   MQTT (TCP 1883)   +----------------+   WebSocket       +----------------+
|  Firmware      | <-----------------> |   Server       | <---------------> |  Browser       |
|  (ESP32)       |                     |   (aedes)      |   (WS /ws :8080) |  client        |
+----------------+                     +----------------+                   +----------------+
                                          |  POST /api/bind  { key }
                                          v
                                     HTTP :8080 (UI + WS gateway + bind)
```

### WebSocket gateway protocol

One JSON frame per message; text frames only.

Client → server:

| Frame | Meaning |
|-------|---------|
| `{ "type":"bind", "key":"<bind key>" }` | bind the device by key |
| `{ "type":"subscribe", "deviceId":"dev_A1B2C3" }` | subscribe to this device's frames |
| `{ "type":"unsubscribe", "deviceId":"dev_A1B2C3" }` | unsubscribe |
| `{ "type":"command", "deviceId":"dev_A1B2C3", "payload":{ "action":"setVoltage", ... } }` | send a device command |
| `{ "type":"ping" }` | keep-alive |

Server → client:

| Frame | Meaning |
|-------|---------|
| `{ "type":"bindResult", "ok":true, "deviceId":..., "userId":..., "name":..., "model":... }` | bind success |
| `{ "type":"bindResult", "ok":false, "error":"..." }` | bind failure |
| `{ "type":"info", "deviceId":..., "data":{...} }` | device identity (retained) |
| `{ "type":"status", "deviceId":..., "data":{...} }` | PSU status snapshot |
| `{ "type":"online", "deviceId":..., "online":true\|false }` | device online state |
| `{ "type":"response", "deviceId":..., "data":{...} }` | device reply to a command |
| `{ "type":"error", "message":"..." }` | protocol error |

Subscribing to a device immediately delivers its cached retained `info`/`status`/
`online` state, so a page refresh always shows the latest known values.

## Identity

| Field | Value |
|-------|-------|
| `deviceId` | `dev_` + last 6 hex chars of the MAC address, e.g. `dev_A1B2C3` |
| bind `key` | `md5(deviceId)` — deterministic, shown on the device setup screen |

> **Security note**: md5 is not secret. Anyone who knows the MAC can compute the
> key. It is an ownership token ("whoever sets the device up owns it"), not a
> credential. A future firmware can use a random one-time code stored in NVS
> instead without changing the wire format.

## Topics

All topics are namespaced under `xysk/<deviceId>/...`.

| Topic | QoS | Retained | Direction | Payload |
|-------|-----|----------|-----------|---------|
| `xysk/<deviceId>/info` | 1 | yes | device → broker | device identity + bind key (see below) |
| `xysk/<deviceId>/status` | 1 | yes | device → broker | current PSU status snapshot |
| `xysk/<deviceId>/online` | 1 | yes (LWT) | device → broker | `1` alive, `0` died |
| `xysk/<deviceId>/command` | 1 | no | server → device | one command object |
| `xysk/<deviceId>/response` | 1 | no | device → server | reply to a command |

### `/info` (retained, published once on connect)

```json
{
  "deviceId": "dev_A1B2C3",
  "key": "bf618ad743f5c8660eabc4df35975fa4",
  "name": "XY-SK150S",
  "model": "XY-SK150S"
}
```

The server only accepts `/info` when `key == md5(deviceId)` (anti-spoofing) and
registers the device. Registration is what makes `/api/bind` resolvable.

### `/online` (LWT)

The device connects with `will.topic = xysk/<deviceId>/online`,
`will.retain = true`, `will.qos = 1`, `will.payload = "0"`. On connect it
publishes retained `1`. A crash/drop publishes retained `0`.

## Status schema (`/status`)

Published retained **only when the value changed** (diff), at most every ~250 ms.
Field `action` = `statusResponse` keeps wire parity with the old web UI.

```json
{
  "action": "statusResponse",
  "connected": true,
  "outputEnabled": false,
  "voltage": 0.0, "current": 0.0, "power": 0.0,
  "inputVoltage": 13.5,
  "voltageSet": 12.0, "currentSet": 1.0, "powerSet": 0.0,
  "cvccMode": 1,
  "cpModeEnabled": false,
  "ampHours": 0.123, "wattHours": 4.56, "outputTime": 3600,
  "internalTemp": 25.4, "externalTemp": 26.1,
  "protectionStatus": 0,
  "lvp": 0.0, "ovp": 60.0, "ocp": 6.0, "opp": 600.0, "otp": 60,
  "tempCelsius": true,
  "backlight": 5, "sleepTimeout": 2,
  "slaveAddress": 1, "baudRateCode": 6,
  "beeper": true,
  "memoryGroup": 0,
  "mpptEnabled": false, "mpptThreshold": 0.0, "batteryCutoff": 0.0,
  "outputOnAtStartup": false, "etp": 0.0,
  "bchEnabled": false, "bchThreshold": 0.0,
  "btfEnabled": false, "btfCutoff": 0.0,
  "clofEnabled": false,
  "hostType": 14874, "wifiConfig": 0, "wifiStatus": 5, "ipv4": 3232235906,
  "ohpHours": 0, "ohpMinutes": 0, "overAmpHours": 0.0, "overWattHours": 0.0,
  "operatingMode": 1, "setValue": 0.0,
  "deviceName": "XY-SK150S",
  "model": "XY-SK150S", "version": 100,
  "keyLockEnabled": false
}
```

Decode notes:

- `voltage`/`voltageSet` in V, `current`/`currentSet` in A, `power` in W.
- `cvccMode`: 1 = CV, 2 = CC, 3 = CP.
- `outputTime` in seconds (since last power-on).
- `protectionStatus`: bitmask / OEM semantics (0 = normal). `0x0001` OVP, `0x0002` OCP, etc. — same as `0x0010` register.
- `ipv4`: packed uint32 of the device's LAN IP.
- `wifiStatus`: `0` offline, `5` connected (PSU WiFi-module block `0x0033`).

## Commands (`/command`)

The server publishes **one command per message**. The device replies on
`/response` with `{"action": "<action>Response", ...}`.

| `action` | Required fields | Reply |
|----------|-----------------|-------|
| `ping` | — | `pingResponse` |
| `getData`, `getStatus` | — | full status JSON (like `/status`, non-retained) |
| `getTimeZone` | — | `timeZoneData` with `timeZones` array + `current` (list from the device, replaces the old `/api/timezone` HTTP call) |
| `getWifiStatus` | — | `wifiStatusResponse` with `wifiStatus` = JSON string (SSID/IP/RSSI/MAC of the ESP32 radio, not the PSU WiFi module) |
| `addWifiNetwork` | `ssid`, `password` | saves to NVS, reconnects if possible, replies `addWifiNetworkResponse` |
| `setConfig` | `voltage` and/or `current` and/or `power` | `setConfigResponse` |
| `powerOutput` | `enable` bool | `powerOutputResponse` |
| `setVoltage` | `voltage` (V) | `setVoltageResponse` |
| `setCurrent` | `current` (A) | `setCurrentResponse` |
| `setPower` | `power` (W) | `setPowerResponse` |
| `setKeyLock` | `enabled` bool | `setKeyLockResponse` |
| `setConstantVoltage` | `voltage` (V) | `setConstantVoltageResponse` |
| `setConstantCurrent` | `current` (A) | `setConstantCurrentResponse` |
| `setConstantPower` | `power` (W) | `setConstantPowerResponse` |
| `setConstantPowerMode` | `enabled` bool | `setConstantPowerModeResponse` |
| `getOperatingMode` | — | `getOperatingModeResponse` with `operatingMode` (1/2/3) + `setValue` |
| `setTimeZone` | `index` (int into TIME_ZONES) | `setTimeZoneResponse` with `success` + `timeZone` |
| `restart` | — | reboots the ESP32 |
| `setProtection` | one or more of `lvp`,`ovp`,`ocp`,`opp`,`otp` | `<key>Response` |
| `setBacklight` | `level` (1-8) | `setBacklightResponse` |
| `setSleepTimeout` | `minutes` | `setSleepTimeoutResponse` |
| `setSlaveAddress` | `address` | `setSlaveAddressResponse` |
| `setBaudRate` | `code` | `setBaudRateResponse` |
| `setTempUnit` | `celsius` bool | `setTempUnitResponse` |
| `setBeeper` | `enabled` bool | `setBeeperResponse` |
| `setMppt` | `enabled` bool | `setMpptResponse` |
| `setBatteryCutoff` | `value` (A) | `setBatteryCutoffResponse` |
| `setBch` | `enabled` bool, `threshold` (A) | `setBchResponse` |
| `setBtfEnable` | `enabled` bool | `setBtfEnableResponse` |
| `setBtfCutoff` | `value` (A) | `setBtfCutoffResponse` |
| `setClof` | `enabled` bool | `setClofResponse` |
| `setPowerOnInit` | `enabled` bool | `setPowerOnInitResponse` |
| `setCpMode` | `enabled` bool | `setCpModeResponse` |
| `setOhp` | `hours`, `minutes` | `setOhpResponse` |
| `setOha` | `value` (Ah) | `setOhaResponse` |
| `setOwh` | `value` (Wh) | `setOwhResponse` |
| `setMemoryGroup` | `group` (0-9) | `setMemoryGroupResponse` |
| `getMemoryGroup` | — | `getMemoryGroupResponse` (profile JSON) |
| `saveMemoryGroup` | `group` (0-9) | `saveMemoryGroupResponse` |
| `psuReset` | — | `psuResetResponse` |
| `clearProtection` | — | `clearProtectionResponse` |

Unknown actions produce no reply. Values are float unless noted.

## Bind flow

```
1. User provisions the device (captive portal / first boot) → device WiFi up.
2. Device connects to broker, publishes retained /info → server registers it.
3. User opens the client at http://<server>:8080, enters the bind key shown
   on the device setup screen.
4. Client sends { "type":"bind", "key":"..." } over the WebSocket gateway
   (or POSTs /api/bind). Server replies:
     ok:true  → { deviceId, userId, name, model } and auto-subscribes the device
     ok:false → error (404 key unknown / 409 already bound to a *different* user).
               Because the key is deterministic (md5(deviceId)), re-binding the
               same owner from a fresh browser is idempotent and returns ok:true.
5. The gateway forwards xysk/<deviceId>/info|status|online|response frames to
   the browser and publishes commands to xysk/<deviceId>/command on its behalf.
```

The device is unaware of users; isolation is enforced by the server (a device
only listens to its own `<deviceId>/command` topic, and `userId` is only used
for the bind bookkeeping in `server/data/devices.json`).

## Captive portal (first boot)

When no saved WiFi works, the device starts an AP `XY-SK150-Setup`
(192.168.4.1) with one page that shows `deviceId` + bind `key` and accepts
SSID/password. Credentials go to NVS (`wificonfig` namespace). This portal
stops as soon as the device connects to the home network. OTA uses the standard
ArduinoOTA UDP port 3232 (`pio run -e <env> -t upload --upload_port <ip>`).

## MQTT settings on the device

Stored in NVS namespace `mqttc` (keys `host`, `port`, `user`, `pass`, `name`),
editable via the serial console (`mqtt set <host> [port] [user] [pass]`).
Currently there is **no TLS** on the device→broker link; the `user`/`pass`
fields are passed through to the broker if it requires authentication.
