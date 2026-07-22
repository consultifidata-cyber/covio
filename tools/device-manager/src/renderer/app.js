// app.js -- renderer bootstrap: nav (§11.6 module map), shared device
// state, and view routing. Talks to the main process ONLY through
// `window.covio` (preload.js's contextBridge surface) -- no Node, no
// direct network access here.

import { el } from './util.js';
import { renderDashboard } from './views/dashboard.js';
import { renderDevices } from './views/devices.js';
import { renderDiscovery } from './views/discovery.js';
import { renderProvisioning } from './views/provisioning.js';
import { renderLiveMonitor } from './views/liveMonitor.js';
import { renderLogs } from './views/logs.js';
import { renderDiagnostics } from './views/diagnostics.js';
import { renderOta } from './views/ota.js';
import { renderNotYetAvailable } from './views/notYetAvailable.js';
import { renderFactoryTest } from './views/factoryTest.js';
import { renderSettings } from './views/settings.js';

// §11.6's full tab list, in order. `render` receives (contentEl, app) and
// is responsible for populating contentEl; it may return a `dispose()`
// function, called when navigating away (e.g. to stop a per-device poll
// timer -- see liveMonitor.js/diagnostics.js).
const NAV = [
  { id: 'dashboard', label: 'Dashboard', render: renderDashboard },
  { id: 'devices', label: 'Devices', render: renderDevices },
  { id: 'discovery', label: 'Discovery', render: renderDiscovery },
  { id: 'provisioning', label: 'Provisioning', render: renderProvisioning },
  { id: 'live-monitor', label: 'Live Monitor', render: renderLiveMonitor },
  { id: 'logs', label: 'Logs', render: renderLogs },
  { id: 'ota', label: 'OTA', render: renderOta },
  { id: 'calibration', label: 'Calibration', render: (c) => renderNotYetAvailable(c, 'Calibration', 'Calibration governance fields are not part of the local device API yet.') },
  { id: 'diagnostics', label: 'Diagnostics', render: renderDiagnostics },
  { id: 'factory-test', label: 'Factory Test', render: renderFactoryTest },
  { id: 'settings', label: 'Settings', render: renderSettings },
];

// Shared application state + a tiny pub/sub so views re-render on change
// without needing a framework.
class AppState {
  constructor() {
    this.devices = new Map(); // hardwareId -> device record
    this.selectedHardwareId = null;
    this._listeners = new Set();
  }

  subscribe(fn) { this._listeners.add(fn); return () => this._listeners.delete(fn); }
  _notify() { for (const fn of this._listeners) fn(this); }

  upsertDevice(hardwareId, fields) {
    if (!hardwareId) return;
    const existing = this.devices.get(hardwareId) || { hardwareId, tags: [], location: '' };
    this.devices.set(hardwareId, { ...existing, ...fields, hardwareId });
    this._notify();
  }

  selectDevice(hardwareId) { this.selectedHardwareId = hardwareId; this._notify(); }
  removeDevice(hardwareId) {
    this.devices.delete(hardwareId);
    if (this.selectedHardwareId === hardwareId) this.selectedHardwareId = null;
    this._notify();
  }
  getSelected() { return this.selectedHardwareId ? this.devices.get(this.selectedHardwareId) : null; }
  list() { return Array.from(this.devices.values()); }
}

const app = { state: new AppState(), navigateTo: null, covio: window.covio };

function buildNav() {
  const navList = document.getElementById('nav-list');
  navList.innerHTML = '';
  for (const item of NAV) {
    const li = el('li', {
      class: 'nav-item',
      text: item.label,
      id: `nav-${item.id}`,
      onclick: () => app.navigateTo(item.id),
    });
    navList.appendChild(li);
  }
}

function setActiveNav(viewId) {
  for (const item of NAV) {
    const li = document.getElementById(`nav-${item.id}`);
    if (li) li.classList.toggle('active', item.id === viewId);
  }
}

function initRouter() {
  const contentEl = document.getElementById('content');
  let disposeCurrent = null;

  app.navigateTo = (viewId) => {
    const item = NAV.find((n) => n.id === viewId) || NAV[0];
    if (typeof disposeCurrent === 'function') { disposeCurrent(); disposeCurrent = null; }
    contentEl.innerHTML = '';
    setActiveNav(item.id);
    disposeCurrent = item.render(contentEl, app) || null;
  };
}

async function loadPersistedDevices() {
  const saved = await app.covio.store.list();
  for (const dev of saved) app.state.upsertDevice(dev.hardwareId, dev);
}

function wireDiscovery() {
  app.covio.discovery.onUpdate((discovered) => {
    for (const d of discovered) {
      if (!d.hardwareId) continue; // no TXT record yet (partial mDNS response) -- wait for a fuller one
      app.state.upsertDevice(d.hardwareId, {
        instanceName: d.instanceName,
        hostname: d.hostname,
        ip: d.ip,
        port: d.port,
        model: d.model,
        logicalDeviceId: d.logicalDeviceId,
        lastKnownFwVersion: d.fwVersion,
        lastSeenMs: d.lastSeenMs,
        stale: d.stale,   // surfaced in the Discovery view -- see discovery.js
        source: 'mdns',
      });
      // Persist non-secret identity/metadata only -- deviceStore.js's own
      // allow-list is the actual enforcement point, this is just the call site.
      app.covio.store.upsert(d.hardwareId, {
        instanceName: d.instanceName, hostname: d.hostname, ip: d.ip, port: d.port,
        model: d.model, logicalDeviceId: d.logicalDeviceId, lastKnownFwVersion: d.fwVersion,
        lastSeenMs: d.lastSeenMs, source: 'mdns',
      });
    }
  });
  app.covio.discovery.start();
}

// DM-Phase 3 error handling (§8): an unexpected error in one view (e.g. a
// malformed response from a device) must not leave the whole app looking
// dead/frozen with no explanation. This is a last-resort banner, not a
// substitute for the per-view graceful-degradation handling already in
// each views/*.js file (offline devices, timeouts, etc. are handled at the
// point of failure, not caught here).
function installErrorBoundary() {
  const banner = el('div', { id: 'error-banner', class: 'error-banner' });
  document.body.appendChild(banner);
  function show(message) {
    banner.textContent = `Unexpected error: ${message} (this view may not have loaded correctly -- try navigating away and back)`;
    banner.classList.add('visible');
  }
  window.addEventListener('error', (evt) => show(evt.message || 'unknown error'));
  window.addEventListener('unhandledrejection', (evt) => show((evt.reason && evt.reason.message) || String(evt.reason)));
}

async function main() {
  installErrorBoundary();
  buildNav();
  initRouter();

  const versionEl = document.getElementById('app-version');
  app.covio.app.getVersion().then((v) => { versionEl.textContent = `v${v}`; });

  await loadPersistedDevices();
  wireDiscovery();
  app.navigateTo('dashboard');
}

main();
