'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const deviceClient = require('../src/main/deviceClient');

// Stubs global.fetch for the duration of one test, restoring afterward --
// avoids any real network I/O in this suite (these are DM-Phase 1/2's
// firmware contract shapes being exercised, not a live device).
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

test('getInfo returns parsed data on 200, matching §13 A.3 shape', async () => {
  await withFetch(
    async (url) => {
      assert.match(url, /\/api\/v1\/info$/);
      return fakeResponse(200, {
        hardware_id: 'esp32-1A2B3C4D5E6F',
        logical_device_id: null,
        asset_id: null,
        fw_version: '1.0.0',
        model: 'covio-oilflow-v1',
        boot_id: 42,
        schema_version_current: 1,
      });
    },
    async () => {
      const r = await deviceClient.getInfo('192.168.4.1', 80);
      assert.equal(r.ok, true);
      assert.equal(r.data.hardware_id, 'esp32-1A2B3C4D5E6F');
      assert.equal(r.data.logical_device_id, null);
    }
  );
});

test('getStatus surfaces the device error envelope on a non-2xx response', async () => {
  await withFetch(
    async () => fakeResponse(404, { error: { code: 'NOT_FOUND', message: 'Unknown path' } }),
    async () => {
      const r = await deviceClient.getStatus('192.168.4.1', 80);
      assert.equal(r.ok, false);
      assert.equal(r.status, 404);
      assert.equal(r.error.code, 'NOT_FOUND');
    }
  );
});

test('getLogs surfaces 501 NOT_IMPLEMENTED as a normal (non-crashing) error result', async () => {
  await withFetch(
    async () => fakeResponse(501, { error: { code: 'NOT_IMPLEMENTED', message: 'Log ring buffer requires ADR-012' } }),
    async () => {
      const r = await deviceClient.getLogs('192.168.4.1', 80);
      assert.equal(r.ok, false);
      assert.equal(r.status, 501);
      assert.equal(r.error.code, 'NOT_IMPLEMENTED');
    }
  );
});

test('a network failure (device unreachable) never throws -- returns ok:false', async () => {
  await withFetch(
    async () => { throw new Error('ECONNREFUSED'); },
    async () => {
      const r = await deviceClient.getStatus('192.168.4.1', 80);
      assert.equal(r.ok, false);
      assert.equal(r.error.code, 'UNREACHABLE');
    }
  );
});

test('an aborted (timed-out) request is reported as TIMEOUT, not a crash', async () => {
  await withFetch(
    async () => {
      const err = new Error('The operation was aborted');
      err.name = 'AbortError';
      throw err;
    },
    async () => {
      const r = await deviceClient.getMetrics('192.168.4.1', 80);
      assert.equal(r.ok, false);
      assert.equal(r.error.code, 'TIMEOUT');
    }
  );
});

test('postConfig sends exactly the four §13 A.3 fields as JSON', async () => {
  await withFetch(
    async (url, opts) => {
      assert.match(url, /\/api\/v1\/config$/);
      assert.equal(opts.method, 'POST');
      const sent = JSON.parse(opts.body);
      assert.deepEqual(Object.keys(sent).sort(), ['api_key', 'server_url', 'wifi_pass', 'wifi_ssid']);
      return fakeResponse(200, { success: true });
    },
    async () => {
      const r = await deviceClient.postConfig('192.168.4.1', 80, {
        wifi_ssid: 'MyFactoryWiFi',
        wifi_pass: 'secret',
        server_url: 'https://lcs.example.com',
        api_key: 'abc123',
      });
      assert.equal(r.ok, true);
      assert.equal(r.data.success, true);
    }
  );
});

test('postConfig surfaces CONFIG_WRITE_FORBIDDEN_NOT_IN_AP_MODE (403) unmodified', async () => {
  await withFetch(
    async () => fakeResponse(403, {
      error: {
        code: 'CONFIG_WRITE_FORBIDDEN_NOT_IN_AP_MODE',
        message: 'Config writes are only accepted while the device is in provisioning mode',
      },
    }),
    async () => {
      const r = await deviceClient.postConfig('192.168.4.1', 80, {
        wifi_ssid: 'x', wifi_pass: 'y', server_url: 'https://z', api_key: 'k',
      });
      assert.equal(r.ok, false);
      assert.equal(r.status, 403);
      assert.equal(r.error.code, 'CONFIG_WRITE_FORBIDDEN_NOT_IN_AP_MODE');
    }
  );
});

