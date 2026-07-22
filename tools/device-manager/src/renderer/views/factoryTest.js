// factoryTest.js -- §11.6 "Factory Test" tab / §11.7's Factory Test SOP,
// DM-Phase 6.
//
// SCOPE, STATED HONESTLY: ADR-008's factory self-test sequence (SD
// write-read-CRC, WiFi association, PCNT pulse-simulator check) is a
// Serial-only, machine-parseable PASS/FAIL the firmware runs BEFORE this
// app can see the device at all (this app has no serial-port channel --
// see §3.2, the app talks only to the firmware's local HTTP API). This view
// therefore covers the SECOND half of §11.7's flow: once a device has
// already passed its own Serial self-test and joined the factory-floor
// network, this view (1) surfaces what the local API CAN confirm
// (WiFi/SD/health/OTA -- the "Heartbeat Test"/part of the "API Test"), then
// (2) drives Logical Device ID + API key provisioning, then (3) generates
// the QR label. Never pretends to show the Serial-only checks it cannot see.
//
// SECURITY (matches DM-Phase 3's own established precedent, §8 test case 4
// "the raw API key is never shown in the UI, logs, or persisted insecurely"):
// the backend-generated api_key is used immediately to provision the device
// and is NEVER rendered to the screen -- only its status (via apiKeyPill,
// the same component every other view already uses for this exact reason).

import { el, healthPill, otaPill, apiKeyPill } from '../util.js';
import { renderDeviceSelector } from './_deviceSelector.js';

// localStorage (not deviceStore/IPC): this is a UX convenience only -- the
// factory-floor receiver's URL, not a secret and not device identity, so it
// doesn't need main-process persistence/allow-listing the way
// deviceStore.js's device records do (see that file's own header comment on
// why THOSE are IPC-only). contextIsolation/sandbox are unaffected either
// way; localStorage is a standard per-origin browser API, not a Node/
// filesystem capability.
const DEFAULT_BACKEND_URL_KEY = 'covio-factory-backend-url';

