// logs.js -- §11.6 "Logs" tab. Audit fix: the original DM-Phase 3 build
// used a fully hardcoded "not yet available" stub here and never actually
// called the already-implemented, already-tested device.getLogs() API.
// §8's acceptance criteria asks for "honestly empty/'not yet available'" --
// this now confirms that LIVE against the device (§13: 501 NOT_IMPLEMENTED
// until ADR-012 exists) rather than assuming it, so if a future firmware
// revision implements ADR-012, this view picks it up with no code change.

import { el } from '../util.js';
import { renderDeviceSelector } from './_deviceSelector.js';

export function renderLogs(root, app) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Logs' }));
  const selectorHost = el('div');
  const bodyHost = el('div');
  wrap.append(selectorHost, bodyHost);
  root.appendChild(wrap);

  async function check(device) {
    bodyHost.innerHTML = '';
    if (!device.ip && !device.hostname) {
      bodyHost.appendChild(el('div', { class: 'empty-state', text: 'This device has no known address yet.' }));
      return;
    }
    bodyHost.appendChild(el('div', { class: 'notice notice-info', text: 'Checking device...' }));
    const r = await app.covio.device.getLogs(device.ip || device.hostname, device.port || 80);
    bodyHost.innerHTML = '';

    if (r.ok) {
      // Forward-compatible: once ADR-012 lands, §13 defines a real
      // {"lines":[{uptime_ms,text}, ...]} 200 response -- render it if we
      // ever actually get one, rather than assuming 501 forever.
      const lines = (r.data && r.data.lines) || [];
      if (lines.length === 0) {
        bodyHost.appendChild(el('div', { class: 'empty-state', text: 'No log lines yet.' }));
        return;
      }
      const pre = el('div', { class: 'panel mono' });
      for (const line of lines) pre.appendChild(el('div', { text: `[${line.uptime_ms}] ${line.text}` }));
      bodyHost.appendChild(pre);
      return;
    }

    if (r.error && r.error.code === 'NOT_IMPLEMENTED') {
      bodyHost.appendChild(el('div', { class: 'empty-state' }, [
        el('p', { text: 'Not yet available.' }),
        el('p', { text: r.error.message, class: 'text-small' }),
      ]));
      return;
    }
    bodyHost.appendChild(el('div', { class: 'notice notice-error', text: `Unreachable (${r.error.code}: ${r.error.message}).` }));
  }

  function onDeviceChosen(device) {
    if (!device) { bodyHost.innerHTML = ''; bodyHost.appendChild(el('div', { class: 'empty-state', text: 'Select a device above.' })); return; }
    check(device);
  }

  const stopSelector = renderDeviceSelector(selectorHost, app, onDeviceChosen);
  return stopSelector;
}
