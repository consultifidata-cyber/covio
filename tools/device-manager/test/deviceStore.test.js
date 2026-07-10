'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { DeviceStore } = require('../src/main/deviceStore');

function tmpDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'covio-devicestore-test-'));
}

test('upsert persists only allow-listed fields -- api_key/wifi_pass can never leak in', () => {
  const dir = tmpDir();
  const store = new DeviceStore(dir);
  store.upsert('esp32-AABBCCDDEEFF', {
    hostname: 'covio-eeff.local',
    ip: '192.168.1.50',
    port: 80,
    tags: ['boiler-room'],
    location: 'Site A',
    api_key: 'THIS-MUST-NEVER-BE-PERSISTED',
    wifi_pass: 'THIS-MUST-NEVER-BE-PERSISTED-EITHER',
  });

  const raw = fs.readFileSync(path.join(dir, 'devices.json'), 'utf8');
  assert.ok(!raw.includes('THIS-MUST-NEVER-BE-PERSISTED'), 'api_key leaked into devices.json');
  assert.ok(!raw.includes('wifi_pass'), 'wifi_pass field name leaked into devices.json');
  assert.ok(!raw.includes('api_key'), 'api_key field name leaked into devices.json');

  const rec = store.get('esp32-AABBCCDDEEFF');
  assert.equal(rec.hostname, 'covio-eeff.local');
  assert.equal(rec.tags[0], 'boiler-room');
  assert.equal(rec.api_key, undefined);
  assert.equal(rec.wifi_pass, undefined);
});

test('upsert merges rather than replaces -- a later background update preserves user-set tags', () => {
  const dir = tmpDir();
  const store = new DeviceStore(dir);
  store.upsert('esp32-111111111111', { hostname: 'covio-1111.local', tags: ['pump-house'] });
  store.upsert('esp32-111111111111', { lastSeenMs: 12345 }); // e.g. a discovery refresh
  const rec = store.get('esp32-111111111111');
  assert.deepEqual(rec.tags, ['pump-house']);
  assert.equal(rec.lastSeenMs, 12345);
});

test('data survives across store instances (real persistence, not just in-memory)', () => {
  const dir = tmpDir();
  new DeviceStore(dir).upsert('esp32-222222222222', { location: 'Warehouse 2' });
  const reopened = new DeviceStore(dir);
  assert.equal(reopened.get('esp32-222222222222').location, 'Warehouse 2');
});

test('a missing/corrupt devices.json does not crash the store', () => {
  const dir = tmpDir();
  fs.writeFileSync(path.join(dir, 'devices.json'), '{ not valid json');
  assert.doesNotThrow(() => {
    const store = new DeviceStore(dir);
    assert.deepEqual(store.list(), []);
  });
});

// Audit fix regression tests -- schemaVersion (Finding: no version marker
// on the persisted format, inconsistent with this project's own
// versioning philosophy applied everywhere else).
test('persisted devices.json carries a schemaVersion', () => {
  const dir = tmpDir();
  new DeviceStore(dir).upsert('esp32-333333333333', { hostname: 'x' });
  const raw = JSON.parse(fs.readFileSync(path.join(dir, 'devices.json'), 'utf8'));
  assert.equal(raw.schemaVersion, 1);
});

test('an unversioned (pre-audit-fix) file is transparently adopted as v1', () => {
  const dir = tmpDir();
  fs.writeFileSync(path.join(dir, 'devices.json'), JSON.stringify({
    devices: { 'esp32-444444444444': { hardwareId: 'esp32-444444444444', location: 'Legacy Site' } },
  }));
  const store = new DeviceStore(dir);
  assert.equal(store.get('esp32-444444444444').location, 'Legacy Site');
});

test('an unrecognized future schemaVersion starts fresh rather than misreading the structure', () => {
  const dir = tmpDir();
  fs.writeFileSync(path.join(dir, 'devices.json'), JSON.stringify({
    schemaVersion: 99,
    devices: { 'esp32-555555555555': { hardwareId: 'esp32-555555555555' } },
  }));
  const store = new DeviceStore(dir);
  assert.deepEqual(store.list(), []);
});
