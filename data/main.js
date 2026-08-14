const $ = (id) => document.getElementById(id);
const fmt = (v, d = 2) => (isNaN(v) ? "--" : Number(v).toFixed(d));

// ---- Multi-instance: several PSU servers saved in localStorage ----
const SERVERS_KEY = "xypsu_servers";
let servers = [];
let currentIp = location.host;

function loadServers() {
  try {
    const raw = localStorage.getItem(SERVERS_KEY);
    servers = raw ? JSON.parse(raw) : [];
  } catch { servers = []; }
  if (!Array.isArray(servers)) servers = [];
  if (!servers.length) servers = [{ name: "PSU", ip: location.host }];
  if (!servers.some((s) => s.ip === location.host)) servers.unshift({ name: "PSU", ip: location.host });
  saveServers();
}

function saveServers() {
  try { localStorage.setItem(SERVERS_KEY, JSON.stringify(servers)); } catch {}
}

function renderDeviceSelect() {
  const sel = $("deviceSelect");
  sel.innerHTML = "";
  servers.forEach((s, i) => {
    const opt = document.createElement("option");
    opt.value = s.ip;
    opt.textContent = `${s.name} (${s.ip})`;
    if (s.ip === currentIp) opt.selected = true;
    sel.appendChild(opt);
  });
  $("deviceName").textContent = servers.find((s) => s.ip === currentIp)?.name || "PSU";
}

function switchDevice(ip) {
  if (!ip || ip === currentIp) return;
  currentIp = ip;
  renderDeviceSelect();
  toast(`Сервер ${ip}`);
  setConn(false);
  connect();
}

function addDevice(name, ip) {
  ip = (ip || "").trim().replace(/^https?:\/\//, "").replace(/\/.*$/, "");
  name = (name || "").trim() || ip || "PSU";
  if (!ip) { toast("Введите IP"); return; }
  servers.push({ name, ip });
  saveServers();
  switchDevice(ip);
}

function removeDevice(ip) {
  servers = servers.filter((s) => s.ip !== ip);
  saveServers();
  if (!servers.length) servers = [{ name: "PSU", ip: location.host }];
  renderDeviceSelect();
  const sel = $("deviceSelect");
  currentIp = sel.value || servers[0].ip;
  connect();
}

// ---- WebSocket: the server pushes fresh status on its own ----
let ws = null;
let connected = false;

function connect() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  if (ws) { ws.onclose = null; try { ws.close(); } catch {} }
  ws = new WebSocket(`${proto}://${currentIp}/ws`);
  ws.onopen = () => {
    connected = true;
    setConn(false);
    loadWifiStatus();
  };
  ws.onclose = () => {
    connected = false;
    setConn(true);
    setTimeout(connect, 1000);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = (e) => handleMessage(e.data);
}

function send(obj) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(obj));
  }
}

// ---- Rendering ----
function setConn(offline) {
  const dot = $("conn");
  dot.className = offline ? "dot dot-off" : "dot dot-on";
  $("connText").textContent = offline ? "offline" : "online";
}

function fmtTime(sec) {
  if (isNaN(sec)) return "--";
  sec = Math.max(0, Math.floor(Number(sec)));
  const h = String(Math.floor(sec / 3600)).padStart(2, "0");
  const m = String(Math.floor((sec % 3600) / 60)).padStart(2, "0");
  const s = String(sec % 60).padStart(2, "0");
  return `${h}:${m}:${s}`;
}

// Only touch an input if the user isn't currently editing it
const editable = (id) => !document.activeElement || document.activeElement.id !== id;

// Protection codes as in the XY-SK150 protocol
const PROT_LABELS = {
  1: "OVP", 2: "OCP", 3: "OPP", 4: "LVP", 5: "OAH", 6: "OHP",
  7: "OTP", 8: "OEP", 9: "OWH", 10: "ICP", 11: "ETP"
};
const protectionText = (code) => PROT_LABELS[code] || `Prot#${code}`;

