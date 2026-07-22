// settings.js -- §11.6 "Settings" tab. Minimal but real: app info and
// local-data management. No cloud/account settings exist at this phase
// (Device Registry/Cloud Sync are explicitly out of scope, §8 requirement #6).

import { el } from '../util.js';

export function renderSettings(root, app) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Settings' }));

  const panel = el('div', { class: 'panel' });
  const table = el('table', { class: 'kv-table' });
  table.appendChild(el('tr', {}, [el('td', { text: 'App version' }), el('td', { id: 'settings-version', text: '...' })]));
  table.appendChild(el('tr', {}, [el('td', { text: 'Known devices' }), el('td', { text: String(app.state.list().length) })]));
  panel.appendChild(table);
  wrap.appendChild(panel);

  app.covio.app.getVersion().then((v) => { table.querySelector('#settings-version').textContent = v; });

  wrap.appendChild(el('h2', { text: 'Local Data' }));
  const dataPanel = el('div', { class: 'panel' });
  dataPanel.appendChild(el('p', {
    class: 'subtitle',
    text: 'This app stores only device identity and the tags/location you assign, on this PC. It never stores WiFi passwords or API keys.',
  }));
  const clearBtn = el('button', { class: 'secondary', text: 'Forget all known devices' });
  dataPanel.appendChild(clearBtn);
  const status = el('div');
  dataPanel.appendChild(status);
  wrap.appendChild(dataPanel);

  clearBtn.addEventListener('click', async () => {
    const confirmed = window.confirm('Remove every discovered/added device from this app? This does not change anything on the devices themselves.');
    if (!confirmed) return;
    for (const d of app.state.list()) {
      await app.covio.store.remove(d.hardwareId);
      app.state.removeDevice(d.hardwareId);
    }
    status.innerHTML = '';
    status.appendChild(el('div', { class: 'notice notice-success', text: 'Cleared.' }));
  });

  root.appendChild(wrap);
}
