# XY-SK120 / XY-SK150 Power Supply Control

![Demo](documentation/demo.jpg)

![Demo](documentation/demo.gif)

Control an XY-SK120 / XY-SK150(S) power supply (and compatible models) over Modbus RTU using a Seeed XIAO ESP32S3 or ESP32C3. The firmware provides a web UI, a serial monitor interface, RTC/weather sync to the PSU screensaver, and a full set of register-debugging tools.

## Features

- **Web UI** (HTTP + WebSocket) served directly from the ESP32:
  - Live dashboard: output voltage / current / power, mode (CV/CC/CW), temperatures, energy counters
  - Output on/off, key lock, protection clear
  - Profile (memory group M0–M9) editor: read, save and apply full profiles
  - Device settings: backlight, sleep, slave address, baud rate, temp unit, beeper, MPPT, CP mode, BCH/BTF/CLOF battery settings, timezone
  - WiFi module status (PSU host block) + WiFi network management
  - Multi-device support: switch between several PSU servers saved in the browser
- **RTC / weather sync** — every ~10 s the ESP32 pushes Unix time plus a 3-day Open-Meteo forecast into the PSU's screensaver (register block `0x0200–0x0214`, same frame the OEM XY-WFPOW module sends)
- **Serial monitor control interface** — menus for basic control, measurement, protection, settings, memory groups, WiFi and register debugging
- **Register debugging** — scan/compare/sniff tools to discover undocumented registers (see below)
- ESP32S3 and ESP32C3 targets

## Hardware Setup

- Connect the XIAO ESP32 to the XY-SK120/SK150 using the TTL interface:
  - XIAO TX pin → XY-SK120 RX pin
  - XIAO RX pin → XY-SK120 TX pin
  - XIAO GND → XY-SK120 GND
- First boot opens a WiFiManager AP (`XY-SK150-Setup`) to configure your network; holding the WiFi reset pin (D0 on ESP32S3, GPIO9 on ESP32C3) to ground for 3 s resets the WiFi settings.

## Web Interface

After connecting, open `http://<esp-ip>/` in a browser. The UI is a single page with three tabs: **Главная** (live dashboard), **Профили** (memory group profiles) and **Настройки** (device/WiFi settings).

Live data is pushed by the server over WebSocket (`/ws`) — the client never polls. See [README_WEB_UI.md](README_WEB_UI.md) for the full endpoint/action reference.

### HTTP endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Web UI (`index.html`) |
| GET | `/api/data` | Current PSU status (JSON) |
| GET/POST | `/api/config` | Device configuration (name, Modbus ID, baud, etc.) |
| GET/POST | `/api/timezone` | Available time zones + current one / set timezone |
| GET | `/health`, `/ping` | Health checks |
| WS | `/ws` | WebSocket for live status + all control commands |

### WebSocket actions (client → server)

Control: `powerOutput`, `setVoltage`, `setCurrent`, `setPower`, `setConstantVoltage`, `setConstantCurrent`, `setConstantPower`, `setConstantPowerMode`, `setKeyLock`, `getKeyLockStatus`, `getStatus`/`getData`, `getOperatingMode`.

Device settings & protection: `setProtection` (key: `lvp`/`ovp`/`ocp`/`opp`/`otp`), `setBacklight`, `setSleepTimeout`, `setSlaveAddress`, `setBaudRate`, `setTempUnit`, `setBeeper`, `setMppt`, `setBatteryCutoff`, `setBch`, `setBtfEnable`, `setBtfCutoff`, `setClof`, `setPowerOnInit`, `setCpMode`, `setOhp`, `setOha`, `setOwh`, `setMemoryGroup`, `getMemoryGroup`, `saveMemoryGroup`, `psuReset`, `clearProtection`.

WiFi & system: `getWifiStatus`, `addWifiNetwork`, `loadWifiCredentials`, `saveWifiCredentials`, `removeWifiNetwork`, `updateWifiPriority`, `connectWifi`, `resetWifi`, `getTimeZones`, `setTimeZone`, `restart`.

## RTC & Weather Sync

The PSU's screensaver can display the current time and a 3-day weather forecast, but it only shows them when a Sinilink XY-WFPOW WiFi module (acting as Modbus master) feeds it. This firmware does the same:

- **Time**: NTP sync + timezone (selectable in the UI, 14 zones from UTC-8 to UTC+12), pushed as local Unix time to `0x0200–0x0201` every ~10 s.
- **Weather**: a background task fetches current conditions + 3-day forecast from Open-Meteo (default location is Tyumen, RU; override via `setWeatherMeteoConfig(lat, lon)`), maps the WMO codes onto the PSU's screensaver icon set and writes the block `0x0203–0x0214`.
- The whole 21-register block `0x0200–0x0214` is written with one Modbus function 0x10 request, mirroring the OEM module.

## Serial Monitor Commands

Enter the menu number (`1`–`7`) or type a command. Top-level commands: `status`, `prot`, `config`, `info`, `help`.

### 1. Basic Control

- `on` / `off` — turn output ON/OFF
- `set V I` — set voltage (V) and current (I)
- `lock` / `unlock` — key lock
- `mem N` — show memory group N (0–9); `call N` — recall group; `save2mem N` — save current settings to group
- `setmem N param value` — set a single parameter in group N (`v`, `i`, `p`, `ovp`, `ocp`, `opp`, `oah`, `owh`, `uvp`, `ucp`)

### 2. Measurement & 3. Protection

`status`-style readouts, plus protection setup (LVP/OVP/OCP/OPP/OTP/OHP/OAH/OWH, power-on state) and protection clear.

