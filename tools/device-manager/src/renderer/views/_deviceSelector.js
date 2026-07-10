// _deviceSelector.js -- shared "pick a device" dropdown, used by
// liveMonitor.js/diagnostics.js/ota.js so this isn't reimplemented three
// times (each of those views is otherwise per-device and needs the exact
// same selection UI). Prefixed with `_` to signal it's a shared internal
// building block, not a top-level §11.6 nav view itself.

import { el } from '../util.js';

// Renders a device <select> into `root`, pre-selecting app.state's current
// selection if any. Calls onChosen(device|null) immediately and on every
// change (including when the underlying device list updates, e.g. a new
// device is discovered while this view is open). Returns a dispose function.
export function renderDeviceSelector(root, app, onChosen) {
  const panel = el('div', { class: 'toolbar' });
  const select = el('select');
  panel.append(el('label', { text: 'Device:', class: 'label-inline' }), select);
  root.appendChild(panel);

  function currentDevice() {
    const id = select.value;
    return id ? app.state.devices.get(id) : null;
  }

  // IMPORTANT: this must never call app.state.selectDevice() itself -- this
  // function is also a subscriber of app.state (see below), and
  // selectDevice() triggers that same subscription. Only the actual user
  // `change` event below is allowed to mutate global selection state;
  // rebuildOptions() only ever manages this <select>'s own local value and
  // calls onChosen() directly, so a device-list update elsewhere (e.g. a
  // new mDNS discovery) can never cascade into a redundant/looping notify.
  function rebuildOptions() {
    const devices = app.state.list();
    const prevValue = select.value || app.state.selectedHardwareId || '';
    select.innerHTML = '';
    if (devices.length === 0) {
      select.appendChild(el('option', { value: '', text: '(no devices known yet)' }));
      onChosen(null);
      return;
    }
    for (const d of devices) {
      select.appendChild(el('option', { value: d.hardwareId, text: d.location ? `${d.location} (${d.hardwareId})` : (d.hostname || d.ip || d.hardwareId) }));
    }
    select.value = devices.some((d) => d.hardwareId === prevValue) ? prevValue : devices[0].hardwareId;
    onChosen(currentDevice());
  }

  select.addEventListener('change', () => {
    app.state.selectDevice(select.value); // genuine user action -- fine to mutate global state here
    onChosen(currentDevice());
  });

  const unsubscribe = app.state.subscribe(rebuildOptions);
  rebuildOptions();
  return unsubscribe;
}
