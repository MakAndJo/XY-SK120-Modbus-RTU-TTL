/**
 * XY-SK150 server: embedded MQTT broker (aedes) + WebSocket gateway + accounts.
 *
 * - aedes broker on tcp:1883 (devices, no credentials - clientId "xy-<deviceId>")
 * - HTTP server on :8080:
 *     GET /                    → serves the client UI (../client)
 *     GET /ws?token=           → WebSocket gateway for the browser client
 *     POST /api/register       → { login, password } → { token, userId }
 *     POST /api/login          → { login, password } → { token, userId }
 *     POST /api/logout
 *     GET  /api/devices        → bound devices of the current user
 *     POST /api/bind           → { code } binds a device (8-digit code) to the user
 *     POST /api/devices/:id/rename → { name }
 *     DELETE /api/devices/:id  → unbind (generates a fresh pairing code)
 *
 * Device topics (see docs/MQTT_PROTOCOL.md):
 *   xysk/<deviceId>/info      retained, device identity
 *   xysk/<deviceId>/status    retained, current PSU status snapshot
 *   xysk/<deviceId>/online    retained LWT 1/0
 *   xysk/<deviceId>/command   server → device commands
 *   xysk/<deviceId>/response  device → server replies to commands
 *
 * Pairing:
 *   The device connects with clientId "xy-<deviceId>" (no credentials). When it
 *   subscribes to its command topic the broker makes sure a pair_code exists and
 *   pushes it via {action:"setPairCode", code}. The user reads the code off the
 *   PSU screen / local UI and enters it in the client → device is bound.
 *   Binding clears the code on the device ({action:"setPairCode", code:""});
 *   unbinding generates a new code and pushes it again.
 *
 * WebSocket protocol (JSON frames), all subscribe/command restricted to devices
 * the authenticated user owns:
 *   client → server:
 *     { "type":"subscribe",  "deviceId":"dev_A1B2C3" }
 *     { "type":"unsubscribe","deviceId":"dev_A1B2C3" }
 *     { "type":"command", "deviceId":"dev_A1B2C3", "payload":{ "action":"setVoltage", ... } }
 *     { "type":"ping" }
 *   server → client:
 *     { "type":"info",    "deviceId":..., "data":{...} }
 *     { "type":"status",  "deviceId":..., "data":{...} }
 *     { "type":"online",  "deviceId":..., "online":true|false }
 *     { "type":"response","deviceId":..., "data":{...} }
 *     { "type":"error",   "message":"..." }
 */
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { existsSync, mkdirSync, renameSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { randomBytes, scryptSync, timingSafeEqual } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { createServer as createNetServer } from 'node:net';
import { DatabaseSync } from 'node:sqlite';
import aedes from 'aedes';
import { WebSocketServer } from 'ws';

const __dirname = dirname(fileURLToPath(import.meta.url));

const TCP_PORT = process.env.MQTT_PORT || 1883;
const HTTP_PORT = process.env.HTTP_PORT || 8080;
const CLIENT_DIR = resolve(__dirname, '..', 'client');
const DATA_DIR = resolve(__dirname, 'data');

// ---- storage (SQLite) ---------------------------------------------------------
mkdirSync(DATA_DIR, { recursive: true });

// Old JSON files are dropped (orphaned bindings can simply be removed).
for (const f of ['devices.json', 'laststate.json']) {
  const p = resolve(DATA_DIR, f);
  if (existsSync(p)) {
    try { renameSync(p, p + '.bak'); console.log(`[db] moved legacy ${f} → ${f}.bak`); } catch {}
  }
}

const db = new DatabaseSync(resolve(DATA_DIR, 'app.db'));
db.exec(`
  CREATE TABLE IF NOT EXISTS users (
    id         TEXT PRIMARY KEY,
    login      TEXT UNIQUE NOT NULL,
    pass_hash  TEXT NOT NULL,
    salt       TEXT NOT NULL,
    created_at INTEGER NOT NULL
  );
  CREATE TABLE IF NOT EXISTS sessions (
    token      TEXT PRIMARY KEY,
    user_id    TEXT NOT NULL,
    created_at INTEGER NOT NULL
  );
  CREATE TABLE IF NOT EXISTS devices (
    device_id TEXT PRIMARY KEY,
    name      TEXT,
    model     TEXT,
    bound_to  TEXT,
    pair_code TEXT,
    last_seen INTEGER
  );
  CREATE TABLE IF NOT EXISTS laststate (
    device_id TEXT NOT NULL,
    kind      TEXT NOT NULL,
    value     TEXT,
    PRIMARY KEY (device_id, kind)
  );
`);

// ---- auth helpers ------------------------------------------------------------

function hashPassword(pass, salt) {
  return scryptSync(String(pass), salt, 32).toString('hex');
}

function createUser(login, password) {
  const salt = randomBytes(16).toString('hex');
  const hash = hashPassword(password, salt);
  const id = 'user_' + randomBytes(6).toString('hex');
  db.prepare('INSERT INTO users (id, login, pass_hash, salt, created_at) VALUES (?,?,?,?,?)')
    .run(id, login, hash, salt, Date.now());
  return id;
}

function verifyPassword(login, password) {
  const row = db.prepare('SELECT * FROM users WHERE login = ?').get(login);
  if (!row) return null;
  const expected = Buffer.from(row.pass_hash, 'hex');
  const actual = Buffer.from(hashPassword(password, row.salt), 'hex');
  if (expected.length !== actual.length) return null;
  return timingSafeEqual(expected, actual) ? row : null;
}

function createSession(userId) {
  const token = randomBytes(24).toString('hex');
  db.prepare('INSERT INTO sessions (token, user_id, created_at) VALUES (?,?,?)')
    .run(token, userId, Date.now());
  return token;
}

function userIdByToken(token) {
  if (!token) return null;
  const row = db.prepare('SELECT user_id FROM sessions WHERE token = ?').get(token);
  return row ? row.user_id : null;
}

function destroySession(token) {
  if (token) db.prepare('DELETE FROM sessions WHERE token = ?').run(token);
}

// ---- device / pairing helpers -------------------------------------------------

function generatePairCode() {
  // 8 digits, each 1-9. The code is shown on the PSU screen as IPv4
  // (4 groups of 2 digits) and IPv4 rendering drops leading zeros — a group
  // like "00" would appear as "0" and become unreadable. Digits 1-9 only
  // guarantee every group renders as exactly two digits (11-99).
  const digits = '123456789';
  let code = '';
  for (let i = 0; i < 8; i++) code += digits[Math.floor(Math.random() * 9)];
  return code;
}

function normalizeCode(code) {
  return String(code || '').replace(/[^0-9]/g, '').slice(0, 8);
}

function ensurePairCode(deviceId) {
  const dev = db.prepare('SELECT * FROM devices WHERE device_id = ?').get(deviceId);
  if (!dev) return null;
  if (dev.bound_to) return null; // bound: no code shown
  // Regenerate codes that contain a 0 (legacy codes were unreadable on the
  // PSU screen because IPv4 rendering drops leading zeros).
  if (!dev.pair_code || /0/.test(dev.pair_code)) {
    const code = generatePairCode();
    db.prepare('UPDATE devices SET pair_code = ? WHERE device_id = ?').run(code, deviceId);
    dev.pair_code = code;
  }
  return dev.pair_code;
}

function pushPairCode(deviceId, code) {
  // code = "" clears the code on the device (device becomes "bound")
  broker.publish({
    topic: `xysk/${deviceId}/command`,
    payload: Buffer.from(JSON.stringify({ action: 'setPairCode', code: code || '' })),
    qos: 1,
  }, (err) => { if (err) console.error('[broker] setPairCode publish error:', err.message); });
}

function ensureDeviceRow(deviceId) {
  const exists = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!exists) {
    db.prepare('INSERT INTO devices (device_id, last_seen) VALUES (?,?)')
      .run(deviceId, Date.now());
  } else {
    db.prepare('UPDATE devices SET last_seen = ? WHERE device_id = ?').run(Date.now(), deviceId);
  }
}

