/**
 * XY-SK150 server: embedded MQTT broker (aedes) + WebSocket gateway + bind API.
 *
 * The broker runs in-process, so the WebSocket gateway talks to it directly via
 * aedes' publish events (no second MQTT client / bridge):
 *
 * - aedes broker on tcp:1883 (devices)
 * - HTTP server on :8080:
 *     GET /             → serves the client UI (../client)
 *     GET /ws           → WebSocket gateway for the browser client
 *     POST /api/bind    → { key } binds a device to a user (REST fallback)
 *
 * Device topics (see docs/MQTT_PROTOCOL.md):
 *   xysk/<deviceId>/info      retained, device identity + bind key
 *   xysk/<deviceId>/status    retained, current PSU status snapshot
 *   xysk/<deviceId>/online    retained LWT 1/0
 *   xysk/<deviceId>/command   server → device commands
 *   xysk/<deviceId>/response  device → server replies to commands
 *
 * WebSocket protocol (JSON frames):
 *   client → server:
 *     { "type":"bind",       "key":"..." }
 *     { "type":"subscribe",  "deviceId":"dev_A1B2C3" }
 *     { "type":"unsubscribe","deviceId":"dev_A1B2C3" }
 *     { "type":"command", "deviceId":"dev_A1B2C3", "payload":{ "action":"setVoltage", ... } }
 *     { "type":"ping" }
 *   server → client:
 *     { "type":"bindResult", "ok":true, "deviceId":..., "userId":..., "name":..., "model":... }
 *     { "type":"bindResult", "ok":false, "error":"..." }
 *     { "type":"info",    "deviceId":..., "data":{...} }
 *     { "type":"status",  "deviceId":..., "data":{...} }
 *     { "type":"online",  "deviceId":..., "online":true|false }
 *     { "type":"response","deviceId":..., "data":{...} }
 *     { "type":"error",   "message":"..." }
 */
import { createServer } from 'node:http';
import { readFile, writeFile } from 'node:fs/promises';
import { existsSync, mkdirSync, readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { createServer as createNetServer } from 'node:net';
import aedes from 'aedes';
import { WebSocketServer } from 'ws';

const __dirname = dirname(fileURLToPath(import.meta.url));

const TCP_PORT = process.env.MQTT_PORT || 1883;
const HTTP_PORT = process.env.HTTP_PORT || 8080;
const CLIENT_DIR = resolve(__dirname, '..', 'client');
const DATA_DIR = resolve(__dirname, 'data');
const DEVICES_FILE = resolve(DATA_DIR, 'devices.json');
const LASTSTATE_FILE = resolve(DATA_DIR, 'laststate.json');

// ---- persistence ------------------------------------------------------------

let devices = {}; // deviceId -> { name, model, boundTo: userId|null }
mkdirSync(DATA_DIR, { recursive: true });

async function loadDevices() {
  if (!existsSync(DEVICES_FILE)) return;
  try {
    devices = JSON.parse(await readFile(DEVICES_FILE, 'utf8'));
    console.log(`[db] loaded ${Object.keys(devices).length} device(s)`);
  } catch (e) {
    console.error('[db] failed to parse devices.json, starting empty:', e.message);
    devices = {};
  }
}

async function saveDevices() {
  try {
    await writeFile(DEVICES_FILE, JSON.stringify(devices, null, 2));
  } catch (e) {
    console.error('[db] failed to save devices.json:', e.message);
  }
}

// ---- broker -----------------------------------------------------------------

const broker = aedes();

// TCP/MQTT for the devices themselves.
createNetServer(broker.handle).listen(TCP_PORT, () =>
  console.log(`[broker] MQTT (TCP) listening on :${TCP_PORT}`));

function bindKey(deviceId) {
  return createHash('md5').update(deviceId).digest('hex');
}

// ---------------------------------------------------------------------------
// Gateway: since the broker lives in this process we just tap into aedes'
// publish stream. No second MQTT client, no bridge round-trip.
// deviceId -> Set<WebSocket> bound to that device
const wsSubscribers = new Map();
// deviceId -> Map<kind, value> latest state, persisted so a fresh browser
// bind always gets current info/status/online even across server restarts.
let lastState = new Map();
mkdirSync(DATA_DIR, { recursive: true });

try {
  if (existsSync(LASTSTATE_FILE)) {
    const parsed = JSON.parse(readFileSync(LASTSTATE_FILE, 'utf8'));
    lastState = new Map(Object.entries(parsed).map(([id, kinds]) => [id, new Map(Object.entries(kinds))]));
  }
} catch (e) {
  console.error('[gateway] failed to load laststate.json:', e.message);
}

function persistLastState() {
  const plain = {};
  for (const [deviceId, kinds] of lastState) {
    plain[deviceId] = Object.fromEntries(kinds);
  }
  writeFile(LASTSTATE_FILE, JSON.stringify(plain)).catch(() => {});
}

function send(ws, obj) {
  if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
}

function addSubscriber(deviceId, ws) {
  if (!wsSubscribers.has(deviceId)) wsSubscribers.set(deviceId, new Set());
  wsSubscribers.get(deviceId).add(ws);
  // deliver latest known state immediately
  const cached = lastState.get(deviceId);
  if (cached) {
    // online is authoritative: if the device is offline the cached info/status
    // is stale and must not be replayed as a live value.
    const isOn = cached.get('online') === '1' || cached.get('online') === 1;
    for (const [kind, value] of cached) {
      if (kind === 'online') {
        send(ws, { type: 'online', deviceId, online: isOn });
      } else if (kind !== 'command') {
        if (!isOn) continue; // don't replay stale info/status for an offline block
        send(ws, { type: kind, deviceId, data: value });
      }
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
  try {
    return JSON.parse(payload.toString());
  } catch {
    return payload.toString();
  }
}

// Hook every device publication. We register devices from /info, cache the
// latest values (info/status/online) and fan out to WS subscribers, all
// without leaving the process.
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
      if (info.key !== bindKey(deviceId)) {
        console.warn(`[broker] key mismatch for ${deviceId}, ignoring info`);
        return;
      }
      if (!devices[deviceId]) {
        devices[deviceId] = {
          name: info.name || 'XY-SK150S',
          model: info.model || 'XY-SK150S',
          boundTo: null,
        };
        console.log(`[db] registered device ${deviceId} (${devices[deviceId].name})`);
        await saveDevices();
      }
    } catch (e) {
      console.error('[broker] bad info message:', e.message);
    }
  }

  // cache latest value (skip commands, they run server→device only)
  if (kind !== 'command') {
    // don't cache a retained clear (empty payload) on unsubscribe
    if (!lastState.has(deviceId)) lastState.set(deviceId, new Map());
    lastState.get(deviceId).set(kind, kind === 'online' ? payload : safeParse(payload));
    persistLastState();
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

// ---- HTTP + WebSocket gateway --------------------------------------------------

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

async function handleBind(reqBody) {
  let body;
  try {
    body = JSON.parse(reqBody);
  } catch {
    return { status: 400, json: { error: 'Bad JSON' } };
  }
  const key = String(body.key || '').trim();
  if (!key) return { status: 400, json: { error: 'Missing key' } };

  // Find a registered device whose bind key matches (md5(deviceId)).
  const match = Object.keys(devices).find((id) => bindKey(id) === key);
  if (!match) {
    return {
      status: 404,
      json: { error: 'No device with this bind key. Make sure the device has connected to the broker at least once.' },
    };
  }
  const rec = devices[match];
  const userId = `user_${createHash('sha256').update(key).digest('hex').slice(0, 12)}`;
  // Idempotent bind: the same owner (key is deterministic md5(deviceId)) may
  // re-bind from a fresh browser without getting a 409.
  if (rec.boundTo && rec.boundTo !== userId) {
    return { status: 409, json: { error: `Device already bound to user ${rec.boundTo}` } };
  }
  rec.boundTo = userId;
  await saveDevices();
  console.log(`[bind] device ${match} bound to ${userId}`);
  return { status: 200, json: { deviceId: match, userId, name: rec.name, model: rec.model } };
}

const httpServer = createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  if (req.method === 'POST' && url.pathname === '/api/bind') {
    let reqBody = '';
    for await (const chunk of req) reqBody += chunk;
    const { status, json } = await handleBind(reqBody);
    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(json));
    return;
  }
  await serveStatic(req, res, url.pathname);
});

