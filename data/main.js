const $ = (id) => document.getElementById(id);
const fmt = (v, d = 2) => (isNaN(v) ? "--" : Number(v).toFixed(d));

// ---- Multi-instance: several PSU servers saved in localStorage ----
const SERVERS_KEY = "xypsu_servers";
const CURRENT_KEY = "xypsu_current";
let servers = [];
let currentIp = localStorage.getItem(CURRENT_KEY) || location.host;

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
  renderServersList();
}

function renderServersList() {
  const tbody = $("serversList");
  if (!tbody) return;
  tbody.innerHTML = "";
  servers.forEach((s) => {
    const tr = document.createElement("tr");
    const label = document.createElement("td");
    label.textContent = `${s.name} (${s.ip})`;
    const ctrl = document.createElement("td");
    ctrl.className = "ctrl";
    const selectBtn = document.createElement("button");
    selectBtn.className = "btn btn-sm btn-primary";
    selectBtn.textContent = "Выбрать";
    selectBtn.addEventListener("click", () => switchDevice(s.ip));
    const delBtn = document.createElement("button");
    delBtn.className = "btn btn-sm btn-danger";
    delBtn.textContent = "Удалить";
    delBtn.addEventListener("click", () => {
      if (confirm(`Удалить этот сервер? (${s.ip})`)) removeDevice(s.ip);
    });
    ctrl.appendChild(selectBtn);
    ctrl.appendChild(delBtn);
    tr.appendChild(label);
    tr.appendChild(ctrl);
    tbody.appendChild(tr);
  });
}

function switchDevice(ip) {
  if (!ip || ip === currentIp) return;
  currentIp = ip;
  try { localStorage.setItem(CURRENT_KEY, ip); } catch {}
  resetUi();
  renderDeviceSelect();
  toast(`Сервер ${ip}`);
  setConn(false);
  connect();
}

// Clear all live values so nothing from the previous server is shown
function resetUi() {
  protArmed = false;
  outputOn = false;
  viewingGroup = null;
  const pg = $("protMemGroup");
  if (pg) pg.value = "0";
  ["voltage", "current", "power", "mode", "internalTemp",
   "ampHours", "wattHours", "outputTime", "inputVoltage",
   "externalTemp", "model"].forEach((id) => {
    const el = $(id);
    if (el) el.textContent = "--";
  });
  document.querySelectorAll(".energy-temp-in, .energy-temp-ex").forEach((el) => el.textContent = "--");
  const btn = $("outBtn");
  if (btn) { btn.textContent = "ВКЛ"; btn.className = "btn btn-success"; }
}

