'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const backendClient = require('../src/main/backendClient');

function withFetch(impl, fn) {
  const original = global.fetch;
  global.fetch = impl;
  return fn().finally(() => { global.fetch = original; });
}

function fakeResponse(status, jsonBody) {
  return {
    ok: status >= 200 && status < 300,
    status,
    text: async () => JSON.stringify(jsonBody),
  };
}

test('provisionDevice posts device_id/asset_label to /admin/devices/provision', async () => {
  await withFetch(
    async (url, opts) => {
      assert.equal(url, 'https://factory.example.com/admin/devices/provision');
      assert.equal(opts.method, 'POST');
      const sent = JSON.parse(opts.body);
      assert.equal(sent.device_id, 'esp32-AABBCCDDEEFF');
      assert.equal(sent.asset_label, 'Line 2 Unit 5');
      return fakeResponse(200, {
        device_id: 'esp32-AABBCCDDEEFF', api_key: 'deadbeef', provisioned_at_ms: 12345,
        logical_device_id: 'COV-000042',
      });
    },
    async () => {
      const r = await backendClient.provisionDevice('https://factory.example.com', {
        device_id: 'esp32-AABBCCDDEEFF', asset_label: 'Line 2 Unit 5',
      });
      assert.equal(r.ok, true);
      assert.equal(r.data.logical_device_id, 'COV-000042');
      assert.equal(r.data.api_key, 'deadbeef');
    }
  );
});

test('provisionDevice strips a trailing slash from the backend URL', async () => {
  await withFetch(
    async (url) => {
      assert.equal(url, 'https://factory.example.com/admin/devices/provision');
      return fakeResponse(200, { device_id: 'x', api_key: 'k', provisioned_at_ms: 1, logical_device_id: 'COV-000001' });
    },
    async () => {
      const r = await backendClient.provisionDevice('https://factory.example.com/', { device_id: 'x' });
      assert.equal(r.ok, true);
    }
  );
});

test('provisionDevice rejects a non-http(s) backend URL before any network call', async () => {
  const r = await backendClient.provisionDevice('not-a-url', { device_id: 'x' });
  assert.equal(r.ok, false);
  assert.equal(r.error.code, 'INVALID_BACKEND_URL');
});

test('provisionDevice requires device_id before any network call', async () => {
  const r = await backendClient.provisionDevice('https://factory.example.com', {});
  assert.equal(r.ok, false);
  assert.equal(r.error.code, 'DEVICE_ID_REQUIRED');
});

test('provisionDevice surfaces the backend error envelope on a non-2xx response', async () => {
  await withFetch(
    async () => fakeResponse(400, { error: { code: 'DEVICE_ID_REQUIRED', message: 'device_id is required' } }),
    async () => {
      const r = await backendClient.provisionDevice('https://factory.example.com', { device_id: 'x' });
      assert.equal(r.ok, false);
      assert.equal(r.error.code, 'DEVICE_ID_REQUIRED');
    }
  );
});

test('a network failure never throws -- returns ok:false UNREACHABLE', async () => {
  await withFetch(
    async () => { throw new Error('ECONNREFUSED'); },
    async () => {
      const r = await backendClient.provisionDevice('https://factory.example.com', { device_id: 'x' });
      assert.equal(r.ok, false);
      assert.equal(r.error.code, 'UNREACHABLE');
    }
  );
});

test('an aborted (timed-out) request is reported as TIMEOUT', async () => {
  await withFetch(
    async () => {
      const err = new Error('The operation was aborted');
      err.name = 'AbortError';
      throw err;
    },
    async () => {
      const r = await backendClient.provisionDevice('https://factory.example.com', { device_id: 'x' });
      assert.equal(r.ok, false);
      assert.equal(r.error.code, 'TIMEOUT');
    }
  );
});
