'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { parseTxt, validateManualAddress } = require('../src/main/discovery');

test('parseTxt decodes standard DNS-SD key=value Buffer entries', () => {
  const entries = [
    Buffer.from('hardware_id=esp32-1A2B3C4D5E6F'),
    Buffer.from('fw=1.0.0'),
    Buffer.from('model=covio-oilflow-v1'),
    Buffer.from('logical_device_id='),
  ];
  const out = parseTxt(entries);
  assert.equal(out.hardware_id, 'esp32-1A2B3C4D5E6F');
  assert.equal(out.fw, '1.0.0');
  assert.equal(out.model, 'covio-oilflow-v1');
  assert.equal(out.logical_device_id, '');
});

test('parseTxt skips malformed entries without throwing', () => {
  const entries = [Buffer.from('no-equals-sign'), Buffer.from('fw=1.0.0')];
  const out = parseTxt(entries);
  assert.equal(out.fw, '1.0.0');
  assert.equal(Object.keys(out).length, 1);
});

test('parseTxt returns {} for non-array input', () => {
  assert.deepEqual(parseTxt(undefined), {});
  assert.deepEqual(parseTxt(null), {});
  assert.deepEqual(parseTxt('not-an-array'), {});
});

test('validateManualAddress accepts a bare IPv4', () => {
  const r = validateManualAddress('192.168.4.1');
  assert.equal(r.ok, true);
  assert.equal(r.host, '192.168.4.1');
  assert.equal(r.port, 80);
});

test('validateManualAddress accepts host:port', () => {
  const r = validateManualAddress('192.168.4.1:8080');
  assert.equal(r.ok, true);
  assert.equal(r.host, '192.168.4.1');
  assert.equal(r.port, 8080);
});

test('validateManualAddress accepts a mDNS/plain hostname', () => {
  const r = validateManualAddress('covio-abc123.local');
  assert.equal(r.ok, true);
  assert.equal(r.host, 'covio-abc123.local');
});

test('validateManualAddress rejects empty input', () => {
  assert.equal(validateManualAddress('').ok, false);
  assert.equal(validateManualAddress('   ').ok, false);
});

test('validateManualAddress rejects a full URL (host field, not a URL field)', () => {
  const r = validateManualAddress('http://192.168.4.1/');
  assert.equal(r.ok, false);
  assert.match(r.error, /just the IP or hostname/i);
});

test('validateManualAddress rejects an out-of-range port', () => {
  assert.equal(validateManualAddress('192.168.4.1:99999').ok, false);
  assert.equal(validateManualAddress('192.168.4.1:0').ok, false);
});
