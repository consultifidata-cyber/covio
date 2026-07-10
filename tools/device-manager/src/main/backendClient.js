'use strict';

// backendClient.js -- DM-Phase 6 (§11.7 Factory Test SOP): the app's FIRST
// connection to the backend Device Registry (server/server.py, DM-Phase 4),
// as opposed to deviceClient.js's device-facing local-API calls. Needed
// because §11.2/ADR-018's Logical Device ID is a sequential, globally-unique
// manufacturing serial that only a central registry can allocate -- no
// device can know the fleet-wide count of previously-manufactured units.
//
// The "backend base URL" this talks to is deliberately NOT a new settings
// surface: it is the exact same server_url an operator already types into
// the Provisioning wizard (provisioning.js) -- server.py serves both the
// device-facing /api/iot/flow/* routes and the /admin/* routes this file
// calls, on the same host. The Factory Test view (factoryTest.js) is where
// the operator supplies it.
//
// Runs in the main process only (same security model as deviceClient.js --
// see its own header comment); never exposed directly to the renderer.

const TIMEOUT_MS = 8000;

function normalizeBaseUrl(raw) {
  return String(raw || '').trim().replace(/\/+$/, '');
}

async function postJson(baseUrl, path, body) {
  const base = normalizeBaseUrl(baseUrl);
  if (!/^https?:\/\//i.test(base)) {
    return { ok: false, status: 0, error: { code: 'INVALID_BACKEND_URL', message: 'Backend URL must start with http:// or https://' } };
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);
  try {
    const res = await fetch(base + path, {
      method: 'POST',
      signal: controller.signal,
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const text = await res.text();
    let parsed = null;
    try { parsed = text ? JSON.parse(text) : null; } catch (_e) { /* fall through with parsed=null */ }
    if (!res.ok) {
      const err = (parsed && parsed.error) || { code: 'UNEXPECTED_RESPONSE', message: `HTTP ${res.status}` };
      return { ok: false, status: res.status, error: err };
    }
    return { ok: true, status: res.status, data: parsed };
  } catch (e) {
    const timedOut = e && e.name === 'AbortError';
    return {
      ok: false,
      status: 0,
      error: {
        code: timedOut ? 'TIMEOUT' : 'UNREACHABLE',
        message: timedOut ? `No response within ${TIMEOUT_MS}ms` : (e && e.message) || 'Request failed',
      },
    };
  } finally {
    clearTimeout(timer);
  }
}

// POST /admin/devices/provision -- DM-Phase 4, extended by DM-Phase 6 to
// also allocate/return a Logical Device ID (§11.2). device_id is the
// device's own hardware_id (from GET /api/v1/info); asset_label is optional.
// Response on success: { device_id, api_key, provisioned_at_ms, logical_device_id }.
function provisionDevice(baseUrl, { device_id, asset_label } = {}) {
  if (!device_id) {
    return Promise.resolve({ ok: false, status: 0, error: { code: 'DEVICE_ID_REQUIRED', message: 'device_id is required' } });
  }
  return postJson(baseUrl, '/admin/devices/provision', { device_id, asset_label });
}

module.exports = { provisionDevice, normalizeBaseUrl };