function renderStatus(s) {
  if (!s) return;
  // Output toggle shows the CURRENT state (state, not action)
  const on = !!s.outputEnabled;
  $("voltage").textContent = fmt(s.voltage);
  $("current").textContent = fmt(s.current, 3);
  $("power").textContent = fmt(s.power);

  if (s.deviceName) $("deviceName").textContent = s.deviceName;

  // 4th slot: CV/CC/CW mode, or the protection error (red) when triggered
  const md = $("mode");
  const code = Number(s.protectionStatus);
  if (!isNaN(code) && code > 0) {
    md.textContent = protectionText(code);
    md.className = "err";
  } else {
    const m = String(s.operatingMode || "--");
    md.textContent = m === "CP" ? "CW" : m;
    md.className = (m === "CV") ? "cv" : (m === "CC") ? "cc" : (m === "CP" ? "cp" : "");
  }

  // Protection status is shown in the 4th slot; reset button only when triggered
  $("clearProtBtn").classList.toggle("hidden", isNaN(code) || code === 0);

  // Energy & temperatures
  $("ampHours").textContent = `${fmt(s.ampHours, 3)} Ah`;
  $("wattHours").textContent = `${fmt(s.wattHours, 3)} Wh`;
  $("outputTime").textContent = fmtTime(s.outputTime);
  $("inputVoltage").textContent = `${fmt(s.inputVoltage)} V`;
  $("internalTemp").textContent = `${fmt(s.internalTemp, 1)} ${s.tempCelsius ? "°C" : "°F"}`;
  $("externalTemp").textContent = `${fmt(s.externalTemp, 1)} ${s.tempCelsius ? "°C" : "°F"}`;

  // Protection inputs
  if (editable("pOvp")) $("pOvp").value = fmt(s.ovp);
  if (editable("pOcp")) $("pOcp").value = fmt(s.ocp, 3);
  if (editable("pOpp")) $("pOpp").value = fmt(s.opp, 1);
  if (editable("pLvp")) $("pLvp").value = fmt(s.lvp);
  if (editable("pOtp")) $("pOtp").value = fmt(s.otp, 1);
  if (editable("pOhpH")) $("pOhpH").value = s.ohpHours != null ? s.ohpHours : "";
  if (editable("pOhpM")) $("pOhpM").value = s.ohpMinutes != null ? s.ohpMinutes : "";
  if (editable("pOha")) $("pOha").value = fmt(s.overAmpHours, 3);
  if (editable("pOwh")) $("pOwh").value = fmt(s.overWattHours, 1);
  if (editable("pIni")) $("pIni").checked = !!s.outputOnAtStartup;

  // Settings inputs
  if (editable("cBacklight")) $("cBacklight").value = s.backlight != null ? s.backlight : "";
  if (editable("cSleep")) $("cSleep").value = s.sleepTimeout != null ? s.sleepTimeout : "";
  if (editable("cSlave")) $("cSlave").value = s.slaveAddress != null ? s.slaveAddress : "";
  if (editable("cBaud")) $("cBaud").value = s.baudRateCode != null ? String(s.baudRateCode) : "6";
  if (editable("cTempUnit")) $("cTempUnit").value = s.tempCelsius ? "c" : "f";
  if (editable("cBeeper")) $("cBeeper").checked = !!s.beeper;
  if (editable("cMppt")) $("cMppt").checked = !!s.mpptEnabled;
  if (editable("cMpptThr")) $("cMpptThr").value = s.mpptThreshold != null ? fmt(s.mpptThreshold) : "";
  if (editable("cBtf")) $("cBtf").value = fmt(s.batteryCutoff, 3);
  if (editable("cMemGroup")) $("cMemGroup").value = s.memoryGroup != null ? String(s.memoryGroup) : "0";

  const btn = $("outBtn");
  btn.textContent = on ? "ВКЛ" : "ВЫКЛ";
  btn.className = "btn " + (on ? "btn-success" : "btn-danger");

  $("keyLock").textContent = s.keyLockEnabled ? "🔒" : "🔓";
  $("keyLock").className = "btn btn-sm " + (s.keyLockEnabled ? "btn-warning" : "btn-ghost");
  $("keyLock").title = s.keyLockEnabled ? "Снять блокировку" : "Заблокировать";

  if (editable("vIn")) $("vIn").value = fmt(s.voltageSet);
  if (editable("iIn")) $("iIn").value = fmt(s.currentSet, 3);

  $("model").textContent = `Model ${s.model} / v${s.version}`;
}

