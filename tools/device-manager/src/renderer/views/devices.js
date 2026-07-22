// devices.js -- §11.6: "Devices (searchable/filterable/taggable list)".
// §11.6 also requires the underlying data model to carry tags/location/
// firmware_version as filterable fields "from DM-Phase 3... even though a
// 3-unit pilot fleet has no practical use for filtering yet" -- built in
// here from the start rather than retrofitted later.

import { el, healthPill, offlinePill } from '../util.js';

export function renderDevices(root, app) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Devices' }));
  wrap.appendChild(el('p', { class: 'subtitle', text: 'Every device this app has ever discovered or been told about.' }));

  const toolbar = el('div', { class: 'toolbar' });
  const searchInput = el('input', { type: 'search', placeholder: 'Search by name, host, tag, or location...' });
  toolbar.appendChild(searchInput);
  wrap.appendChild(toolbar);

  const tableHost = el('div', { class: 'panel' });
  wrap.appendChild(tableHost);
  root.appendChild(wrap);

  const healthCache = new Map(); // hardwareId -> last-polled health_state (best-effort, not guaranteed fresh)

  function matches(device, q) {
    if (!q) return true;
    const hay = [device.hardwareId, device.hostname, device.ip, device.location, device.model, ...(device.tags || [])]
      .filter(Boolean).join(' ').toLowerCase();
    return hay.includes(q.toLowerCase());
  }

  function renderTable() {
    const q = searchInput.value.trim();
    const devices = app.state.list().filter((d) => matches(d, q));
    tableHost.innerHTML = '';

    if (devices.length === 0) {
      tableHost.appendChild(el('div', { class: 'empty-state', text: app.state.list().length === 0
        ? 'No devices yet -- visit Discovery to find one on your network.'
        : 'No devices match your search.' }));
      return;
    }

    const table = el('table');
    table.appendChild(el('thead', {}, el('tr', {}, [
      el('th', { text: 'Device' }), el('th', { text: 'Health' }), el('th', { text: 'Model' }),
      el('th', { text: 'Firmware' }), el('th', { text: 'Location' }), el('th', { text: 'Tags' }), el('th', { text: '' }),
    ])));
    const tbody = el('tbody');
    for (const device of devices) {
      const healthState = healthCache.get(device.hardwareId);
      const row = el('tr', { class: 'clickable' }, [
        el('td', {}, [
          el('div', { text: device.hostname || device.ip || device.hardwareId }),
          el('div', { class: 'mono text-dim-small', text: device.hardwareId }),
        ]),
        el('td', {}, healthState ? healthPill(healthState) : offlinePill()),
        el('td', { text: device.model || '-' }),
        el('td', { text: device.lastKnownFwVersion || '-' }),
        el('td', { text: device.location || '-' }),
        el('td', {}, (device.tags || []).map((t) => el('span', { class: 'tag', text: t }))),
        el('td', {}, el('button', {
          class: 'secondary', text: 'Edit',
          onclick: (evt) => { evt.stopPropagation(); editDevice(device); },
        })),
      ]);
      row.addEventListener('click', () => { app.state.selectDevice(device.hardwareId); app.navigateTo('live-monitor'); });
      tbody.appendChild(row);
    }
    table.appendChild(tbody);
    tableHost.appendChild(table);
  }

  function editDevice(device) {
    const location = window.prompt('Location (e.g. "Boiler Room 2"):', device.location || '');
    if (location === null) return;
    const tagsRaw = window.prompt('Tags, comma-separated:', (device.tags || []).join(', '));
    if (tagsRaw === null) return;
    const tags = tagsRaw.split(',').map((t) => t.trim()).filter(Boolean);
    app.state.upsertDevice(device.hardwareId, { location, tags });
    app.covio.store.upsert(device.hardwareId, { location, tags });
  }

  async function pollHealthForVisibleDevices() {
    const devices = app.state.list();
    await Promise.all(devices.map(async (d) => {
      if (!d.ip && !d.hostname) return;
      const r = await app.covio.device.getHealth(d.ip || d.hostname, d.port || 80);
      healthCache.set(d.hardwareId, r.ok ? r.data.health_state : null);
    }));
    renderTable();
  }

  searchInput.addEventListener('input', renderTable);
  const unsubscribe = app.state.subscribe(renderTable);
  renderTable();
  pollHealthForVisibleDevices();
  const timer = setInterval(pollHealthForVisibleDevices, 8000);

  return () => { clearInterval(timer); unsubscribe(); };
}