function deviceOnline(deviceId) {
  const row = db.prepare('SELECT value FROM laststate WHERE device_id = ? AND kind = ?')
    .get(deviceId, 'online');
  return row ? row.value === '1' : false;
}

// ---- broker -------------------------------------------------------------------

const broker = aedes();

createNetServer(broker.handle).listen(TCP_PORT, () =>
  console.log(`[broker] MQTT (TCP) listening on :${TCP_PORT}`));

// Register known devices when they connect. clientId is "xy-<deviceId>".
broker.on('client', (client) => {
  const id = String(client.id || '');
  if (id.startsWith('xy-')) {
    const deviceId = id.slice(3);
    ensureDeviceRow(deviceId);
    console.log(`[broker] device connected: ${id}`);
  } else {
    console.log(`[broker] client connected: ${id}`);
  }
});
broker.on('clientDisconnect', (client) => {
  const id = String(client.id || '');
  if (id.startsWith('xy-')) {
    console.log(`[broker] device disconnected: ${id}`);
  } else {
    console.log(`[broker] client disconnected: ${id}`);
  }
});

// When a device subscribes to its command topic it is ready to receive the
// pairing code (or the "bound, clear code" message). Always sync the device's
// stored code to the server state: a bound device gets an empty code so any
// stale code left in NVS (e.g. after a reboot) is cleared.
broker.on('subscribe', (subscriptions, client) => {
  const id = String(client.id || '');
  if (!id.startsWith('xy-')) return;
  const deviceId = id.slice(3);
  for (const sub of subscriptions) {
    if (sub.topic === `xysk/${deviceId}/command`) {
      const dev = db.prepare('SELECT bound_to FROM devices WHERE device_id = ?').get(deviceId);
      if (dev && dev.bound_to) {
        pushPairCode(deviceId, '');
      } else {
        const code = ensurePairCode(deviceId);
        if (code) pushPairCode(deviceId, code);
      }
      break;
    }
  }
});