test('postConfig rejects a malformed fields argument gracefully (audit fix) instead of throwing', async () => {
  // No fetch stub needed -- this must be caught before any network call.
  const r1 = await deviceClient.postConfig('192.168.4.1', 80, null);
  assert.equal(r1.ok, false);
  assert.equal(r1.error.code, 'INVALID_REQUEST');

  const r2 = await deviceClient.postConfig('192.168.4.1', 80, 'not-an-object');
  assert.equal(r2.ok, false);
  assert.equal(r2.error.code, 'INVALID_REQUEST');
});

test('postConfig surfaces CONFIG_MISSING_FIELD (400) unmodified', async () => {
  await withFetch(
    async () => fakeResponse(400, {
      error: { code: 'CONFIG_MISSING_FIELD', message: 'all four fields are required together' },
    }),
    async () => {
      const r = await deviceClient.postConfig('192.168.4.1', 80, {
        wifi_ssid: '', wifi_pass: '', server_url: '', api_key: '',
      });
      assert.equal(r.ok, false);
      assert.equal(r.error.code, 'CONFIG_MISSING_FIELD');
    }
  );
});

// ---- DM-Phase 6: postFactoryProvision ---------------------------------------

test('postFactoryProvision posts to /api/v1/factory/provision with only logical_device_id/api_key', async () => {
  await withFetch(
    async (url, opts) => {
      assert.match(url, /\/api\/v1\/factory\/provision$/);
      assert.equal(opts.method, 'POST');
      const sent = JSON.parse(opts.body);
      assert.deepEqual(Object.keys(sent).sort(), ['api_key', 'logical_device_id']);
      return fakeResponse(200, { logical_device_id: 'COV-000123', api_key_status: 'configured' });
    },
    async () => {
      const r = await deviceClient.postFactoryProvision('192.168.4.55', 80, {
        logical_device_id: 'COV-000123',
        api_key: 'somekey',
      });
      assert.equal(r.ok, true);
      assert.equal(r.data.logical_device_id, 'COV-000123');
      // the raw api_key is never echoed back by the device, and this client
      // never fabricates it either -- only status.
      assert.equal('api_key' in r.data, false);
    }
  );
});

test('postFactoryProvision surfaces NOT_FOUND (404) unmodified -- device is not a factory-test build', async () => {
  await withFetch(
    async () => fakeResponse(404, { error: { code: 'NOT_FOUND', message: 'Unknown path' } }),
    async () => {
      const r = await deviceClient.postFactoryProvision('192.168.4.55', 80, { logical_device_id: 'COV-000123' });
      assert.equal(r.ok, false);
      assert.equal(r.status, 404);
      assert.equal(r.error.code, 'NOT_FOUND');
    }
  );
});

test('postFactoryProvision surfaces LOGICAL_ID_ALREADY_ASSIGNED (409) unmodified', async () => {
  await withFetch(
    async () => fakeResponse(409, {
      error: { code: 'LOGICAL_ID_ALREADY_ASSIGNED', message: 'this device already has a different logical_device_id' },
    }),
    async () => {
      const r = await deviceClient.postFactoryProvision('192.168.4.55', 80, { logical_device_id: 'COV-999999' });
      assert.equal(r.ok, false);
      assert.equal(r.error.code, 'LOGICAL_ID_ALREADY_ASSIGNED');
    }
  );
});

test('postFactoryProvision rejects a malformed fields argument gracefully instead of throwing', async () => {
  const r1 = await deviceClient.postFactoryProvision('192.168.4.55', 80, null);
  assert.equal(r1.ok, false);
  assert.equal(r1.error.code, 'INVALID_REQUEST');

  const r2 = await deviceClient.postFactoryProvision('192.168.4.55', 80, 'nope');
  assert.equal(r2.ok, false);
  assert.equal(r2.error.code, 'INVALID_REQUEST');
});

test('a network failure during postFactoryProvision never throws -- returns ok:false', async () => {
  await withFetch(
    async () => { throw new Error('ECONNREFUSED'); },
    async () => {
      const r = await deviceClient.postFactoryProvision('192.168.4.55', 80, { logical_device_id: 'COV-000001' });
      assert.equal(r.ok, false);
      assert.equal(r.error.code, 'UNREACHABLE');
    }
  );
});
