'use strict';

// deviceClient.js -- DM-Phase 3 device communication layer.
//
// Talks ONLY to the firmware's local HTTP API (§3.2: "The app talks only
// to the firmware's new local HTTP API... it never needs direct DB
// access"). Every path/field/error-code below is taken directly from
// Docs/Covio_Device_Manager_Live_Readiness_Plan.md §13 (Appendix A) -- that
// section is authoritative; nothing here invents a field §13 doesn't
// define.
//
// GET requests: §13 A.2's five read-only endpoints (DM-Phase 1).
// POST /api/v1/config: §13 A.3's frozen request/response shapes exactly,
//   AP-mode only (DM-Phase 2) -- this client does not and cannot know
//   ahead of time whether a given device is in AP mode; that is exactly
//   what the device's own 403 CONFIG_WRITE_FORBIDDEN_NOT_IN_AP_MODE
//   response communicates back, surfaced here unmodified.
//
// Runs in the main process only -- never exposed directly to the renderer
// (see preload.js/main.js's IPC surface). No API key or WiFi password is
// ever logged here.

const GET_TIMEOUT_MS = 4000;
const CONFIG_TIMEOUT_MS = 8000; // config writes are still a plain field-validated write (DM-Phase 2's
                                 // JSON path performs no live WiFi test), but allow more margin regardless.

function baseUrl(host, port) {
  return `http://${host}:${port || 80}`;
}

// Shared GET+JSON-parse+error-shape helper -- every read endpoint below is
// a thin wrapper over this, avoiding five near-identical copies of the
// same fetch/timeout/parse logic.
async function getJson(host, port, path, timeoutMs) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetch(baseUrl(host, port) + path, {
      method: 'GET',
      signal: controller.signal,
      headers: { Accept: 'application/json' },
    });
    const text = await res.text();
    let body = null;
    try { body = text ? JSON.parse(text) : null; } catch (_e) { /* fall through with body=null */ }
    if (!res.ok) {
      // §13 A.5: every non-2xx response is the fixed error envelope. If the
      // device somehow returned something else, report that honestly
      // rather than pretending it parsed.
      const err = (body && body.error) || { code: 'UNEXPECTED_RESPONSE', message: `HTTP ${res.status}` };
      return { ok: false, status: res.status, error: err };
    }
    return { ok: true, status: res.status, data: body };
  } catch (e) {
    const timedOut = e && e.name === 'AbortError';
    return {
      ok: false,
      status: 0,
      error: {
        code: timedOut ? 'TIMEOUT' : 'UNREACHABLE',
        message: timedOut ? `No response within ${timeoutMs}ms` : (e && e.message) || 'Request failed',
      },
    };
  } finally {
    clearTimeout(timer);
  }
}

// GET /api/v1/info -- §13 A.3.
function getInfo(host, port) {
  return getJson(host, port, '/api/v1/info', GET_TIMEOUT_MS);
}

// GET /api/v1/status -- §13 A.3.
function getStatus(host, port) {
  return getJson(host, port, '/api/v1/status', GET_TIMEOUT_MS);
}

// GET /api/v1/health -- §13 A.3.
function getHealth(host, port) {
  return getJson(host, port, '/api/v1/health', GET_TIMEOUT_MS);
}

// GET /api/v1/metrics -- §13 A.3.
function getMetrics(host, port) {
  return getJson(host, port, '/api/v1/metrics', GET_TIMEOUT_MS);
}

// GET /api/v1/logs -- §13 A.3: 501 NOT_IMPLEMENTED until ADR-012 exists.
// getJson() already surfaces that as ok:false with the device's own error
// envelope -- callers (the Logs view) show it as "not yet available", not
// as a connectivity failure, by checking error.code === 'NOT_IMPLEMENTED'.
function getLogs(host, port) {
  return getJson(host, port, '/api/v1/logs', GET_TIMEOUT_MS);
}