// ---- gateway: fan out device publications to bound browsers ---------------------
const wsSubscribers = new Map(); // deviceId -> Set<WebSocket>

function send(ws, obj) {
  if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
}

function addSubscriber(deviceId, ws) {
  if (!wsSubscribers.has(deviceId)) wsSubscribers.set(deviceId, new Set());
  wsSubscribers.get(deviceId).add(ws);
  // replay cached state for a fresh subscription
  const rows = db.prepare('SELECT kind, value FROM laststate WHERE device_id = ?').all(deviceId);
  const isOn = deviceOnline(deviceId);
  for (const { kind, value } of rows) {
    if (kind === 'online') {
      send(ws, { type: 'online', deviceId, online: isOn });
    } else if (kind !== 'command') {
      if (!isOn) continue;
      let parsed = value;
      try { parsed = JSON.parse(value); } catch {}
      send(ws, { type: kind, deviceId, data: parsed });
    }
  }
}

function removeSubscriber(deviceId, ws) {
  const set = wsSubscribers.get(deviceId);
  if (!set) return;
  set.delete(ws);
  if (set.size === 0) wsSubscribers.delete(deviceId);
}

function safeParse(payload) {
  try { return JSON.parse(payload.toString()); } catch { return payload.toString(); }
}

broker.on('publish', async (packet) => {
  if (!packet.topic.startsWith('xysk/')) return;
  const parts = packet.topic.split('/');
  if (parts.length < 3) return;
  const deviceId = parts[1];
  const kind = parts[2];
  const payload = packet.payload.toString();

  // device identity / registration
  if (kind === 'info') {
    try {
      const info = JSON.parse(payload);
      if (info.deviceId !== deviceId) return;
      ensureDeviceRow(deviceId);
      db.prepare('UPDATE devices SET name = ?, model = ? WHERE device_id = ?')
        .run(info.name || 'XY-SK150S', info.model || 'XY-SK150S', deviceId);
    } catch (e) {
      console.error('[broker] bad info message:', e.message);
    }
  }

  // Touch pairing requested from the block: drop the old binding and record the
// fresh code (the device generates it and ships it in the payload). Fall back
// to generating one here if the payload is missing/invalid.
  if (kind === 'repair') {
    ensureDeviceRow(deviceId);
    let code = '';
    try {
      const r = JSON.parse(payload);
      code = normalizeCode(r.code);
    } catch {}
    if (code.length !== 8) code = generatePairCode();
    db.prepare('UPDATE devices SET bound_to = NULL, pair_code = ? WHERE device_id = ?')
      .run(code, deviceId);
    console.log(`[repair] ${deviceId} re-pairing, new code ${code}`);
    pushPairCode(deviceId, code);
    return;
  }

  // cache latest value (skip commands, they run server → device only)
  if (kind !== 'command') {
    db.prepare('INSERT OR REPLACE INTO laststate (device_id, kind, value) VALUES (?,?,?)')
      .run(deviceId, kind, payload);
  }

  // fan out to bound browsers
  const set = wsSubscribers.get(deviceId);
  if (!set) return;

  let frame;
  if (kind === 'online') {
    frame = { type: 'online', deviceId, online: payload === '1' };
  } else if (kind !== 'command') {
    frame = { type: kind, deviceId, data: safeParse(payload) };
  }
  if (!frame) return;
  for (const ws of set) send(ws, frame);
});

// ---- HTTP: static + JSON API ---------------------------------------------------

