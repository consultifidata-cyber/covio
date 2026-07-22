'use strict';

// Exercises the real `qrcode` dependency (no mocking) -- this is a thin
// wrapper, and the thing actually worth verifying is that the real
// dependency is installed/wired correctly and produces a usable data: URL,
// not just that our own wrapper code calls a mock correctly.

const test = require('node:test');
const assert = require('node:assert/strict');
const { generateQrDataUrl } = require('../src/main/qrGenerator');

test('generateQrDataUrl produces a PNG data: URL for a Logical Device ID', async () => {
  const url = await generateQrDataUrl('COV-000123');
  assert.match(url, /^data:image\/png;base64,/);
  assert.ok(url.length > 100, 'data URL looks too short to be a real QR image');
});

test('generateQrDataUrl rejects an empty/non-string input rather than hanging or throwing synchronously', async () => {
  await assert.rejects(() => generateQrDataUrl(''));
  await assert.rejects(() => generateQrDataUrl(null));
  await assert.rejects(() => generateQrDataUrl(undefined));
});

test('different Logical Device IDs produce different QR images', async () => {
  const a = await generateQrDataUrl('COV-000001');
  const b = await generateQrDataUrl('COV-000002');
  assert.notEqual(a, b);
});