// POST /api/v1/config -- §13 A.3. `fields` must contain exactly
// {wifi_ssid, wifi_pass, server_url, api_key}; all four are sent together
// (the firmware itself rejects a partial write -- this client does not
// pre-validate beyond requiring the caller to supply the object shape, so
// the device's own CONFIG_MISSING_FIELD/CONFIG_INVALID_URL responses are
// always the authoritative validation result, never duplicated/guessed
// here).
async function postConfig(host, port, fields) {
  // Audit fix: every other path in this module resolves with {ok:false,...}
  // on a problem rather than throwing/rejecting -- a malformed `fields`
  // argument (defensive-only; today's one caller, provisioning.js, already
  // validates before calling) should not be the one exception to that
  // pattern.
  if (!fields || typeof fields !== 'object') {
    return { ok: false, status: 0, error: { code: 'INVALID_REQUEST', message: 'fields must be an object' } };
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), CONFIG_TIMEOUT_MS);
  try {
    const res = await fetch(baseUrl(host, port) + '/api/v1/config', {
      method: 'POST',
      signal: controller.signal,
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        wifi_ssid: fields.wifi_ssid,
        wifi_pass: fields.wifi_pass,
        server_url: fields.server_url,
        api_key: fields.api_key,
      }),
    });
    const text = await res.text();
    let body = null;
    try { body = text ? JSON.parse(text) : null; } catch (_e) { /* fall through */ }
    if (!res.ok) {
      const err = (body && body.error) || { code: 'UNEXPECTED_RESPONSE', message: `HTTP ${res.status}` };
      return { ok: false, status: res.status, error: err };
    }
    return { ok: true, status: res.status, data: body }; // { success: true } per §13
  } catch (e) {
    const timedOut = e && e.name === 'AbortError';
    return {
      ok: false,
      status: 0,
      error: {
        code: timedOut ? 'TIMEOUT' : 'UNREACHABLE',
        message: timedOut
          ? `No response within ${CONFIG_TIMEOUT_MS}ms -- the device likely rebooted to apply the new config, which is expected on success`
          : (e && e.message) || 'Request failed',
      },
    };
  } finally {
    clearTimeout(timer);
  }
}

// POST /api/v1/factory/provision -- DM-Phase 6 (§11.2/§11.7, ADR-008/ADR-018).
// Only reachable at all on a device running the factory-test firmware
// variant (FACTORY_TEST_BUILD=1, config.h) -- every other image simply
// 404s, which getJson()'s own §13 A.5 handling already surfaces honestly as
// ok:false rather than something this client needs to special-case. `fields`
// is {logical_device_id?, api_key?} -- at least one is required by the
// device itself; this client does not duplicate that validation, matching
// postConfig()'s own "the device's response is the authoritative validation
// result" convention.
async function postFactoryProvision(host, port, fields) {
  if (!fields || typeof fields !== 'object') {
    return { ok: false, status: 0, error: { code: 'INVALID_REQUEST', message: 'fields must be an object' } };
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), CONFIG_TIMEOUT_MS);
  try {
    const res = await fetch(baseUrl(host, port) + '/api/v1/factory/provision', {
      method: 'POST',
      signal: controller.signal,
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        logical_device_id: fields.logical_device_id,
        api_key: fields.api_key,
      }),
    });
    const text = await res.text();
    let body = null;
    try { body = text ? JSON.parse(text) : null; } catch (_e) { /* fall through */ }
    if (!res.ok) {
      const err = (body && body.error) || { code: 'UNEXPECTED_RESPONSE', message: `HTTP ${res.status}` };
      return { ok: false, status: res.status, error: err };
    }
    return { ok: true, status: res.status, data: body }; // { logical_device_id, api_key_status }
  } catch (e) {
    const timedOut = e && e.name === 'AbortError';
    return {
      ok: false,
      status: 0,
      error: {
        code: timedOut ? 'TIMEOUT' : 'UNREACHABLE',
        message: timedOut ? `No response within ${CONFIG_TIMEOUT_MS}ms` : (e && e.message) || 'Request failed',
      },
    };
  } finally {
    clearTimeout(timer);
  }
}

module.exports = { getInfo, getStatus, getHealth, getMetrics, getLogs, postConfig, postFactoryProvision, baseUrl };