const mime = {
  '.html': 'text/html', '.js': 'application/javascript',
  '.css': 'text/css', '.svg': 'image/svg+xml', '.png': 'image/png',
  '.ico': 'image/x-icon', '.json': 'application/json',
};

async function serveStatic(req, res, urlPath) {
  let rel = decodeURIComponent(urlPath);
  if (rel === '/' || rel === '') rel = '/index.html';
  const relPath = rel.startsWith('/') ? rel.slice(1) : rel;
  const file = resolve(CLIENT_DIR, relPath);
  if (!file.startsWith(CLIENT_DIR) || !existsSync(file)) {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not found');
    return;
  }
  try {
    const data = await readFile(file);
    const ext = file.slice(file.lastIndexOf('.'));
    res.writeHead(200, {
      'Content-Type': mime[ext] || 'application/octet-stream',
      'Cache-Control': 'no-store, no-cache, must-revalidate, max-age=0',
      'Pragma': 'no-cache',
    });
    res.end(data);
  } catch {
    res.writeHead(500);
    res.end('Error');
  }
}

function readBody(req) {
  return new Promise((resolvePromise) => {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e6) req.destroy(); });
    req.on('end', () => resolvePromise(body));
    req.on('error', () => resolvePromise(''));
  });
}

function json(res, status, obj) {
  res.writeHead(status, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify(obj));
}

function requireAuth(req, res) {
  const auth = req.headers.authorization || '';
  const token = auth.startsWith('Bearer ') ? auth.slice(7) : null;
  const userId = userIdByToken(token);
  if (!userId) {
    json(res, 401, { error: 'Unauthorized' });
    return null;
  }
  return userId;
}

const httpServer = createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const path = url.pathname;

  if (req.method === 'POST' && path === '/api/register') {
    const body = JSON.parse((await readBody(req)) || '{}');
    const login = String(body.login || '').trim();
    const password = String(body.password || '');
    if (login.length < 3 || password.length < 4) {
      json(res, 400, { error: 'Login must be ≥3 chars, password ≥4 chars' });
      return;
    }
    if (db.prepare('SELECT id FROM users WHERE login = ?').get(login)) {
      json(res, 409, { error: 'Login already exists' });
      return;
    }
    const userId = createUser(login, password);
    json(res, 200, { token: createSession(userId), userId });
    return;
  }

  if (req.method === 'POST' && path === '/api/login') {
    const body = JSON.parse((await readBody(req)) || '{}');
    const row = verifyPassword(String(body.login || ''), String(body.password || ''));
    if (!row) {
      json(res, 401, { error: 'Invalid login or password' });
      return;
    }
    json(res, 200, { token: createSession(row.id), userId: row.id });
    return;
  }

  if (req.method === 'POST' && path === '/api/logout') {
    const auth = req.headers.authorization || '';
    destroySession(auth.startsWith('Bearer ') ? auth.slice(7) : null);
    json(res, 200, { ok: true });
    return;
  }

  // ---- static client UI (public, no auth; the login overlay guards the app) ----
  if (req.method === 'GET' && !path.startsWith('/api/')) {
    await serveStatic(req, res, path);
    return;
  }

  // ---- authenticated API below ----
  const userId = requireAuth(req, res);
  if (!userId) return;

  if (req.method === 'GET' && path === '/api/devices') {
    const rows = db.prepare('SELECT * FROM devices WHERE bound_to = ?').all(userId);
    const devices = rows.map((d) => ({
      deviceId: d.device_id,
      name: d.name || d.device_id,
      model: d.model || 'XY-SK150S',
      online: deviceOnline(d.device_id),
    }));
    json(res, 200, { devices });
    return;
  }

  if (req.method === 'POST' && path === '/api/bind') {
    const body = JSON.parse((await readBody(req)) || '{}');
    const code = normalizeCode(body.code);
    if (code.length !== 8) {
      json(res, 400, { error: 'Enter the 8-digit code shown on the device' });
      return;
    }
    const dev = db.prepare('SELECT * FROM devices WHERE pair_code = ?').get(code);
    if (!dev) {
      json(res, 404, { error: 'No device with this code. Is it connected to the server?' });
      return;
    }
    if (dev.bound_to && dev.bound_to !== userId) {
      json(res, 409, { error: `Device already bound to user ${dev.bound_to}` });
      return;
    }
    db.prepare('UPDATE devices SET bound_to = ?, pair_code = NULL WHERE device_id = ?')
      .run(userId, dev.device_id);
    pushPairCode(dev.device_id, ''); // tell the device: bound, hide code
    console.log(`[bind] device ${dev.device_id} bound to ${userId}`);
    json(res, 200, { deviceId: dev.device_id, name: dev.name || dev.device_id, model: dev.model || 'XY-SK150S' });
    return;
  }

  if (req.method === 'POST' && path.startsWith('/api/devices/') && path.endsWith('/rename')) {
    const deviceId = path.slice('/api/devices/'.length, -'/rename'.length);
    const body = JSON.parse((await readBody(req)) || '{}');
    const name = String(body.name || '').trim().slice(0, 32);
    const dev = db.prepare('SELECT * FROM devices WHERE device_id = ? AND bound_to = ?').get(deviceId, userId);
    if (!dev) { json(res, 404, { error: 'Device not found' }); return; }
    db.prepare('UPDATE devices SET name = ? WHERE device_id = ?').run(name || dev.device_id, deviceId);
    json(res, 200, { ok: true, name: name || dev.device_id });
    return;
  }

  if (req.method === 'DELETE' && path.startsWith('/api/devices/')) {
    const deviceId = path.slice('/api/devices/'.length);
    const dev = db.prepare('SELECT * FROM devices WHERE device_id = ? AND bound_to = ?').get(deviceId, userId);
    if (!dev) { json(res, 404, { error: 'Device not found' }); return; }
    const code = generatePairCode();
    db.prepare('UPDATE devices SET bound_to = NULL, pair_code = ? WHERE device_id = ?').run(code, deviceId);
    pushPairCode(deviceId, code); // device shows a fresh code again
    console.log(`[unbind] device ${deviceId} unbound from ${userId}`);
    json(res, 200, { ok: true });
    return;
  }

  json(res, 404, { error: 'Not found' });
});

