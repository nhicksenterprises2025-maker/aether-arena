const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const fs = require('node:fs');
const http = require('node:http');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');

const root = process.env.RIFT_GAME_ROOT || process.env.RIFT_PROJECT_ROOT || path.resolve(__dirname, '..');
function request(port, method, route, body) {
  return new Promise((resolve, reject) => {
    const bytes = body === undefined ? null : Buffer.from(body);
    const req = http.request({host: '127.0.0.1', port, method, path: route, headers: bytes ? {'Content-Type': 'application/json', 'Content-Length': bytes.length} : {}}, res => {
      const chunks = [];
      res.on('data', chunk => chunks.push(chunk));
      res.on('end', () => resolve({status: res.statusCode, type: res.headers['content-type'], body: Buffer.concat(chunks).toString()}));
      res.on('error', error => { error.message += ' [response ' + method + ' ' + route + ' bodyBytes=' + (bytes ? bytes.length : 0) + ']'; reject(error); });
    });
    req.setTimeout(8000, () => req.destroy(new Error('HTTP timeout')));
    req.on('error', error => { error.message += ' [' + method + ' ' + route + ' bodyBytes=' + (bytes ? bytes.length : 0) + ']'; reject(error); });
    req.end(bytes);
  });
}
async function freePort() {
  const server = net.createServer();
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  await new Promise(resolve => server.close(resolve));
  return port;
}
async function test(kind) {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'rift-save-regression-'));
  const port = await freePort();
  const env = {...process.env, LOCALAPPDATA: temp, PORT: String(port)};
  const args = kind === 'node' ? [path.join(root, 'server.js')] : ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(root, 'local_server.ps1'), '-Root', root, '-Port', String(port)];
  const child = childProcess.spawn(kind === 'node' ? process.execPath : 'powershell.exe', args, {cwd: root, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']});
  let logs = '';
  child.stdout.on('data', chunk => { logs += chunk; });
  child.stderr.on('data', chunk => { logs += chunk; });
  try {
    for (let i = 0; i < 50; i++) {
      try { if ((await request(port, 'GET', '/')).status === 200) break; } catch {}
      if (child.exitCode !== null) throw new Error('Server exited: ' + logs);
      if (i === 49) throw new Error('Server did not start: ' + logs);
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    const index = await request(port, 'GET', '/');
    assert.equal(index.status, 200);
    assert.match(index.type, /text\/html/);
    const model = fs.readdirSync(path.join(root, 'assets', 'models')).find(file => file.endsWith('.glb'));
    assert.equal((await request(port, 'GET', '/assets/models/' + model)).type, 'model/gltf-binary');
    assert.deepEqual(JSON.parse((await request(port, 'GET', '/api/save')).body), {});
    let deep = {value: 'still here'};
    for (let i = 0; i < 12; i++) deep = {nested: deep};
    const deck = ['ironclad', 'ember_archer', 'archer_tower', 'boulderback', 'arc_mage', 'rambeast', 'sky_manta', 'nova_flask'];
    const deckPresets = {activePresetId: 'deck-5', presets: Array.from({length: 5}, (_, i) => ({id: 'deck-' + (i + 1), name: 'Deck ' + (i + 1), cards: deck.slice()}))};
    const body = JSON.stringify({profile: {username: 'Æ雪👑', wins: 4}, deck, deckPresets, deep, list: [null, {zero: 0}]});
    assert.equal((await request(port, 'POST', '/api/save', body)).status, 200, 'Unicode save failed');
    assert.equal((await request(port, 'GET', '/api/save')).body, body, 'JSON data changed during save');
    assert.equal((await request(port, 'POST', '/api/save', '{broken')).status, 400);
    assert.equal((await request(port, 'POST', '/api/save', '')).status, 400);
    assert.equal((await request(port, 'GET', '/api/save')).body, body, 'Invalid JSON replaced the save');
    assert.equal((await request(port, 'POST', '/api/save', JSON.stringify({large: 'x'.repeat(262144)}))).status, 413);
    assert.equal((await request(port, 'POST', '/api/save', JSON.stringify({large: '雪'.repeat(90000)}))).status, 413, 'Limit must count bytes');
    assert.equal((await request(port, 'GET', '/api/save')).body, body, 'Oversized JSON replaced the save');
    assert.deepEqual(JSON.parse((await request(port, 'GET', '/api/lab')).body), {});
    const labBody=JSON.stringify({schema:1,meta:{schema:2,datasets:[],payload:'雪'.repeat(100000)},replays:[]});
    assert.equal((await request(port,'POST','/api/lab',labBody)).status,200,'Lab supports large telemetry saves');
    assert.equal((await request(port,'GET','/api/lab')).body,labBody,'Lab JSON round trip');
    assert.equal((await request(port,'GET','/api/save')).body,body,'Lab write must preserve player save');
    assert.equal((await request(port,'POST','/api/lab','{broken')).status,400);
    assert.equal((await request(port,'POST','/api/lab',JSON.stringify({large:'x'.repeat(16777216)}))).status,413);
    assert.equal((await request(port,'GET','/api/lab')).body,labBody,'Invalid/oversized lab writes preserve history');
    assert.equal((await request(port, 'POST', '/index.html', '{}')).status, 405);
    assert.equal((await request(port, 'GET', '/..%5coutside.txt')).status, 403);
    assert.ok((await request(port, 'GET', '/%E0%A4%A')).status >= 400);
    assert.equal((await request(port, 'GET', '/')).status, 200, 'Malformed URL killed server');
    await new Promise((resolve, reject) => {
      const socket = net.connect(port, '127.0.0.1', () => {
        socket.write('POST /api/save HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\n{"half":');
        socket.destroy();
        setTimeout(resolve, 100);
      });
      socket.on('error', error => { error.message += ' [intentional aborted request]'; reject(error); });
    });
    assert.equal((await request(port, 'GET', '/')).status, 200, 'Aborted request killed server');
    assert.equal((await request(port, 'GET', '/api/save')).body, body, 'Aborted JSON replaced the save');
    const savePath = path.join(temp, 'RiftCrownArena', 'player_save.json');
    const lockCommand = "$lockedFile = [IO.File]::Open('" + savePath.replaceAll("'", "''") + "', [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read); [Console]::WriteLine('locked'); Start-Sleep -Seconds 30; $lockedFile.Dispose()";
    const lock = childProcess.spawn('powershell.exe', ['-NoProfile', '-Command', lockCommand], {windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']});
    await new Promise((resolve, reject) => { lock.stdout.once('data', resolve); lock.once('error', reject); });
    try {
      assert.equal((await request(port, 'POST', '/api/save', '{"changed":true}')).status, 500, 'Write failure needs a server error');
      assert.equal((await request(port, 'GET', '/api/save')).body, body, 'Write failure damaged the previous save');
    } finally {
      lock.kill();
      await new Promise(resolve => lock.once('exit', resolve));
    }
    assert.equal(fs.readdirSync(path.join(temp, 'RiftCrownArena')).filter(file => file.endsWith('.tmp')).length, 0);
    console.log(kind + ': static assets, UTF-8 saves, JSON preservation, invalid/oversized payloads, methods, traversal, malformed URLs, aborted requests, write failure preservation PASS');
  } finally {
    child.kill();
    await new Promise(resolve => child.once('exit', resolve));
    fs.rmSync(temp, {recursive: true, force: true});
  }
}
(async () => { await test('node'); await test('powershell'); })().catch(error => { console.error(error.message, error); process.exitCode = 1; });
