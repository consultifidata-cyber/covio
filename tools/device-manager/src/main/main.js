'use strict';

// main.js -- Electron main process entry point.
//
// SECURITY MODEL (test case 4 -- API key never shown/logged/persisted
// insecurely): nodeIntegration is OFF and contextIsolation is ON for the
// renderer. The renderer never gets direct Node, filesystem, or network
// access -- every device/network/storage operation happens here, in the
// main process, and is exposed to the renderer only through the narrow,
// explicit IPC surface below (mirrored in preload.js's contextBridge). A
// compromised/buggy renderer page cannot reach the filesystem or make
// arbitrary network requests.

const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');

const { Discovery, validateManualAddress } = require('./discovery');
const deviceClient = require('./deviceClient');
const backendClient = require('./backendClient');
const { generateQrDataUrl } = require('./qrGenerator');
const { DeviceStore } = require('./deviceStore');
const { scanNearbySetupNetworks } = require('./wifiScan');

let mainWindow = null;
let discovery = null;
let deviceStore = null;

// DM-Phase 3 error handling (§8): an unexpected error in a single IPC
// handler (e.g. a genuinely malformed response somewhere) must not crash
// the whole app out from under an operator mid-task. deviceClient.js
// already catches everything it can predict (timeouts, unreachable, bad
// JSON); this is the last-resort net for anything it can't.
process.on('uncaughtException', (err) => {
  console.error('[main] uncaught exception:', err);
});
process.on('unhandledRejection', (reason) => {
  console.error('[main] unhandled rejection:', reason);
});

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1180,
    height: 760,
    minWidth: 860,
    minHeight: 560,
    title: 'Covio Device Manager',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  mainWindow.setMenuBarVisibility(false);

  // Audit fix (Electron security checklist #12/#13): this app never links
  // to or loads remote content today, but explicitly denying navigation
  // away from the packaged app and denying new-window creation is cheap,
  // standard hardening rather than relying on that remaining true forever
  // as the app grows -- deny-by-default, not "nothing to deny yet".
  mainWindow.webContents.on('will-navigate', (event, url) => {
    if (url !== mainWindow.webContents.getURL()) event.preventDefault();
  });
  mainWindow.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));

  mainWindow.loadFile(path.join(__dirname, '..', 'renderer', 'index.html'));
}

function wireDiscoveryToRenderer() {
  discovery.onUpdate((devices) => {
    if (mainWindow && !mainWindow.isDestroyed()) {
      mainWindow.webContents.send('discovery:update', devices);
    }
  });
}

// ---- IPC surface (mirrors preload.js's exposed API 1:1) --------------------
function registerIpcHandlers() {
  ipcMain.handle('discovery:start', () => { discovery.start(); return true; });
  ipcMain.handle('discovery:stop', () => { discovery.stop(); return true; });
  ipcMain.handle('discovery:validateManualAddress', (_evt, input) => validateManualAddress(input));

  ipcMain.handle('device:getInfo', (_evt, host, port) => deviceClient.getInfo(host, port));
  ipcMain.handle('device:getStatus', (_evt, host, port) => deviceClient.getStatus(host, port));
  ipcMain.handle('device:getHealth', (_evt, host, port) => deviceClient.getHealth(host, port));
  ipcMain.handle('device:getMetrics', (_evt, host, port) => deviceClient.getMetrics(host, port));
  ipcMain.handle('device:getLogs', (_evt, host, port) => deviceClient.getLogs(host, port));
  // fields is a plain, transient object {wifi_ssid, wifi_pass, server_url,
  // api_key} passed straight through to the device over the LAN -- never
  // written to deviceStore, never logged (see deviceClient.js's own header
  // comment).
  ipcMain.handle('device:postConfig', (_evt, host, port, fields) => deviceClient.postConfig(host, port, fields));
  // DM-Phase 6 (§11.2/§11.7): factory-only write path -- fields is
  // {logical_device_id?, api_key?}, transient like postConfig's fields
  // (never written to deviceStore, never logged -- see deviceClient.js).
  ipcMain.handle('device:postFactoryProvision', (_evt, host, port, fields) => deviceClient.postFactoryProvision(host, port, fields));

  // DM-Phase 6: the app's first backend (server.py admin API) connection --
  // see backendClient.js's own header comment for why baseUrl is not a new
  // persisted setting.
  ipcMain.handle('backend:provisionDevice', (_evt, baseUrl, fields) => backendClient.provisionDevice(baseUrl, fields));

  // DM-Phase 6: QR generation happens in the main process (qrGenerator.js's
  // own header comment) -- returns a data: URL string, never exposes the
  // qrcode module itself to the renderer. Same {ok, ...}/{ok:false, error}
  // shape as every other IPC call here, so the renderer has one consistent
  // pattern to handle rather than a special-cased null-means-failure.
  ipcMain.handle('factory:generateQr', async (_evt, text) => {
    try {
      const dataUrl = await generateQrDataUrl(text);
      return { ok: true, dataUrl };
    } catch (e) {
      return { ok: false, error: { code: 'QR_GENERATION_FAILED', message: (e && e.message) || 'QR generation failed' } };
    }
  });

  ipcMain.handle('store:list', () => deviceStore.list());
  ipcMain.handle('store:upsert', (_evt, hardwareId, fields) => deviceStore.upsert(hardwareId, fields));
  ipcMain.handle('store:remove', (_evt, hardwareId) => { deviceStore.remove(hardwareId); return true; });

  ipcMain.handle('wifi:scanNearbySetupNetworks', () => scanNearbySetupNetworks());

  ipcMain.handle('app:getVersion', () => app.getVersion());
}

app.whenReady().then(() => {
  deviceStore = new DeviceStore(app.getPath('userData'));
  discovery = new Discovery();
  registerIpcHandlers();
  createWindow();
  wireDiscoveryToRenderer();
  discovery.start();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

// Explicit, synchronous teardown of the mDNS socket before the app exits --
// test case 5 ("uninstalling the app leaves no orphaned background
// processes"): this process holds no other long-lived OS resources (no
// detached child processes are ever spawned -- see wifiScan.js), so
// stopping discovery here is sufficient for a clean exit.
app.on('window-all-closed', () => {
  if (discovery) discovery.stop();
  if (process.platform !== 'darwin') app.quit();
});

app.on('before-quit', () => {
  if (discovery) discovery.stop();
});