// ---- WebSocket gateway (authenticated, same port /ws?token=...) ---------------

const wss = new WebSocketServer({ server: httpServer, path: '/ws' });

wss.on('connection', (ws, req) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const token = url.searchParams.get('token') || '';
  const userId = userIdByToken(token);
  if (!userId) {
    ws.close(4001, 'Unauthorized');
    return;
  }

  const canAccess = (deviceId) => {
    const dev = db.prepare('SELECT device_id FROM devices WHERE device_id = ? AND bound_to = ?')
      .get(deviceId, userId);
    return !!dev;
  };

  ws.on('error', (err) => console.error('[ws] client error:', err.message));
  ws.on('message', async (raw) => {
    let msg;
    try { msg = JSON.parse(raw.toString()); } catch {
      send(ws, { type: 'error', message: 'Bad JSON' });
      return;
    }
    switch (msg.type) {
      case 'subscribe':
        if (!msg.deviceId) { send(ws, { type: 'error', message: 'Missing deviceId' }); break; }
        if (!canAccess(msg.deviceId)) { send(ws, { type: 'error', message: 'Not your device' }); break; }
        addSubscriber(msg.deviceId, ws);
        break;
      case 'unsubscribe':
        if (!msg.deviceId) { send(ws, { type: 'error', message: 'Missing deviceId' }); break; }
        removeSubscriber(msg.deviceId, ws);
        break;
      case 'command':
        if (!msg.deviceId || !msg.payload) {
          send(ws, { type: 'error', message: 'Missing deviceId or payload' });
          break;
        }
        if (!canAccess(msg.deviceId)) { send(ws, { type: 'error', message: 'Not your device' }); break; }
        broker.publish({
          topic: `xysk/${msg.deviceId}/command`,
          payload: Buffer.from(JSON.stringify(msg.payload)),
          qos: 1,
        }, (err) => { if (err) console.error('[broker] publish error:', err.message); });
        break;
      case 'ping':
        send(ws, { type: 'pong' });
        break;
      default:
        send(ws, { type: 'error', message: `Unknown type: ${msg.type}` });
    }
  });

  ws.on('close', () => {
    for (const [deviceId, set] of wsSubscribers) {
      if (set.has(ws)) removeSubscriber(deviceId, ws);
    }
  });
});

httpServer.listen(HTTP_PORT, () =>
  console.log(`[http] client UI + WS gateway (/ws) + API on :${HTTP_PORT}`));

// ---- startup -------------------------------------------------------------------

const userCount = db.prepare('SELECT COUNT(*) AS n FROM users').get().n;
console.log(`[db] ${userCount} user(s), ${db.prepare('SELECT COUNT(*) AS n FROM devices').get().n} device(s)`);
console.log('XY-SK150 server ready.');
console.log(`  devices:    mqtt://localhost:${TCP_PORT}`);
console.log(`  client UI:  http://localhost:${HTTP_PORT}  (WS gateway at /ws)`);