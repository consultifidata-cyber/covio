'use strict';

// preload.js -- the ONLY bridge between the sandboxed renderer and the main
// process. Exposes exactly the surface main.js's IPC handlers implement,
// nothing more (in particular, no raw `ipcRenderer`, no `require`, no
// filesystem/network access of any kind is ever handed to the renderer).

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('covio', {
  discovery: {
    start: () => ipcRenderer.invoke('discovery:start'),
    stop: () => ipcRenderer.invoke('discovery:stop'),
    validateManualAddress: (input) => ipcRenderer.invoke('discovery:validateManualAddress', input),
    // Renderer subscribes to push updates; returns an unsubscribe function.
    onUpdate: (callback) => {
      const listener = (_evt, devices) => callback(devices);
      ipcRenderer.on('discovery:update', listener);
      return () => ipcRenderer.removeListener('discovery:update', listener);
    },
  },
  device: {
    getInfo: (host, port) => ipcRenderer.invoke('device:getInfo', host, port),
    getStatus: (host, port) => ipcRenderer.invoke('device:getStatus', host, port),
    getHealth: (host, port) => ipcRenderer.invoke('device:getHealth', host, port),
    getMetrics: (host, port) => ipcRenderer.invoke('device:getMetrics', host, port),
    getLogs: (host, port) => ipcRenderer.invoke('device:getLogs', host, port),
    postConfig: (host, port, fields) => ipcRenderer.invoke('device:postConfig', host, port, fields),
    postFactoryProvision: (host, port, fields) => ipcRenderer.invoke('device:postFactoryProvision', host, port, fields),
  },
  backend: {
    provisionDevice: (baseUrl, fields) => ipcRenderer.invoke('backend:provisionDevice', baseUrl, fields),
  },
  factory: {
    generateQr: (text) => ipcRenderer.invoke('factory:generateQr', text),
  },
  store: {
    list: () => ipcRenderer.invoke('store:list'),
    upsert: (hardwareId, fields) => ipcRenderer.invoke('store:upsert', hardwareId, fields),
    remove: (hardwareId) => ipcRenderer.invoke('store:remove', hardwareId),
  },
  wifi: {
    scanNearbySetupNetworks: () => ipcRenderer.invoke('wifi:scanNearbySetupNetworks'),
  },
  app: {
    getVersion: () => ipcRenderer.invoke('app:getVersion'),
  },
});
