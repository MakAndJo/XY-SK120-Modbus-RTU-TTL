/* XY-SK150 browser client: native WebSocket bridge to the server. */
(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);
  const el = {};

  ['conn', 'connText', 'bindSection', 'deviceInfo', 'devId', 'devName', 'devModel',
   'devOnline', 'bindForm', 'bindKey', 'bindErr', 'bindBtn', 'statusSection',
   'voltage', 'current', 'power', 'outputBtn', 'modeBtn', 'setVoltage', 'setCurrent',
   'setBtn', 'setPower', 'setPowerBtn', 'ampHours', 'wattHours', 'outputTime', 'temp',
   'inputVoltage', 'ovp', 'ocp', 'opp', 'otp', 'lvp', 'setProtBtn', 'keyLockBtn',
   'beepBtn', 'psuResetBtn', 'memGroup', 'memLoadBtn', 'memSaveBtn', 'resetLocal']
    .forEach((id) => { el[id] = $(id); });

  const store = {
    get deviceId() { return localStorage.getItem('xysk_deviceId') || ''; },
    set deviceId(v) { v ? localStorage.setItem('xysk_deviceId', v) : localStorage.removeItem('xysk_deviceId'); },
  };

  const wsUrl = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;

  let ws = null;
  let reconnectTimer = null;
  let deviceId = '';   // bound device

  // ---- WebSocket bridge ------------------------------------------------------

  function connect() {
    ws = new WebSocket(wsUrl);
    ws.onopen = () => {
      setOnline(true);
      if (deviceId) subscribe(deviceId);
    };
    ws.onclose = () => {
      setOnline(false);
      scheduleReconnect();
    };
    ws.onerror = () => { /* onclose fires next */ };
    ws.onmessage = onMessage;
  }

  function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, 3000);
  }

  function send(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg));
  }

  function subscribe(id) {
    deviceId = id;
    store.deviceId = id;
    send({ type: 'subscribe', deviceId: id });
  }

  function sendCommand(payload) {
    if (!deviceId) return;
    send({ type: 'command', deviceId, payload });
  }

  function onMessage(event) {
    let msg;
    try { msg = JSON.parse(event.data); } catch { return; }
    switch (msg.type) {
      case 'bindResult':
        handleBindResult(msg);
        break;
      case 'info':
        el.devName.textContent = (msg.data && msg.data.name) || 'XY-SK150S';
        el.devModel.textContent = (msg.data && msg.data.model) || '-';
        break;
      case 'status':
        renderStatus(msg.data || {});
        break;
      case 'online':
        el.devOnline.textContent = msg.online ? 'online' : 'offline';
        break;
      case 'response':
        handleResponse(msg.data || {});
        break;
      case 'error':
        console.warn('[ws] server error:', msg.message);
        break;
    }
  }

  // ---- rendering ------------------------------------------------------------

  function setOnline(online) {
    el.conn.classList.toggle('online', online);
    el.connText.textContent = online ? 'online' : 'offline';
  }

  const fmt = (v, d) => (v === undefined || v === null || isNaN(v)) ? '--' : Number(v).toFixed(d);

  function renderStatus(s) {
    el.voltage.textContent = fmt(s.voltage, 2);
    el.current.textContent = fmt(s.current, 3);
    el.power.textContent = fmt(s.power, 1);
    el.inputVoltage.textContent = fmt(s.inputVoltage, 1);
    el.ampHours.textContent = fmt(s.ampHours, 3);
    el.wattHours.textContent = fmt(s.wattHours, 1);
    el.outputTime.textContent = (s.outputTime !== undefined) ? formatRuntime(s.outputTime) : '--';
    const tempVal = (s.tempCelsius === false) ? null : s.internalTemp;
    el.temp.textContent = tempVal !== null && tempVal !== undefined ? fmt(tempVal, 1) + '°C' : '--';

    el.outputBtn.textContent = s.outputEnabled ? 'Output ON' : 'Output OFF';
    el.outputBtn.classList.toggle('on', !!s.outputEnabled);

    const modeNames = { 1: 'CV', 2: 'CC', 3: 'CP' };
    el.modeBtn.textContent = 'Mode: ' + (modeNames[s.cvccMode] || '--');
    el.keyLockBtn.textContent = 'Key lock: ' + (s.keyLocked ? 'ON' : 'OFF');
    el.beepBtn.textContent = 'Beeper: ' + (s.beeper === undefined ? '--' : (s.beeper ? 'ON' : 'OFF'));

    ['ovp', 'ocp', 'opp', 'otp', 'lvp'].forEach((k) => {
      if (s[k] !== undefined && !el[k].value) el[k].value = s[k];
    });
    if (s.memoryGroup) el.memGroup.value = s.memoryGroup;
    if (s.voltageSet !== undefined) el.setVoltage.value = s.voltageSet;
    if (s.currentSet !== undefined) el.setCurrent.value = s.currentSet;
  }

  function formatRuntime(seconds) {
    seconds = Math.floor(seconds);
    const h = Math.floor(seconds / 3600), m = Math.floor((seconds % 3600) / 60), s = seconds % 60;
    return `${h}h ${m}m ${s}s`;
  }

  function handleResponse(r) {
    if (r.success === false) console.warn('command failed', r);
  }

  // ---- bind flow ------------------------------------------------------------

  async function bindDevice(key) {
    el.bindErr.classList.add('hidden');
    send({ type: 'bind', key });
  }

  function handleBindResult(r) {
    if (!r.ok) {
      el.bindErr.textContent = r.error || 'Bind failed';
      el.bindErr.classList.remove('hidden');
      return;
    }
    deviceId = r.deviceId;
    store.deviceId = r.deviceId;
    el.devId.textContent = r.deviceId;
    el.devName.textContent = r.name || '-';
    el.devModel.textContent = r.model || '-';
    enterDeviceView();
    subscribe(r.deviceId); // triggers cached retained state push
  }

  function enterDeviceView() {
    el.deviceInfo.classList.remove('hidden');
    el.bindForm.classList.add('hidden');
    el.bindBtn.classList.remove('hidden');
    el.statusSection.classList.remove('hidden');
    el.miscSection.classList.remove('hidden');
    el.memSection.classList.remove('hidden');
  }

  function enterBindView() {
    deviceId = '';
    el.deviceInfo.classList.add('hidden');
    el.bindForm.classList.remove('hidden');
    el.bindBtn.classList.add('hidden');
    el.statusSection.classList.add('hidden');
    el.miscSection.classList.add('hidden');
    el.memSection.classList.add('hidden');
    el.bindKey.value = '';
  }

  // ---- wiring ---------------------------------------------------------------

  el.bindForm.addEventListener('submit', (e) => { e.preventDefault(); bindDevice(el.bindKey.value.trim()); });
  el.bindBtn.addEventListener('click', enterBindView);
  el.resetLocal.addEventListener('click', (e) => { e.preventDefault(); store.deviceId = ''; enterBindView(); });

  el.outputBtn.addEventListener('click', () =>
    sendCommand({ action: 'powerOutput', enable: !el.outputBtn.classList.contains('on') }));
  el.setBtn.addEventListener('click', () =>
    sendCommand({ action: 'setConfig', voltage: parseFloat(el.setVoltage.value), current: parseFloat(el.setCurrent.value) }));
  el.setPowerBtn.addEventListener('click', () =>
    sendCommand({ action: 'setConfig', power: parseFloat(el.setPower.value) }));
  el.setProtBtn.addEventListener('click', () =>
    sendCommand({ action: 'setProtection',
      ovp: parseFloat(el.ovp.value), ocp: parseFloat(el.ocp.value),
      opp: parseFloat(el.opp.value), otp: parseFloat(el.otp.value),
      lvp: parseFloat(el.lvp.value) }));
  el.keyLockBtn.addEventListener('click', () =>
    sendCommand({ action: 'setKeyLock', enabled: !el.keyLockBtn.textContent.endsWith('ON') }));
  el.beepBtn.addEventListener('click', () =>
    sendCommand({ action: 'setBeeper', enabled: !el.beepBtn.textContent.endsWith('ON') }));
  el.psuResetBtn.addEventListener('click', () => sendCommand({ action: 'psuReset' }));
  el.memLoadBtn.addEventListener('click', () =>
    sendCommand({ action: 'setMemoryGroup', group: parseInt(el.memGroup.value, 10) }));
  el.memSaveBtn.addEventListener('click', () =>
    sendCommand({ action: 'saveMemoryGroup', group: parseInt(el.memGroup.value, 10) }));

  // ---- boot -----------------------------------------------------------------

  if (store.deviceId) {
    deviceId = store.deviceId;
    el.devId.textContent = deviceId;
    enterDeviceView();
  } else {
    enterBindView();
  }
  connect();
})();