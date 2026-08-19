# XY-SK120 Web UI Guide

The web interface is a self-contained single-page app served from the ESP32's LittleFS. It requires no build step, no framework and no network access — everything lives in three files under `data/`:

| File | Purpose |
|------|---------|
| `data/index.html` | Single HTML page with all three tabs |
| `data/main.js` | All logic: WebSocket, rendering, actions, routing, multi-device |
| `data/style.css` | All styles (hand-written, no framework) |

## Table of Contents

- [Architecture](#architecture)
- [HTTP Endpoints](#http-endpoints)
- [WebSocket Protocol](#websocket-protocol)
  - [Status push (`statusResponse`)](#status-push-statusresponse)
  - [Client actions](#client-actions)
  - [Handling responses](#handling-responses)
- [Tabs & Routing](#tabs--routing)
- [Multi-Device Support](#multi-device-support)
- [Adding a New Control](#adding-a-new-control)

## Architecture

The UI follows one simple rule: **the server pushes live data, the client sends commands.**

1. The ESP32 polls the PSU every ~250 ms (`pollAndBroadcastPSUStatus()` in `web_interface.cpp`) and broadcasts a `statusResponse` JSON frame to every connected WebSocket client **only when the status actually changed**.
2. `main.js` keeps one `handleMessage()` switch that renders any incoming action.
3. User actions send small JSON commands over the same WebSocket; the server replies with `<action>Response` frames and the loop poller then pushes the refreshed state to everyone.

There is no `fetch()` polling loop in the client — the ESP32 is the source of truth and the only writer to the page.

## HTTP Endpoints

Served by `setupWebServer()` in `src/web_interface/web_interface.cpp`:

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | `index.html` |
| GET | `/style.css`, `/main.js` | static assets |
| GET | `/api/data` | current PSU output status (voltage/current/power/output on) |
| GET | `/api/config` | device configuration (name, Modbus id, baud, parity, …) |
| POST | `/api/config` | update + persist configuration |
| GET | `/api/timezone` | `{ timeZones: [...], current: {...} }` |
| POST | `/api/timezone` | body `{ "index": <n> }` → set timezone |
| GET | `/health`, `/ping` | plain-text health checks |
| WS | `/ws` | WebSocket endpoint for everything else |

Static files are served via `handleFileRead()` for any path that exists on LittleFS.

## WebSocket Protocol

All messages are JSON. The client connects to `ws://<host>/ws` (see `connect()` in `main.js`) and sends commands through the single `send(obj)` helper. The server dispatches on `action`.

### Status push (`statusResponse`)

Sent automatically on connect and whenever PSU state changes. Field summary:

| Field | Description |
|-------|-------------|
| `connected` | false if the PSU did not respond |
| `voltage`, `current`, `power` | live output values |
| `voltageSet`, `currentSet`, `powerSet` | working setpoints |
| `operatingMode` | `CV`, `CC`, `CP` |
| `outputEnabled` | output on/off |
| `protectionStatus` | 0 = normal, >0 = protection code |
| `inputVoltage` | Uin |
| `ampHours`, `wattHours`, `outputTime` | energy counters |
| `internalTemp`, `externalTemp`, `tempCelsius` | temperatures |
| `model`, `version` | device identity |
| `keyLockEnabled`, `beeper`, `backlight`, `sleepTimeout` | device state |
| `slaveAddress`, `baudRateCode`, `memoryGroup` | Modbus + group |
| `mpptEnabled`, `mpptThreshold`, `batteryCutoff` | MPPT / battery |
| `cpModeEnabled`, `outputOnAtStartup`, `etp` | misc settings |
| `bchEnabled`, `bchThreshold`, `btfEnabled`, `btfCutoff`, `clofEnabled` | battery charging/output-off settings |
| `hostType`, `wifiConfig`, `wifiStatus`, `ipv4` | PSU WiFi module host block |
| `lvp`, `ovp`, `ocp`, `opp`, `otp` | protection thresholds |
| `ohpHours`, `ohpMinutes`, `overAmpHours`, `overWattHours` | extended protection |
| `deviceName` | configured device name |

### Client actions

**Output & setpoints**

| Action | Payload | Response |
|--------|---------|----------|
| `powerOutput` | `{ enable: bool }` | `powerOutputResponse` |
| `setVoltage` | `{ voltage }` | `setVoltageResponse` |
| `setCurrent` | `{ current }` | `setCurrentResponse` |
| `setPower` | `{ power }` | `setPowerResponse` |
| `setConstantVoltage` / `setConstantCurrent` / `setConstantPower` | value | matching `...Response` |
| `setConstantPowerMode` | `{ enable }` | `constantPowerModeResponse` |
| `setKeyLock` | `{ lock }` | `keyLockResponse` / `setKeyLockResponse` |
| `getKeyLockStatus` | – | `keyLockStatusResponse` |
| `getStatus` / `getData` | – | `statusResponse` |
| `getOperatingMode` | – | `operatingModeResponse` |

**Protection & settings** (all handled by `handleDeviceSettingAction`, respond with `<action>Response`)

| Action | Payload |
|--------|---------|
| `setProtection` | `{ key: "lvp"\|"ovp"\|"ocp"\|"opp"\|"otp", value }` |
| `setBacklight` | `{ level }` |
| `setSleepTimeout` | `{ minutes }` |
| `setSlaveAddress` | `{ address }` |
| `setBaudRate` | `{ code }` |
| `setTempUnit` | `{ celsius }` |
| `setBeeper` | `{ enabled }` |
| `setMppt` | `{ enable, threshold }` |
| `setBatteryCutoff` | `{ current }` |
| `setBch` | `{ enabled, threshold }` |
| `setBtfEnable` | `{ enabled }` |
| `setBtfCutoff` | `{ current }` |
| `setClof` | `{ enabled }` |
| `setPowerOnInit` | `{ enabled }` |
| `setCpMode` | `{ enabled }` |
| `setOhp` | `{ hours, minutes }` |
| `setOha` | `{ ampHours }` |
| `setOwh` | `{ wattHours }` |
| `setMemoryGroup` | `{ group }` (recall M0–M9) |
| `getMemoryGroup` | `{ group }` → `memoryGroupData` with the full profile |
| `saveMemoryGroup` | `{ group, voltageSet, currentSet, lvp, ovp, ocp, opp, ohpHours, ohpMinutes, overAmpHours, overWattHours, otp, etp, outputOnAtStartup }` |
| `psuReset` | – (factory defaults) |
| `clearProtection` | – |

**WiFi & system**

| Action | Payload | Response |
|--------|---------|----------|
| `getWifiStatus` | – | `wifiStatusResponse` |
| `addWifiNetwork` | `{ ssid, password, priority }` | `addWifiNetworkResponse` |
| `saveWifiCredentials` | `{ ssid, password }` | `saveWifiCredentialsResponse` |
| `loadWifiCredentials` | – | `loadWifiCredentialsResponse` |
| `removeWifiNetwork` | `{ index, ssid?, deleteMode? }` | `removeWifiNetworkResponse` |
| `updateWifiPriority` | `{ index, priority }` | `updateWifiPriorityResponse` |
| `connectWifi` | `{ ssid, password }` | `connectWifiResponse` |
| `resetWifi` | – | `resetWifiResponse` |
| `getTimeZones` | – | `timeZonesResponse` (`timeZones`, `current`) |
| `setTimeZone` | `{ index }` | `setTimeZoneResponse` |
| `restart` | – | `restartResponse`, then ESP restarts |
| `ping` | – | `pong` |

### Handling responses

All incoming frames go through `handleMessage(raw)` in `main.js`, which switches on `msg.action`. Conventions:

- Every command response ends in `Response` and carries `success: bool`.
- `statusResponse` is handled by `renderStatus()` which fills every field that has a matching DOM element (it also ignores elements the user is currently editing — see "dirty" tracking below).
- Errors surface as toasts: any `<action>Response` with `success === false` and an `error` string shows a toast automatically.

## Tabs & Routing

The app is a single page with three `<tab>` panels and a bottom nav bar. Routing is hash-based (`#main`, `#prot`, `#cfg`):

- `switchTab(tab)` shows the matching `#page-<tab>` element and updates the nav.
- `initTab()` reads `location.hash` on load and on every `hashchange` event.

Tab structure in `index.html`:

| Tab | id | Content |
|-----|----|---------|
| Главная | `#page-main` | live V/A/W display, mode + protection slot, setpoint inputs, memory-group recall, energy/temperature table |
| Профили | `#page-prot` | full profile editor for the selected memory group (save / apply / cancel) |
| Настройки | `#page-cfg` | device settings, PSU WiFi-module status, ESP32 WiFi, servers list, ESP restart |

## Multi-Device Support

`main.js` keeps a server list in `localStorage`:

- `xypsu_servers` — array of `{ name, ip }`
- `xypsu_current` — the active server IP

The header `<select id="deviceSelect">` switches servers, and the "Серверы" card in the settings tab adds/removes them. On switch, `resetUi()` clears all live values and `connect()` opens a new WebSocket to the selected host. The current host is always prepended to the list, so the page works standalone.

## Adding a New Control

To expose a new PSU register in the UI:

1. **Backend** — add an `else if` branch in `handleWebSocketMessage()` (or `handleDeviceSettingAction()` for settings/protection) in `src/web_interface/web_interface.cpp`. Read/write the register under `lockModbus() / unlockModbus()` and reply with a `<name>Response` frame.
2. **UI** — if it should live-update, add the field to `renderStatus()` in `data/main.js`. Status fields are cheap to add: the frame already carries everything `readPSUStatusBatched()` reads.
3. **Send** — add a send call via `send({ action: "...", ... })`, then handle the reply in `handleMessage()`.

### Conventions

- Always acquire the Modbus mutex (`lockModbus()`/`unlockModbus()`) around any PSU bus access — the status poller in `loop()` runs concurrently.
- Keep `index.html` dependency-free and self-contained; no CDNs, no external fonts.
- After changing `data/`, rebuild with `pio run -t uploadfs` and hard-refresh the browser.
- The server broadcasts status only when the value changed — don't rely on responses alone if a control needs immediate visual feedback.