// WebSocket gateway lives on the same HTTP port (path /ws).
const wss = new WebSocketServer({ server: httpServer, path: '/ws' });

wss.on('connection', (ws) => {
  ws.on('error', (err) => {
    console.error('[ws] client error:', err.message);
  });
  ws.on('message', async (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      send(ws, { type: 'error', message: 'Bad JSON' });
      return;
    }
    switch (msg.type) {
      case 'bind': {
        const { status, json } = await handleBind(JSON.stringify({ key: msg.key }));
        if (status === 200) {
          send(ws, { type: 'bindResult', ok: true, ...json });
          addSubscriber(json.deviceId, ws);
        } else {
          send(ws, { type: 'bindResult', ok: false, error: json.error });
        }
        break;
      }
      case 'subscribe':
        if (!msg.deviceId) { send(ws, { type: 'error', message: 'Missing deviceId' }); break; }
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
        // Straight into aedes - no MQTT round trip.
        broker.publish({
          topic: `xysk/${msg.deviceId}/command`,
          payload: Buffer.from(JSON.stringify(msg.payload)),
          qos: 1,
        }, (err) => {
          if (err) console.error('[broker] publish error:', err.message);
        });
        break;
      case 'ping':
        send(ws, { type: 'pong' });
        break;
      default:
        send(ws, { type: 'error', message: `Unknown type: ${msg.type}` });
    }
  });

  ws.on('close', () => {
    // drop this socket from every device it subscribed to
    for (const [deviceId, set] of wsSubscribers) {
      if (set.has(ws)) removeSubscriber(deviceId, ws);
    }
  });
});

httpServer.listen(HTTP_PORT, () =>
  console.log(`[http] client UI + WS gateway (/ws) + bind API on :${HTTP_PORT}`));

// ---- startup ----------------------------------------------------------------

await loadDevices();
console.log('XY-SK150 server ready.');
console.log(`  devices:    mqtt://localhost:${TCP_PORT}`);
console.log(`  client UI:  http://localhost:${HTTP_PORT}  (WS gateway at /ws)`);