### 4. Settings

`beeper`, `brightness`, `tempunit`, `sleep`, `slave`, `baud`, `rxpin`/`txpin`, `mppt`/`mpptthr`, `default` (factory reset), `save`, `saveconfig`, `showsettings`.

### 7. WiFi Settings

`scan`, `connect "ssid" "pass"`, `ap`, `exit`, `status`, `ip`, `savedwifi`, `addwifi "ssid" "pass" [priority]`, `syncwifi`, `repairwifi`.

### 5. Debug (Register R/W)

Direct register access for discovering undocumented features:

- `read addr` / `readhex addr` — read a holding register
- `write addr value` / `writehex addr value` — write one register
- `mwrite reg1 val1 reg2 val2 ...` — write several registers
- `wblock start count v1 v2 ...` — write a contiguous block in one FC16 request
- `raw func addr count` — raw read of any register block
- `read4 addr` / `read4hex addr` — read an **input register** (FC04)
- `scan start end` — scan holding registers; `scan4 start end` — scan input registers
- `compare start end` — two-pass discovery: snapshot, then detect which registers change when you change a setting on the device
- `sniff [seconds]` — non-blocking bus sniffer: capture unsolicited frames the PSU sends to its WiFi module
- `writetrial reg start end delay` — try writing a range of values to a register
- `writerange start end value delay` — write a value across a register range
- `weather [code] [tHigh] [tLow] [tNow] [hum]` — manually write today's weather to the RTC block; `weather off/on` — toggle manual mode
- `weatherscan [start] [end]` — auto-cycle weather icon codes every second (film the screensaver to map codes)

Addresses may be decimal or hex (`0x` prefix).

## Register Map

Holding registers (function 0x03), partial list:

| Address | Description | Unit | Scaling | R/W |
|---------|-------------|------|---------|-----|
| 0x0000  | Voltage setting (working) | V | /100 | R/W |
| 0x0001  | Current setting (working) | A | /1000 | R/W |
| 0x0002  | Output voltage | V | /100 | R |
| 0x0003  | Output current | A | /1000 | R |
| 0x0004  | Output power | W | /100 | R |
| 0x0005  | Input voltage | V | /100 | R |
| 0x0006–0x0007 | Amp-hours (low/high) | Ah | ×0.01 | R |
| 0x0008–0x0009 | Watt-hours (low/high) | Wh | ×0.01 | R |
| 0x000A–0x000C | Output time (h/m/s) | s | – | R |
| 0x000D–0x000E | Internal / external temp | °C/°F | /10 | R |
| 0x000F  | Key lock | 0/1 | – | R/W |
| 0x0010  | Protection status (0=normal, 1=OVP, …) | – | – | R/W |
| 0x0011  | CC/CV mode flag | – | – | R |
| 0x0012  | Output on/off | 0/1 | – | R/W |
| 0x0013  | Temperature unit (0=°C, 1=°F) | – | – | R/W |
| 0x0014  | Backlight brightness | – | – | R/W |
| 0x0015  | Sleep timeout | min | – | R/W |
| 0x0016–0x0017 | Model / firmware version | – | – | R |
| 0x0018–0x0019 | Slave address / baudrate code | – | – | R/W |
| 0x001C  | Beeper | 0/1 | – | R/W |
| 0x001D  | Active memory group (M0–M9) | – | – | R/W |
| 0x001F  | MPPT enable | 0/1 | – | R/W |
| 0x0020  | MPPT threshold | – | /100 | R/W |
| 0x0021  | Battery cutoff current (legacy BTF) | A | /1000 | R/W |
| 0x0022–0x0023 | CP mode enable / power set | W | /10 | R/W |
| 0x0029–0x002D | BCH enable/threshold, BTF enable/cutoff, CLOF enable | – | – | R/W |
| 0x0030–0x0034 | WiFi module host block: MASTER (`0x3B3A`), WIFI-CONFIG, WIFI-STATUS, IPV4 high/low | – | – | R/W |
| 0x0050–0x005E | Profile registers: CV/CC set, LVP, OVP, OCP, OPP, OHP, OAH, OWH, OTP, power-on init, ETP | – | – | R/W |
| 0x0050 + N×0x10 | Memory group N profile block (15 registers) | – | – | R/W |
| 0x0200–0x0214 | RTC/weather block (time, status, today + 3-day forecast) | – | – | R/W |

For the complete map see `lib/XY-SKxxx/XY-SKxxx.h` and the docs in `documentation/`.

## Building

This project uses PlatformIO. Build and upload firmware + filesystem:

```
pio run -t upload            # build & upload firmware
pio run -t uploadfs          # build & upload the LittleFS web UI
```

Select the board environment (`seeed_xiao_esp32s3` default, `seeed_xiao_esp32c3` for the C3). Useful custom targets:

```
pio run -t custom_showsize   # firmware size breakdown
pio run -t custom_showpart   # partition table info
```

## Project Layout

- `src/main.cpp` — boot, WiFi stabilization, background tasks (WiFi host keep-alive, weather fetch, RTC/weather sync)
- `src/web_interface/` — HTTP routes + WebSocket command handling
- `src/modbus/` — RTC/weather sync (`rtc_weather.*`) and Open-Meteo client (`weather_api.*`)
- `src/serial_interface/` — serial menu system
- `src/wifi_interface/` — WiFiManager wrapper, credentials storage
- `src/config/` — NVS/LittleFS configuration
- `lib/XY-SKxxx/` — XY-SKxxx Modbus register library
- `data/` — web UI (`index.html`, `main.js`, `style.css`)

## License

[MIT License](LICENSE)
