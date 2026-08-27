# MQTT Protocol (XY-SK150)

The device connects to any MQTT broker (e.g. the mosquitto add-on in Home
Assistant) and exposes its power-supply state and commands under
`xysk/<deviceId>/...`. There is no external server: the panel is served by the
device itself over HTTP + WebSocket, and MQTT is an optional publish/control
channel (enabled in the panel / serial, requires at least a broker IP).

```
+----------------+   HTTP :80 + WS /ws   +----------------+
|  Firmware      | <--------------------> |  Browser       |   local panel
|  (ESP32)       |                        +----------------+
+----------------+
     |
     | MQTT (TCP 1883, optional)
     v
+----------------+
|  MQTT broker   |  e.g. mosquitto / Home Assistant
+----------------+
```

## Identity

| Field | Value |
|-------|-------|
| `deviceId` | `dev_` + last 6 hex chars of the MAC address, e.g. `dev_A1B2C3` |

The MQTT client id is `xy-<deviceId>`. Broker credentials (username/password)
are optional; empty credentials connect anonymously.

## Topics

| Topic | QoS | Retained | Direction | Payload |
|-------|-----|----------|-----------|---------|
| `xysk/<deviceId>/info` | 1 | yes | device → broker | device identity |
| `xysk/<deviceId>/status` | 1 | yes | device → broker | current PSU status snapshot |
| `xysk/<deviceId>/online` | 1 | yes (LWT) | device → broker | `1` alive, `0` died |
| `xysk/<deviceId>/command` | 1 | no | anyone → device | one command object |
| `xysk/<deviceId>/response` | 1 | no | device → anyone | reply to a command |

### `/info` (retained, published once on connect)

```json
{ "deviceId": "dev_A1B2C3", "name": "XY-SK150S", "model": "XY-SK150S" }
```

### `/online` (LWT)

The device connects with `will.topic = xysk/<deviceId>/online`,
`will.retain = true`, `will.qos = 1`, `will.payload = "0"`. On connect it
publishes retained `1`. A crash/drop publishes retained `0`.

## Status schema (`/status`)

Published retained **only when the value changed** (diff), roughly every 1 s.
Field `action` = `statusResponse` keeps wire parity with the panel.

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
  "hostType": 14874, "wifiConfig": 0, "wifiStatus": 4, "ipv4": 3232235906,
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
- `wifiStatus`: WiFi-module host block `0x0032` (0=NULL, 1=TOUCH, 2=AP, 3=ROUT, 4=SERVER).

## Commands (`/command`)

Publish **one command object per message**. The device replies on `/response`
with `{"action": "<action>Response", ...}`.

| `action` | Required fields | Reply |
|----------|-----------------|-------|
| `ping` | — | `pong` |
| `getData`, `getStatus` | — | full status JSON (like `/status`, non-retained) |
| `getTimeZone` | — | `timeZoneData` with `timeZones` array + `current` |
| `getWifiStatus` | — | `wifiStatusResponse` with `wifiStatus` = JSON string (SSID/IP/RSSI/MAC of the ESP32 radio) |
| `addWifiNetwork` | `ssid`, `password` | saves to NVS, reconnects if possible, replies `addWifiNetworkResponse` |
| `setMqttConfig` | `host`, `port`, `user`, `pass` | `setMqttConfigResponse` (use `mqtt on/off` / panel to enable) |
| `powerOutput` | `enable` bool | `powerOutputResponse` |
| `setVoltage` | `voltage` (V) | `setVoltageResponse` |
| `setCurrent` | `current` (A) | `setCurrentResponse` |
| `setPower` | `power` (W) | `setPowerResponse` |
| `setKeyLock` | `lock` bool | `setKeyLockResponse` |
| `setTimeZone` | `index` (int into TIME_ZONES) | `setTimeZoneResponse` |
| `restart` | — | reboots the ESP32 |
| `setProtection` | one or more of `lvp`,`ovp`,`ocp`,`opp`,`otp` | `<key>Response` |
| `setBacklight` | `level` | `setBacklightResponse` |
| `setSleepTimeout` | `minutes` | `setSleepTimeoutResponse` |
| `setSlaveAddress` | `address` | `setSlaveAddressResponse` |
| `setBaudRate` | `code` | `setBaudRateResponse` |
| `setTempUnit` | `celsius` bool | `setTempUnitResponse` |
| `setBeeper` | `enabled` bool | `setBeeperResponse` |
| `setMppt` | `enabled` bool, `threshold` | `setMpptResponse` |
| `setBatteryCutoff` | `current` (A) | `setBatteryCutoffResponse` |
| `setBch` | `enabled` bool, `threshold` | `setBchResponse` |
| `setBtfEnable` | `enabled` bool | `setBtfEnableResponse` |
| `setBtfCutoff` | `current` (A) | `setBtfCutoffResponse` |
| `setClof` | `enabled` bool | `setClofResponse` |
| `setPowerOnInit` | `enabled` bool | `setPowerOnInitResponse` |
| `setCpMode` | `enabled` bool | `setCpModeResponse` |
| `setOhp` | `hours`, `minutes` | `setOhpResponse` |
| `setOha` | `ampHours` (Ah) | `setOhaResponse` |
| `setOwh` | `wattHours` (Wh) | `setOwhResponse` |
| `setMemoryGroup` | `group` (0-9) | `setMemoryGroupResponse` |
| `getMemoryGroup` | `group` (0-9) | `memoryGroupData` (profile JSON) |
| `saveMemoryGroup` | `group` (0-9) | `saveMemoryGroupResponse` |
| `psuReset` | — | `psuResetResponse` |
| `clearProtection` | — | `clearProtectionResponse` |

Unknown actions produce no reply. Values are float unless noted.

## Local panel

The device also serves the full panel:

- HTTP `:80` — static client (`/`, `/main.js`, `/style.css`) and one-off config:
  `POST /api/wifi` (add/connect WiFi), `POST /api/mqtt` (`{host,port,user,pass,enable}`),
  `GET|POST /api/mode` (read / exit AP mode).
- WebSocket `ws://<host>/ws` — server pushes the status JSON (same schema as
  `/status`, `action:"statusResponse"`, plus `ssid`/`ip`/`rssi`/`mqttHost`/
  `mqttPort`/`mqttUser`/`mqttEnabled`/`mqttConnected`/`mode`) every ~1 s while a
  client is connected, and replies to commands sent as plain `{action,...}`
  objects.

## MQTT settings on the device

Stored in NVS namespace `mqttc` (keys `host`, `port`, `user`, `pass`, `name`,
`enable`). Editable via the serial console (`mqtt set <host> [port] [user]
[pass]`, `mqtt on/off`) or the panel. There is **no TLS** on the device→broker
link; `user`/`pass` are passed through to the broker if it requires
authentication.
