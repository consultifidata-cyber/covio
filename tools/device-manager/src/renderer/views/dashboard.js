// dashboard.js -- §11.6: "Dashboard (fleet-wide online/offline/alarm counts)".
// Polls every known device's /api/v1/health (§13) at the same 5s order-of-
// magnitude cadence as the device's own push cycle (§8 acceptance criteria).

import { el, healthPill } from '../util.js';

const POLL_MS = 5000;

export function renderDashboard(root, app) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Dashboard' }));
  wrap.appendChild(el('p', { class: 'subtitle', text: 'Fleet-wide status, refreshed every 5s.' }));

  const stats = el('div', { class: 'stat-row' });
  const tileOnline = statTile('0', 'Online');
  const tileDegraded = statTile('0', 'Degraded');
  const tileOffline = statTile('0', 'Offline / Unreachable');
  const tileTotal = statTile('0', 'Known Devices');
  stats.append(tileTotal.node, tileOnline.node, tileDegraded.node, tileOffline.node);
  wrap.appendChild(stats);

  wrap.appendChild(el('h2', { text: 'Active Alarms' }));
  const alarmsPanel = el('div', { class: 'panel' });
  wrap.appendChild(alarmsPanel);

  root.appendChild(wrap);

  async function refresh() {
    const devices = app.state.list();
    tileTotal.setValue(String(devices.length));

    if (devices.length === 0) {
      alarmsPanel.innerHTML = '';
      alarmsPanel.appendChild(el('div', { class: 'empty-state', text: 'No devices yet -- visit Discovery to find one on your network.' }));
      tileOnline.setValue('0'); tileDegraded.setValue('0'); tileOffline.setValue('0');
      return;
    }

    const results = await Promise.all(devices.map(async (d) => {
      if (!d.ip && !d.hostname) return { device: d, health: null, reachable: false };
      const host = d.ip || d.hostname;
      const r = await app.covio.device.getHealth(host, d.port || 80);
      return { device: d, health: r.ok ? r.data : null, reachable: r.ok };
    }));

    let online = 0, degraded = 0, offline = 0;
    const allAlarms = [];
    for (const { device, health, reachable } of results) {
      if (!reachable || !health) { offline++; continue; }
      if (health.health_state === 'ok') online++;
      else if (health.health_state === 'degraded') degraded++;
      else offline++;
      for (const alarm of health.alarms || []) allAlarms.push({ device, alarm });
    }
    tileOnline.setValue(String(online));
    tileDegraded.setValue(String(degraded));
    tileOffline.setValue(String(offline));

    alarmsPanel.innerHTML = '';
    if (allAlarms.length === 0) {
      alarmsPanel.appendChild(el('div', { class: 'empty-state', text: 'No active alarms.' }));
    } else {
      const table = el('table');
      table.appendChild(el('thead', {}, el('tr', {}, [
        el('th', { text: 'Device' }), el('th', { text: 'Type' }), el('th', { text: 'Severity' }), el('th', { text: 'Message' }),
      ])));
      const tbody = el('tbody');
      for (const { device, alarm } of allAlarms) {
        tbody.appendChild(el('tr', {}, [
          el('td', { text: device.location || device.hostname || device.hardwareId }),
          el('td', { text: alarm.type }),
          el('td', {}, el('span', { class: `pill ${alarm.severity === 'CRITICAL' ? 'pill-crit' : 'pill-warn'}`, text: alarm.severity })),
          el('td', { text: alarm.message }),
        ]));
      }
      table.appendChild(tbody);
      alarmsPanel.appendChild(table);
    }
  }

  refresh();
  const timer = setInterval(refresh, POLL_MS);
  return () => clearInterval(timer);
}

function statTile(value, label) {
  const numEl = el('div', { class: 'num', text: value });
  const node = el('div', { class: 'stat-tile' }, [numEl, el('div', { class: 'label', text: label })]);
  return { node, setValue: (v) => { numEl.textContent = v; } };
}
