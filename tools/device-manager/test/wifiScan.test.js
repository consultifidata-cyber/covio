'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { parseSsids } = require('../src/main/wifiScan');

test('parseSsids extracts only Covio-Setup-* networks from netsh output', () => {
  const sample = [
    'Interfaces on the system: 1',
    '',
    'There is 3 network(s) currently visible.',
    '',
    'SSID 1 : Covio-Setup-A1B2',
    '    Network type            : Infrastructure',
    '    Authentication          : Open',
    'SSID 2 : MyHomeWiFi',
    '    Network type            : Infrastructure',
    'SSID 3 : Covio-Setup-99FF',
    '    Network type            : Infrastructure',
  ].join('\r\n');

  const found = parseSsids(sample);
  assert.deepEqual(found.sort(), ['Covio-Setup-99FF', 'Covio-Setup-A1B2']);
});

test('parseSsids returns [] when nothing matches', () => {
  assert.deepEqual(parseSsids('SSID 1 : SomeOtherNetwork\r\n'), []);
});

test('parseSsids does not throw on empty or garbage input', () => {
  assert.deepEqual(parseSsids(''), []);
  assert.deepEqual(parseSsids('not netsh output at all'), []);
});

test('parseSsids de-duplicates repeated SSIDs', () => {
  const sample = 'SSID 1 : Covio-Setup-A1B2\r\nSSID 2 : Covio-Setup-A1B2\r\n';
  assert.deepEqual(parseSsids(sample), ['Covio-Setup-A1B2']);
});