function addDevice(name, ip) {
  ip = (ip || "").trim().replace(/^https?:\/\//, "").replace(/\/.*$/, "");
  name = (name || "").trim() || ip || "PSU";
  if (!ip) { toast("Введите IP"); return; }
  const existing = servers.find((s) => s.ip === ip);
  if (existing) {
    existing.name = name;
  } else {
    servers.push({ name, ip });
  }
  saveServers();
  switchDevice(ip);
}

function removeDevice(ip) {
  const wasCurrent = ip === currentIp;
  servers = servers.filter((s) => s.ip !== ip);
  saveServers();
  if (!servers.length) servers = [{ name: "PSU", ip: location.host }];
  if (wasCurrent) {
    currentIp = servers[0].ip;
    try { localStorage.setItem(CURRENT_KEY, currentIp); } catch {}
    resetUi();
  }
  renderDeviceSelect();
  if (wasCurrent) {
    setConn(false);
    connect();
  }
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

// True while a protection fault is active - the on/off button acts as reset
let protArmed = false;
// Current output state from the last status push
let outputOn = false;
// Profile (memory group) being previewed/edited in the protection card; null = live values
let viewingGroup = null;
// One of the settings inputs was edited; when true the polled status stops overwriting them
let configDirty = false;
// Last server values of the settings inputs, used to send only what really changed
let lastConfig = {};

function configInputsDirty() {
  configDirty = true;
}

function renderStatus(s) {
  if (!s) return;
  // Output toggle shows the CURRENT state (state, not action)
  outputOn = !!(s.outputEnabled);
  const on = outputOn;
  $("voltage").textContent = fmt(s.voltage) + "V";
  $("current").textContent = fmt(s.current, 3) + "A";
  $("power").textContent = fmt(s.power) + "W";

  if (s.deviceName) $("deviceName").textContent = s.deviceName;

  // 4th slot: CV/CC/CW mode, or the protection error (red) when triggered
  const md = $("mode");
  const code = Number(s.protectionStatus);
  if (!isNaN(code) && code > 0) {
    md.textContent = protectionText(code);
    md.className = "err";
  } else if (!on) {
    md.textContent = "OFF";
    md.className = "off";
  } else {
    const m = String(s.operatingMode || "--");
    md.textContent = m === "CP" ? "CW" : m;
    md.className = (m === "CV") ? "cv" : (m === "CC") ? "cc" : (m === "CP" ? "cp" : "");
  }

  // Protection status is shown in the 4th slot; on/off button becomes "Сброс"
  protArmed = !isNaN(code) && code > 0;
  const btn = $("outBtn");
  if (protArmed) {
    btn.textContent = "Сброс";
    btn.className = "btn btn-danger";
  } else {
    btn.textContent = on ? "ВКЛ" : "ВЫКЛ";
    btn.className = "btn " + (on ? "btn-success" : "btn-danger");
  }

  // Energy & temperatures
  $("ampHours").textContent = `${fmt(s.ampHours, 3)} Ah`;
  $("wattHours").textContent = `${fmt(s.wattHours, 3)} Wh`;
  $("outputTime").textContent = fmtTime(s.outputTime);
  $("inputVoltage").textContent = `${fmt(s.inputVoltage)} V`;
  // Both the live big display and the energy table share temperature values
  const tIn = `${fmt(s.internalTemp, 1)} ${s.tempCelsius ? "°C" : "°F"}`;
  const tEx = `${fmt(s.externalTemp, 1)} ${s.tempCelsius ? "°C" : "°F"}`;
  if ($("internalTemp")) $("internalTemp").textContent = tIn;
  document.querySelectorAll(".energy-temp-in").forEach((el) => el.textContent = tIn);
  if ($("externalTemp")) $("externalTemp").textContent = tEx;
  document.querySelectorAll(".energy-temp-ex").forEach((el) => el.textContent = tEx);

  // Protection inputs (skipped while a profile is previewed in the protection card)
  if (viewingGroup == null) {
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
    if (editable("pSetV")) $("pSetV").value = fmt(s.voltageSet);
    if (editable("pSetI")) $("pSetI").value = fmt(s.currentSet, 3);
  }

  // Settings inputs (skipped while the user is editing them)
  if (editable("cBacklight")) $("cBacklight").value = s.backlight != null ? s.backlight : "";
  if (editable("cSleep")) $("cSleep").value = s.sleepTimeout != null ? s.sleepTimeout : "";
  if (editable("cSlave")) $("cSlave").value = s.slaveAddress != null ? s.slaveAddress : "";
  if (editable("cBaud")) $("cBaud").value = s.baudRateCode != null ? String(s.baudRateCode) : "6";
  if (editable("cTempUnit")) $("cTempUnit").value = s.tempCelsius ? "c" : "f";
  if (editable("cBeeper")) $("cBeeper").checked = !!s.beeper;
  if (editable("cMppt")) $("cMppt").checked = !!s.mpptEnabled;
  if (editable("cMpptThr")) $("cMpptThr").value = s.mpptThreshold != null ? fmt(s.mpptThreshold) : "";
  if (editable("cCpMode")) $("cCpMode").checked = !!s.cpModeEnabled;
  if (editable("cBtf")) $("cBtf").value = fmt(s.batteryCutoff, 3);
  if (!configDirty) {
    lastConfig = {
      backlight: s.backlight != null ? String(s.backlight) : "",
      sleep: s.sleepTimeout != null ? String(s.sleepTimeout) : "",
      slave: s.slaveAddress != null ? String(s.slaveAddress) : "",
      baud: s.baudRateCode != null ? String(s.baudRateCode) : "6",
      tempunit: s.tempCelsius ? "c" : "f",
      beeper: !!s.beeper,
      mppt: !!s.mpptEnabled,
      mpptThr: s.mpptThreshold != null ? fmt(s.mpptThreshold) : "",
      cpmode: !!s.cpModeEnabled,
      btf: fmt(s.batteryCutoff, 3),
    };
  }
  document.querySelectorAll(".memgroup-sel").forEach((el) => {
    if (el === document.activeElement) return;
    // While previewing a profile, keep the protection selector on the chosen group
    if (viewingGroup != null && el.id === "protMemGroup") return;
    el.value = s.memoryGroup != null ? String(s.memoryGroup) : "0";
  });

  const keyLockBtn = $("keyLock");
  keyLockBtn.textContent = s.keyLockEnabled ? "🔒" : "🔓";
  keyLockBtn.className = "btn btn-sm " + (s.keyLockEnabled ? "btn-warning" : "btn-ghost");
  keyLockBtn.title = s.keyLockEnabled ? "Снять блокировку" : "Заблокировать";

  const cpMode = !!s.cpModeEnabled;
  // CP mode: show the power setpoint, hide the current setpoint (and vice versa)
  $("pCol").classList.toggle("hidden", !cpMode);
  $("iCol").classList.toggle("hidden", cpMode);

  if (editable("vIn")) $("vIn").value = fmt(s.voltageSet);
  if (editable("iIn")) $("iIn").value = fmt(s.currentSet, 3);
  if (editable("pIn")) $("pIn").value = s.powerSet != null ? fmt(s.powerSet, 1) : "";

  $("model").textContent = `Model ${s.model} / v${s.version}`;
}

// Populate the protection fields with a memory group's stored profile values
let profileDirty = false;
let lastProfile = {};

function renderMemoryGroup(d) {
  const vals = {
    pSetV: fmt(d.voltageSet),
    pSetI: fmt(d.currentSet, 3),
    pLvp: fmt(d.lvp),
    pOvp: fmt(d.ovp),
    pOcp: fmt(d.ocp, 3),
    pOpp: fmt(d.opp, 1),
    pOtp: fmt(d.otp, 1),
    pEtp: fmt(d.etp, 1),
    pOhpH: d.ohpHours != null ? String(d.ohpHours) : "",
    pOhpM: d.ohpMinutes != null ? String(d.ohpMinutes) : "",
    pOha: fmt(d.overAmpHours, 3),
    pOwh: fmt(d.overWattHours, 1),
    pIni: !!d.outputOnAtStartup,
  };
  Object.keys(vals).forEach((id) => {
    const el = $(id);
    if (!el) return;
    if (el.type === "checkbox") el.checked = vals[id];
    else el.value = vals[id];
  });
  lastProfile = vals;
  profileDirty = false;
  toast(`Профиль M${d.group} загружен`);
}

function profileInputsDirty() {
  profileDirty = true;
}

// Send only the profile fields that really changed
function saveProfile() {
  if (viewingGroup == null) { toast("Сначала выберите профиль"); return; }
  const p = lastProfile;
  const req = {};
  const num = (id) => parseFloat($(id).value) || 0;
  if ($("pSetV").value !== p.pSetV) req.voltageSet = num("pSetV");
  if ($("pSetI").value !== p.pSetI) req.currentSet = num("pSetI");
  if ($("pLvp").value !== p.pLvp) req.lvp = num("pLvp");
  if ($("pOvp").value !== p.pOvp) req.ovp = num("pOvp");
  if ($("pOcp").value !== p.pOcp) req.ocp = num("pOcp");
  if ($("pOpp").value !== p.pOpp) req.opp = num("pOpp");
  if ($("pOtp").value !== p.pOtp) req.otp = num("pOtp");
  if ($("pEtp").value !== p.pEtp) req.etp = num("pEtp");
  if ($("pOhpH").value !== p.pOhpH) req.ohpHours = parseInt($("pOhpH").value) || 0;
  if ($("pOhpM").value !== p.pOhpM) req.ohpMinutes = parseInt($("pOhpM").value) || 0;
  if ($("pOha").value !== p.pOha) req.overAmpHours = num("pOha");
  if ($("pOwh").value !== p.pOwh) req.overWattHours = num("pOwh");
  if ($("pIni").checked !== p.pIni) req.outputOnAtStartup = $("pIni").checked;

  const keys = Object.keys(req);
  if (!keys.length) { toast("Нет изменений"); return; }
  req.action = "saveMemoryGroup";
  req.group = viewingGroup;
  send(req);
  profileDirty = false;
  toast("Сохранено изменений: " + keys.length);
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
    case "memoryGroupData":
      if (msg.success) {
        viewingGroup = Number(msg.group);
        renderMemoryGroup(msg);
      } else toast("Не удалось прочитать профиль");
      break;
    case "saveMemoryGroupResponse":
      toast(msg.success ? `Профиль M${msg.group} сохранён` : "Ошибка сохранения профиля");
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
  if (protArmed) {
    send({ action: "clearProtection" });
    return;
  }
  send({ action: "powerOutput", enable: !outputOn });
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

function setPower() {
  const p = parseFloat($("pIn").value);
  if (isNaN(p)) return;
  send({ action: "setPower", power: p });
}

// Step the voltage/current/power setpoint by a fixed delta and apply it
function stepSetpoint(which, delta, decimals) {
  const input = $(which);
  const cur = parseFloat(input.value);
  const next = (isNaN(cur) ? 0 : cur) + delta;
  input.value = next.toFixed(decimals);
  if (which === "vIn") setVoltage();
  else if (which === "iIn") setCurrent();
  else setPower();
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
function applyRow(action, btn) {
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
    case "cpmode": send({ action: "setCpMode", enabled: $("cCpMode").checked }); break;
    case "btf": send({ action: "setBatteryCutoff", current: parseFloat($("cBtf").value) }); break;
    case "memgroup": {
      const sel = btn ? btn.closest(".ctrl, .memgroup-row").querySelector(".memgroup-sel") : document.querySelector(".memgroup-sel");
      if (sel) send({ action: "setMemoryGroup", group: parseInt(sel.value) });
      break;
    }
  }
}

// Send only the settings that really changed since the last status push
function saveConfig() {
  const c = lastConfig;
  const req = [];
  if (String($("cBacklight").value) !== c.backlight) req.push({ action: "setBacklight", level: parseInt($("cBacklight").value) });
  if (String($("cSleep").value) !== c.sleep) req.push({ action: "setSleepTimeout", minutes: parseInt($("cSleep").value) });
  if (String($("cSlave").value) !== c.slave) req.push({ action: "setSlaveAddress", address: parseInt($("cSlave").value) });
  if ($("cBaud").value !== c.baud) req.push({ action: "setBaudRate", code: parseInt($("cBaud").value) });
  if ($("cTempUnit").value !== c.tempunit) req.push({ action: "setTempUnit", celsius: $("cTempUnit").value === "c" });
  if ($("cBeeper").checked !== c.beeper) req.push({ action: "setBeeper", enabled: $("cBeeper").checked });
  const mpptChanged = $("cMppt").checked !== c.mppt || String($("cMpptThr").value) !== c.mpptThr;
  if (mpptChanged) req.push({ action: "setMppt", enable: $("cMppt").checked, threshold: parseFloat($("cMpptThr").value) || 0.8 });
  if ($("cCpMode").checked !== c.cpmode) req.push({ action: "setCpMode", enabled: $("cCpMode").checked });
  if (String($("cBtf").value) !== c.btf) req.push({ action: "setBatteryCutoff", current: parseFloat($("cBtf").value) });

  if (!req.length) { toast("Нет изменений"); return; }
  req.forEach(send);
  configDirty = false;
  toast("Отправлено " + req.length + " измен." );
}

function cancelConfig() {
  configDirty = false;
  send({ action: "getData" });
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
  $("restartBtn").addEventListener("click", () => {
    if (confirm("Перезапустить ESP32?")) send({ action: "restart" });
  });

  // Settings form: mark dirty on any edit, Save/Cancel like the protection card
  ["cBacklight", "cSleep", "cSlave", "cBaud", "cTempUnit", "cBeeper", "cMppt", "cMpptThr", "cCpMode", "cBtf"].forEach((id) => {
    const el = $(id);
    if (!el) return;
    el.addEventListener(el.dataset.markOnly ? "click" : "change", configInputsDirty);
  });
  $("cfgSaveBtn").addEventListener("click", saveConfig);
  $("cfgCancelBtn").addEventListener("click", cancelConfig);

  // Tabs
  document.querySelectorAll("#tabbar button").forEach((btn) => {
    btn.addEventListener("click", () => switchTab(btn.dataset.page));
  });

  // Multi-instance controls
  $("deviceSelect").addEventListener("change", (e) => switchDevice(e.target.value));
  $("addOk").addEventListener("click", () => {
    addDevice($("newName").value, $("newIp").value);
    $("newName").value = "";
    $("newIp").value = "";
  });
  const addIpEnter = (e) => { if (e.key === "Enter") $("addOk").click(); };
  $("newIp").addEventListener("keydown", addIpEnter);
  $("newName").addEventListener("keydown", addIpEnter);

  // All [data-action] "ok" buttons
  document.querySelectorAll(".btn[data-action]").forEach((btn) => {
    btn.addEventListener("click", () => applyRow(btn.dataset.action, btn));
  });

  // Protection card: profile preview + Save/Cancel/Recall
  $("protMemGroup").addEventListener("change", (e) => {
    const g = parseInt(e.target.value);
    if (isNaN(g)) return;
    send({ action: "getMemoryGroup", group: g });
  });
  ["pSetV", "pSetI", "pLvp", "pOvp", "pOcp", "pOpp", "pOtp", "pEtp", "pOhpH", "pOhpM", "pOha", "pOwh", "pIni"].forEach((id) => {
    const el = $(id);
    if (!el) return;
    el.addEventListener("change", profileInputsDirty);
  });
  $("protSaveBtn").addEventListener("click", saveProfile);
  $("protCancelBtn").addEventListener("click", () => {
    const g = viewingGroup;
    viewingGroup = null;
    profileDirty = false;
    $("protMemGroup").value = String(g == null ? 0 : g);
    send({ action: "getData" }); // restore live values
  });
  $("protRecallBtn").addEventListener("click", () => {
    if (viewingGroup == null) { toast("Сначала выберите профиль"); return; }
    send({ action: "setMemoryGroup", group: viewingGroup });
  });

  $("vIn").addEventListener("keydown", (e) => { if (e.key === "Enter") setVoltage(); });
  $("iIn").addEventListener("keydown", (e) => { if (e.key === "Enter") setCurrent(); });

  // Step buttons for voltage/current with auto-repeat while held
  const addStepHandler = (id, target, delta, decimals) => {
    const el = $(id);
    let timer = null;
    const step = () => stepSetpoint(target, delta, decimals);
    const start = (e) => {
      e.preventDefault();
      step();
      timer = setInterval(step, 120);
    };
    const stop = () => clearInterval(timer);
    el.addEventListener("mousedown", start);
    el.addEventListener("touchstart", start, { passive: false });
    el.addEventListener("mouseup", stop);
    el.addEventListener("mouseleave", stop);
    el.addEventListener("touchend", stop);
    el.addEventListener("touchcancel", stop);
  };
  addStepHandler("vMinus", "vIn", -0.1, 2);
  addStepHandler("vPlus", "vIn", 0.1, 2);
  addStepHandler("iMinus", "iIn", -0.01, 3);
  addStepHandler("iPlus", "iIn", 0.01, 3);
  addStepHandler("pMinus", "pIn", -0.1, 1);
  addStepHandler("pPlus", "pIn", 0.1, 1);

  $("pIn").addEventListener("keydown", (e) => { if (e.key === "Enter") setPower(); });

  connect();
  setInterval(loadWifiStatus, 30000);
});