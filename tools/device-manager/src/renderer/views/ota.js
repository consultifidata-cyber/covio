// ota.js -- §11.6 "OTA" tab. Shows the CURRENT ota_state + running_version
// already available from /api/v1/status (§13) -- honestly. Triggering
// updates, release channels, and OTA history are DM-Phase 5+ scope and are
// deliberately NOT implemented here (§8 requirement #6).

import { el, otaPill } from '../util.js';
import { renderDeviceSelector } from './_deviceSelector.js';

const POLL_MS = 5000;

export function renderOta(root, app) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'OTA' }));
  wrap.appendChild(el('p', { class: 'subtitle', text: 'Current firmware update state only. Triggering updates and release channels are a later phase.' }));
  const selectorHost = el('div');
  const bodyHost = el('div');
  wrap.append(selectorHost, bodyHost);
  root.appendChild(wrap);

  let timer = null;
  function stopPolling() { if (timer) { clearInterval(timer); timer = null; } }

  async function poll(device) {
    if (!device.ip && !device.hostname) return;
    const r = await app.covio.device.getStatus(device.ip || device.hostname, device.port || 80);
    bodyHost.innerHTML = '';
    if (!r.ok) {
      bodyHost.appendChild(el('div', { class: 'notice notice-error', text: `Unreachable (${r.error.code}: ${r.error.message}). Retrying...` }));
      return;
    }
    const panel = el('div', { class: 'panel' });
    const table = el('table', { class: 'kv-table' });
    table.append(
      el('tr', {}, [el('td', { text: 'State' }), el('td', {}, otaPill(r.data.ota.state))]),
      el('tr', {}, [el('td', { text: 'Running version' }), el('td', { text: r.data.ota.running_version })]),
    );
    panel.appendChild(table);
    bodyHost.appendChild(panel);
    bodyHost.appendChild(el('div', {
      class: 'notice notice-info',
      text: 'Triggering an update, release channels, and OTA history are not yet available in this app (a later phase).',
    }));
  }

  function onDeviceChosen(device) {
    stopPolling();
    if (!device) { bodyHost.innerHTML = ''; bodyHost.appendChild(el('div', { class: 'empty-state', text: 'Select a device above.' })); return; }
    poll(device);
    timer = setInterval(() => poll(device), POLL_MS);
  }

  const stopSelector = renderDeviceSelector(selectorHost, app, onDeviceChosen);
  return () => { stopPolling(); stopSelector(); };
}