export function renderFactoryTest(root, app) {
  let device = null;
  let disposed = false;

  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Factory Test' }));
  wrap.appendChild(el('p', {
    class: 'subtitle',
    text: 'Covers the network-visible half of the §11.7 factory SOP, once a unit has already passed its own '
      + 'Serial self-test (SD/WiFi/pulse-simulator) and joined the factory-floor network. That Serial-only '
      + 'PASS/FAIL is not visible to this app -- check the Serial Monitor for it.',
  }));

  const selectorHost = el('div');
  const checksHost = el('div');
  const provisionHost = el('div');
  const qrHost = el('div');
  wrap.append(selectorHost, checksHost, provisionHost, qrHost);
  root.appendChild(wrap);

  // ---- network-visible checks ------------------------------------------------
  async function runChecks() {
    checksHost.innerHTML = '';
    if (!device) return;
    if (!device.ip && !device.hostname) {
      checksHost.appendChild(el('div', { class: 'notice notice-error', text: 'This device has no known address yet.' }));
      return;
    }
    const host = device.ip || device.hostname;
    const port = device.port || 80;
    checksHost.appendChild(el('div', { class: 'notice notice-info', text: 'Checking device...' }));

    const [infoRes, statusRes, healthRes] = await Promise.all([
      app.covio.device.getInfo(host, port),
      app.covio.device.getStatus(host, port),
      app.covio.device.getHealth(host, port),
    ]);
    if (disposed) return;
    checksHost.innerHTML = '';

    if (!infoRes.ok || !statusRes.ok) {
      checksHost.appendChild(el('div', {
        class: 'notice notice-error',
        text: `Unreachable (${(infoRes.error || statusRes.error).code}: ${(infoRes.error || statusRes.error).message}).`,
      }));
      return;
    }
    const info = infoRes.data;
    const s = statusRes.data;
    const h = healthRes.ok ? healthRes.data : null;

    const panel = el('div', { class: 'panel' });
    const table = el('table', { class: 'kv-table' });
    const rows = [
      ['Hardware ID', info.hardware_id],
      ['Existing Logical Device ID', info.logical_device_id || '(none yet)'],
      ['Model / Firmware', `${info.model} / ${info.fw_version}`],
      ['WiFi', `${s.wifi.connected ? 'connected to' : 'disconnected from'} ${s.wifi.ssid} (${s.wifi.rssi_dbm} dBm)`],
      ['SD card', s.sd_status],
      ['Health', h ? healthPill(h.health_state) : healthPill(s.health_state)],
      ['OTA', otaPill(s.ota.state)],
      ['API key', apiKeyPill(s.api_key_status)],
      ['Last sync', s.last_sync_ms_ago == null ? 'never (API Test not yet confirmed)' : `${Math.round(s.last_sync_ms_ago / 1000)}s ago`],
    ];
    for (const [label, value] of rows) {
      table.appendChild(el('tr', {}, [el('td', { text: label }), el('td', {}, typeof value === 'string' ? el('span', { text: value }) : value)]));
    }
    panel.appendChild(table);
    checksHost.appendChild(panel);

    renderProvisionForm(info);
  }

  // ---- provisioning + Logical Device ID / API key write -----------------------
  function renderProvisionForm(info) {
    provisionHost.innerHTML = '';
    qrHost.innerHTML = '';

    if (info.logical_device_id) {
      provisionHost.appendChild(el('div', {
        class: 'notice notice-success',
        text: `Already provisioned as ${info.logical_device_id}.`,
      }));
      renderQr(info.logical_device_id);
      return;
    }

    provisionHost.appendChild(el('h2', { text: 'Provision' }));
    const backendInput = el('input', {
      type: 'text',
      placeholder: 'https://factory-receiver.example.com',
      value: window.localStorage.getItem(DEFAULT_BACKEND_URL_KEY) || '',
    });
    const assetLabelInput = el('input', { type: 'text', placeholder: 'Asset label (optional)' });
    provisionHost.append(
      el('label', { text: 'Backend URL (server.py / factory receiver)' }), backendInput,
      el('label', { text: 'Asset label (optional)' }), assetLabelInput,
    );

    const status = el('div');
    const provisionBtn = el('button', { text: 'Generate ID & Provision' });
    provisionHost.appendChild(el('div', { class: 'toolbar mt-1' }, [provisionBtn]));
    provisionHost.appendChild(status);

    provisionBtn.addEventListener('click', async () => {
      status.innerHTML = '';
      const backendUrl = backendInput.value.trim();
      if (!/^https?:\/\//i.test(backendUrl)) {
        status.appendChild(el('div', { class: 'notice notice-error', text: 'Backend URL must start with http:// or https://' }));
        return;
      }
      window.localStorage.setItem(DEFAULT_BACKEND_URL_KEY, backendUrl);

      provisionBtn.disabled = true;
      status.appendChild(el('div', { class: 'notice notice-info', text: 'Requesting Logical Device ID + API key from backend...' }));

      const provRes = await app.covio.backend.provisionDevice(backendUrl, {
        device_id: info.hardware_id,
        asset_label: assetLabelInput.value.trim() || undefined,
      });
      status.innerHTML = '';
      if (!provRes.ok) {
        provisionBtn.disabled = false;
        status.appendChild(el('div', { class: 'notice notice-error', text: `Backend error (${provRes.error.code}): ${provRes.error.message}` }));
        return;
      }

      // The raw api_key is used immediately below and never rendered --
      // see this file's own header comment (matches DM-Phase 3's precedent).
      const { logical_device_id, api_key } = provRes.data;
      status.appendChild(el('div', { class: 'notice notice-info', text: `Writing ${logical_device_id} to device...` }));

      const host = device.ip || device.hostname;
      const writeRes = await app.covio.device.postFactoryProvision(host, device.port || 80, {
        logical_device_id, api_key,
      });
      status.innerHTML = '';
      provisionBtn.disabled = false;

      if (!writeRes.ok) {
        if (writeRes.error.code === 'NOT_FOUND') {
          status.appendChild(el('div', {
            class: 'notice notice-error',
            text: 'This device is not running factory-test firmware (FACTORY_TEST_BUILD=1) -- the factory write endpoint '
              + `does not exist on it. The backend already allocated ${logical_device_id}; it was NOT written to this device.`,
          }));
        } else {
          status.appendChild(el('div', { class: 'notice notice-error', text: `Device write failed (${writeRes.error.code}): ${writeRes.error.message}` }));
        }
        return;
      }

      status.appendChild(el('div', { class: 'notice notice-success', text: `Provisioned as ${logical_device_id}.` }));
      renderQr(logical_device_id);
    });
  }

  // ---- QR label ---------------------------------------------------------------
  async function renderQr(logicalDeviceId) {
    qrHost.innerHTML = '';
    qrHost.appendChild(el('h2', { text: 'QR Label' }));
    const qrRes = await app.covio.factory.generateQr(logicalDeviceId);
    if (disposed) return;
    if (!qrRes.ok) {
      qrHost.appendChild(el('div', { class: 'notice notice-error', text: `Could not generate QR code: ${qrRes.error.message}` }));
      return;
    }
    const panel = el('div', { class: 'panel qr-panel' });
    const img = el('img', { src: qrRes.dataUrl, alt: logicalDeviceId, width: '200', height: '200' });
    panel.appendChild(img);
    panel.appendChild(el('p', { text: logicalDeviceId, class: 'qr-caption' }));
    qrHost.appendChild(panel);
    const printBtn = el('button', { text: 'Print Label', onclick: () => window.print() });
    qrHost.appendChild(printBtn);
  }

  function onDeviceChosen(chosen) {
    device = chosen;
    checksHost.innerHTML = '';
    provisionHost.innerHTML = '';
    qrHost.innerHTML = '';
    if (!device) {
      checksHost.appendChild(el('div', { class: 'empty-state', text: 'Select a device above.' }));
      return;
    }
    runChecks();
  }

  const stopSelector = renderDeviceSelector(selectorHost, app, onDeviceChosen);
  const refreshBtn = el('button', { class: 'secondary', text: 'Refresh Checks', onclick: () => runChecks() });
  selectorHost.appendChild(refreshBtn);

  return () => { disposed = true; if (stopSelector) stopSelector(); };
}
