// discovery.js (view) -- §11.6 "Discovery" tab + §3.2's manual add-by-IP
// fallback for networks that block mDNS multicast.

import { el, offlinePill } from '../util.js';

export function renderDiscovery(root, app) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Discovery' }));
  wrap.appendChild(el('p', { class: 'subtitle', text: 'Devices found via mDNS (_covio._tcp.local) on this network, live.' }));

  const listPanel = el('div', { class: 'panel' });
  wrap.appendChild(listPanel);

  wrap.appendChild(el('h2', { text: 'Add by IP address' }));
  wrap.appendChild(el('p', { class: 'subtitle', text: 'If a device does not appear above, your network may block mDNS multicast -- add it manually.' }));
  const manualPanel = el('div', { class: 'panel' });
  const manualInput = el('input', { type: 'text', placeholder: '192.168.1.50 or covio-abc123.local' });
  const manualStatus = el('div');
  const manualBtn = el('button', { text: 'Add Device' });
  manualPanel.append(
    el('div', { class: 'toolbar' }, [manualInput, manualBtn]),
    manualStatus
  );
  wrap.appendChild(manualPanel);
  root.appendChild(wrap);

  manualBtn.addEventListener('click', async () => {
    manualStatus.innerHTML = '';
    const validation = await app.covio.discovery.validateManualAddress(manualInput.value);
    if (!validation.ok) {
      manualStatus.appendChild(el('div', { class: 'notice notice-error', text: validation.error }));
      return;
    }
    manualBtn.disabled = true;
    manualStatus.appendChild(el('div', { class: 'notice notice-info', text: `Contacting ${validation.host}:${validation.port}...` }));
    const info = await app.covio.device.getInfo(validation.host, validation.port);
    manualBtn.disabled = false;
    manualStatus.innerHTML = '';
    if (!info.ok) {
      manualStatus.appendChild(el('div', {
        class: 'notice notice-error',
        text: `Could not reach a Covio device there (${info.error.code}: ${info.error.message}). Check the address and that the device is powered on.`,
      }));
      return;
    }
    app.state.upsertDevice(info.data.hardware_id, {
      hostname: validation.host, ip: validation.host, port: validation.port,
      model: info.data.model, logicalDeviceId: info.data.logical_device_id,
      lastKnownFwVersion: info.data.fw_version, lastSeenMs: Date.now(), source: 'manual',
    });
    app.covio.store.upsert(info.data.hardware_id, {
      hostname: validation.host, ip: validation.host, port: validation.port,
      model: info.data.model, logicalDeviceId: info.data.logical_device_id,
      lastKnownFwVersion: info.data.fw_version, lastSeenMs: Date.now(), source: 'manual',
    });
    manualStatus.appendChild(el('div', { class: 'notice notice-success', text: `Added ${info.data.hardware_id}. See it in Devices.` }));
    manualInput.value = '';
  });

  function renderList() {
    const devices = app.state.list().filter((d) => d.source === 'mdns' || d.source === 'manual');
    listPanel.innerHTML = '';
    if (devices.length === 0) {
      listPanel.appendChild(el('div', { class: 'empty-state', text: 'Searching for devices... this can take up to 10s after the app starts.' }));
      return;
    }
    const table = el('table');
    table.appendChild(el('thead', {}, el('tr', {}, [
      el('th', { text: 'Device' }), el('th', { text: 'Address' }), el('th', { text: 'Model' }), el('th', { text: 'Firmware' }),
      el('th', { text: 'Last Seen' }), el('th', { text: '' }),
    ])));
    const tbody = el('tbody');
    for (const d of devices) {
      tbody.appendChild(el('tr', {}, [
        el('td', { text: d.hardwareId }),
        el('td', { class: 'mono', text: `${d.hostname || d.ip}${d.port && d.port !== 80 ? ':' + d.port : ''}` }),
        el('td', { text: d.model || '-' }),
        el('td', { text: d.lastKnownFwVersion || '-' }),
        el('td', { text: d.lastSeenMs ? new Date(d.lastSeenMs).toLocaleTimeString() : '-' }),
        // §8 test case 2's spirit extended to Discovery: a device that
        // stopped responding to mDNS is shown as such, not silently
        // dropped from the list (avoids UI flicker on a brief mDNS miss).
        el('td', {}, d.stale ? offlinePill() : null),
      ]));
    }
    table.appendChild(tbody);
    listPanel.appendChild(table);
  }

  const unsubscribe = app.state.subscribe(renderList);
  renderList();
  return unsubscribe;
}