function renderWifi(s) {
  $("wifiSsid").textContent = s.ssid || "--";
  $("wifiIp").textContent = s.ip || "--";
  $("wifiRssi").textContent = s.rssi != null ? `${s.rssi} dBm` : "--";
}

// ---- Tabs ----
function switchTab(tab) {
  document.querySelectorAll(".tabpage").forEach((p) => p.classList.add("hidden"));
  $("page-" + tab).classList.remove("hidden");
  document.querySelectorAll("#tabbar button").forEach((b) => {
    b.classList.toggle("active", b.dataset.page === tab);
  });
}

// ---- Message handling ----
function handleMessage(raw) {
  let msg;
  try { msg = JSON.parse(raw); } catch { return; }
  switch (msg.action) {
    case "statusResponse":
      renderStatus(msg);
      break;
    case "wifiStatusResponse":
      renderWifi(msg.wifiStatus ? JSON.parse(msg.wifiStatus) : msg);
      break;
    case "powerOutputResponse":
    case "clearProtectionResponse":
      if (msg.error) toast(msg.error);
      else if (msg.action === "clearProtectionResponse") toast("Защита сброшена");
      break;
    case "setVoltageResponse":
    case "setCurrentResponse":
      if (msg.error) toast(msg.error);
      break;
    case "setKeyLockResponse":
    case "keyLockResponse":
      if (msg.locked != null) {
        $("keyLock").textContent = msg.locked ? "🔒" : "🔓";
        $("keyLock").className = "btn btn-sm " + (msg.locked ? "btn-warning" : "btn-ghost");
        $("keyLock").title = msg.locked ? "Снять блокировку" : "Заблокировать";
      }
      break;
    case "connectWifiResponse":
      if (msg.success) { toast(`Подключено к ${msg.ssid}`); loadWifiStatus(); }
      else toast(msg.error || "Не удалось подключиться");
      break;
    case "addWifiNetworkResponse":
      toast(msg.success ? "Сеть сохранена" : "Ошибка сохранения сети");
      break;
  }
  // Toast failures from device-setting responses
  if (msg.action && msg.action.endsWith("Response") && msg.success === false && msg.error) {
    toast(msg.error);
  }
}

function toast(t) {
  const el = $("toast");
  el.textContent = t;
  el.classList.add("show");
  clearTimeout(el._t);
  el._t = setTimeout(() => el.classList.remove("show"), 2500);
}

// ---- Actions ----
function toggleOutput() {
  const on = $("outBtn").textContent === "ВКЛ";
  send({ action: "powerOutput", enable: !on });
}

function setVoltage() {
  const v = parseFloat($("vIn").value);
  if (isNaN(v)) return;
  send({ action: "setVoltage", voltage: v });
}

function setCurrent() {
  const i = parseFloat($("iIn").value);
  if (isNaN(i)) return;
  send({ action: "setCurrent", current: i });
}

function toggleKeyLock() {
  send({ action: "setKeyLock", lock: !($("keyLock").textContent === "🔒") });
}

function loadWifiStatus() {
  send({ action: "getWifiStatus" });
}

function addNetwork() {
  const ssid = $("wifiNewSsid").value.trim();
  const password = $("wifiNewPass").value;
  if (!ssid) { toast("Введите SSID"); return; }
  send({ action: "addWifiNetwork", ssid, password, priority: 1 });
  $("wifiNewPass").value = "";
}

