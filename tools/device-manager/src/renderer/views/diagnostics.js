// diagnostics.js -- §11.6: "Diagnostics (/api/v1/metrics)". Deep
// engineering diagnostics, distinct from Live Monitor's /status (§3.1a).

import { el } from '../util.js';
import { renderDeviceSelector } from './_deviceSelector.js';

const POLL_MS = 5000;

export function renderDiagnostics(root, app) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Diagnostics' }));
  wrap.appendChild(el('p', { class: 'subtitle', text: 'Deep engineering metrics -- for investigating a specific unit, not day-to-day status.' }));
  const selectorHost = el('div');
  const bodyHost = el('div');
  wrap.append(selectorHost, bodyHost);
  root.appendChild(wrap);

  let timer = null;
  function stopPolling() { if (timer) { clearInterval(timer); timer = null; } }

  async function poll(device) {
    if (!device.ip && !device.hostname) return;
    const r = await app.covio.device.getMetrics(device.ip || device.hostname, device.port || 80);
    bodyHost.innerHTML = '';
    if (!r.ok) {
      bodyHost.appendChild(el('div', { class: 'notice notice-error', text: `Unreachable (${r.error.code}: ${r.error.message}). Retrying...` }));
      return;
    }
    const m = r.data;
    const panel = el('div', { class: 'panel' });
    const table = el('table', { class: 'kv-table' });
    const rows = [
      ['Free heap', `${m.free_heap_bytes.toLocaleString()} bytes`],
      ['Heap low-water mark', `${m.heap_low_water_mark_bytes.toLocaleString()} bytes`],
      ['Reset reason', m.reset_reason],
      ['CPU frequency', `${m.cpu_freq_mhz} MHz`],
      ['Flash size', `${(m.flash_size_bytes / 1024 / 1024).toFixed(1)} MB`],
      ['SD write latency', m.sd_write_latency_ms == null ? 'n/a' : `${m.sd_write_latency_ms} ms`],
      ['Queue read latency', m.queue_read_latency_ms == null ? 'n/a' : `${m.queue_read_latency_ms} ms`],
      ['Pulse frequency', m.pulse_frequency_hz == null ? 'n/a' : `${m.pulse_frequency_hz.toFixed(2)} Hz`],
      ['Network RTT', m.network_rtt_ms == null ? 'n/a' : `${m.network_rtt_ms} ms`],
      ['RSSI history', m.rssi_history_dbm.length ? m.rssi_history_dbm.join(', ') + ' dBm' : 'no samples yet'],
      ['Temperature', m.temperature_c == null ? 'not applicable to this hardware build' : `${m.temperature_c} °C`],
    ];
    for (const [label, value] of rows) table.appendChild(el('tr', {}, [el('td', { text: label }), el('td', { text: value })]));
    panel.appendChild(table);
    bodyHost.appendChild(panel);
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
