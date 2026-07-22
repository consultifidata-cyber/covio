// liveMonitor.js -- §11.6: "Live Monitor (per-device /api/v1/status+/health)".
// Polls at 5s, matching the device's own push cadence order of magnitude
// (§8 acceptance criteria: "poll cadence in the same order of magnitude as
// the device's own 5s push cadence").

import { el, healthPill, otaPill, apiKeyPill, msAgo, deviceLabel } from '../util.js';
import { renderDeviceSelector } from './_deviceSelector.js';

const POLL_MS = 5000;

export function renderLiveMonitor(root, app) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Live Monitor' }));
  const selectorHost = el('div');
  const bodyHost = el('div');
  wrap.append(selectorHost, bodyHost);
  root.appendChild(wrap);

  let timer = null;
  let stopSelector = null;

  function stopPolling() { if (timer) { clearInterval(timer); timer = null; } }

  async function poll(device) {
    if (!device.ip && !device.hostname) {
      bodyHost.innerHTML = '';
      bodyHost.appendChild(el('div', { class: 'notice notice-error', text: 'This device has no known address yet.' }));
      return;
    }
    const host = device.ip || device.hostname;
    const [statusRes, healthRes] = await Promise.all([
      app.covio.device.getStatus(host, device.port || 80),
      app.covio.device.getHealth(host, device.port || 80),
    ]);
    bodyHost.innerHTML = '';

    if (!statusRes.ok) {
      // Graceful degradation, not a crash (§8 test case 2: "a device going
      // offline mid-session degrades gracefully in the UI, no crash").
      bodyHost.appendChild(el('div', {
        class: 'notice notice-error',
        text: `Unreachable (${statusRes.error.code}: ${statusRes.error.message}). Retrying every ${POLL_MS / 1000}s...`,
      }));
      return;
    }
    const s = statusRes.data;
    const h = healthRes.ok ? healthRes.data : null;

    const panel = el('div', { class: 'panel' });
    const table = el('table', { class: 'kv-table' });
    const rows = [
      ['Health', h ? healthPill(h.health_state) : healthPill(s.health_state)],
      ['WiFi', `${s.wifi.connected ? 'connected to' : 'disconnected from'} ${s.wifi.ssid} (${s.wifi.rssi_dbm} dBm)`],
      ['Server URL', s.server_url],
      ['API key', apiKeyPill(s.api_key_status)],
      ['SD card', s.sd_status],
      ['Queue backlog', String(s.queue.backlog)],
      ['Total pulses', String(s.totalizer_raw_pulses)],
      ['Last sync', s.last_sync_ms_ago == null ? 'never' : msAgo(s.last_sync_ms_ago)],
      ['Last push HTTP code', s.last_push_http_code == null ? 'n/a' : String(s.last_push_http_code)],
      ['OTA', otaPill(s.ota.state)],
      ['Uptime', msAgo(s.uptime_ms)],
    ];
    for (const [label, value] of rows) {
      table.appendChild(el('tr', {}, [el('td', { text: label }), el('td', {}, typeof value === 'string' ? el('span', { text: value }) : value)]));
    }
    panel.appendChild(table);
    bodyHost.appendChild(panel);

    if (h && h.alarms && h.alarms.length) {
      bodyHost.appendChild(el('h2', { text: 'Alarms' }));
      const alarmPanel = el('div', { class: 'panel' });
      for (const a of h.alarms) {
        alarmPanel.appendChild(el('div', { class: 'notice notice-error' }, `${a.type} (${a.severity}): ${a.message}`));
      }
      bodyHost.appendChild(alarmPanel);
    }
  }

  function onDeviceChosen(device) {
    stopPolling();
    if (!device) {
      bodyHost.innerHTML = '';
      bodyHost.appendChild(el('div', { class: 'empty-state', text: 'Select a device above.' }));
      return;
    }
    poll(device);
    timer = setInterval(() => poll(device), POLL_MS);
  }

  stopSelector = renderDeviceSelector(selectorHost, app, onDeviceChosen);

  return () => { stopPolling(); if (stopSelector) stopSelector(); };
}