// Map table-row [data-action] buttons to their inputs and build the command
function applyRow(action) {
  switch (action) {
    case "ovp": send({ action: "setProtection", key: "ovp", value: parseFloat($("pOvp").value) }); break;
    case "ocp": send({ action: "setProtection", key: "ocp", value: parseFloat($("pOcp").value) }); break;
    case "opp": send({ action: "setProtection", key: "opp", value: parseFloat($("pOpp").value) }); break;
    case "lvp": send({ action: "setProtection", key: "lvp", value: parseFloat($("pLvp").value) }); break;
    case "otp": send({ action: "setProtection", key: "otp", value: parseFloat($("pOtp").value) }); break;
    case "ohp": send({ action: "setOhp", hours: parseInt($("pOhpH").value) || 0, minutes: parseInt($("pOhpM").value) || 0 }); break;
    case "oha": send({ action: "setOha", ampHours: parseFloat($("pOha").value) }); break;
    case "owh": send({ action: "setOwh", wattHours: parseFloat($("pOwh").value) }); break;
    case "ini": send({ action: "setPowerOnInit", enabled: $("pIni").checked }); break;
    case "backlight": send({ action: "setBacklight", level: parseInt($("cBacklight").value) }); break;
    case "sleep": send({ action: "setSleepTimeout", minutes: parseInt($("cSleep").value) }); break;
    case "slave": send({ action: "setSlaveAddress", address: parseInt($("cSlave").value) }); break;
    case "baud": send({ action: "setBaudRate", code: parseInt($("cBaud").value) }); break;
    case "tempunit": send({ action: "setTempUnit", celsius: $("cTempUnit").value === "c" }); break;
    case "beeper": send({ action: "setBeeper", enabled: $("cBeeper").checked }); break;
    case "mppt": send({ action: "setMppt", enable: $("cMppt").checked, threshold: parseFloat($("cMpptThr").value) || 0.8 }); break;
    case "btf": send({ action: "setBatteryCutoff", current: parseFloat($("cBtf").value) }); break;
    case "memgroup": send({ action: "setMemoryGroup", group: parseInt($("cMemGroup").value) }); break;
  }
}

// ---- Boot ----
document.addEventListener("DOMContentLoaded", () => {
  loadServers();
  renderDeviceSelect();

  $("outBtn").addEventListener("click", toggleOutput);
  $("keyLock").addEventListener("click", toggleKeyLock);
  $("wifiAddBtn").addEventListener("click", addNetwork);
  $("wifiRefresh").addEventListener("click", loadWifiStatus);
  $("psuResetBtn").addEventListener("click", () => {
    if (confirm("Сбросить БП к заводским настройкам?")) send({ action: "psuReset" });
  });
  $("clearProtBtn").addEventListener("click", () => {
    if (confirm("Сбросить сработавшую защиту?")) send({ action: "clearProtection" });
  });
  $("restartBtn").addEventListener("click", () => {
    if (confirm("Перезапустить ESP32?")) send({ action: "restart" });
  });

  // Tabs
  document.querySelectorAll("#tabbar button").forEach((btn) => {
    btn.addEventListener("click", () => switchTab(btn.dataset.page));
  });

  // Multi-instance controls
  $("deviceSelect").addEventListener("change", (e) => switchDevice(e.target.value));
  $("devAddBtn").addEventListener("click", () => {
    $("addForm").classList.toggle("hidden");
    if (!$("addForm").classList.contains("hidden")) $("newName").focus();
  });
  $("addCancel").addEventListener("click", () => $("addForm").classList.add("hidden"));
  $("addOk").addEventListener("click", () => {
    addDevice($("newName").value, $("newIp").value);
    $("newName").value = "";
    $("newIp").value = "";
    $("addForm").classList.add("hidden");
  });
  $("devDelBtn").addEventListener("click", () => {
    if (confirm("Удалить этот сервер?")) removeDevice(currentIp);
  });

  // All [data-action] "ok" buttons
  document.querySelectorAll(".editable .btn[data-action]").forEach((btn) => {
    btn.addEventListener("click", () => applyRow(btn.dataset.action));
  });

  $("vIn").addEventListener("keydown", (e) => { if (e.key === "Enter") setVoltage(); });
  $("iIn").addEventListener("keydown", (e) => { if (e.key === "Enter") setCurrent(); });

  connect();
  setInterval(loadWifiStatus, 30000);
});