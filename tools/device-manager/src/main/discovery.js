'use strict';

// discovery.js -- DM-Phase 3 device discovery.
//
// Two paths, per §8's DM-Phase 3 technical work:
//   1. mDNS browse for _covio._tcp.local (§13 A.6's frozen mDNS record).
//      Windows has no built-in mDNS resolver (§3.2's "critical Windows-
//      specific gotcha") -- this uses the bundled `multicast-dns` package
//      to send/receive raw mDNS packets over UDP 5353 directly, rather than
//      relying on the OS to resolve *.local names.
//   2. Manual add-by-IP, for networks that block mDNS multicast (§3.2's
//      documented fallback) -- see validateManualAddress() below, used by
//      the Discovery UI when the operator types an IP/hostname directly.
//
// This module only ever produces a device's ADVERTISED identity (from its
// mDNS TXT records) plus how to reach it (host/port). It never itself calls
// the device's HTTP API -- that is deviceClient.js's job.

const dns = require('multicast-dns');

const SERVICE_TYPE = '_covio._tcp.local';
const QUERY_INTERVAL_MS = 10000;
const STALE_AFTER_MS = 45000; // no response in this long -> considered stale, not removed (avoids UI flicker)

class Discovery {
  constructor() {
    this._mdns = null;
    this._queryTimer = null;
    this._devices = new Map(); // key: instance name, value: device record
    this._listeners = new Set();
  }

  // Emits the current device list (array) to every registered listener.
  onUpdate(fn) {
    this._listeners.add(fn);
    return () => this._listeners.delete(fn);
  }

  _emit() {
    const list = Array.from(this._devices.values());
    for (const fn of this._listeners) {
      try { fn(list); } catch (e) { console.error('[discovery] listener error:', e); }
    }
  }

  start() {
    if (this._mdns) return; // already running
    this._mdns = dns();
    this._mdns.on('response', (packet) => this._handleResponse(packet));
    this._mdns.on('error', (err) => console.error('[discovery] mdns error:', err.message));
    this._query();
    this._queryTimer = setInterval(() => this._query(), QUERY_INTERVAL_MS);
  }

  stop() {
    if (this._queryTimer) { clearInterval(this._queryTimer); this._queryTimer = null; }
    if (this._mdns) { this._mdns.destroy(); this._mdns = null; }
  }

  _query() {
    if (!this._mdns) return;
    this._mdns.query({ questions: [{ name: SERVICE_TYPE, type: 'PTR' }] });
    this._pruneStale();
  }

  _pruneStale() {
    const now = Date.now();
    let changed = false;
    for (const [key, dev] of this._devices) {
      const stale = now - dev.lastSeenMs > STALE_AFTER_MS;
      if (dev.stale !== stale) { dev.stale = stale; changed = true; }
    }
    if (changed) this._emit();
  }

  // Parses one mDNS response packet. A single packet may carry the PTR,
  // SRV, TXT, and A records together (common when a device responds
  // directly to a query), or they may arrive split across multiple
  // packets/known-answer-suppression rounds -- records already known for an
  // instance are preserved across calls, only overwritten by fresher data.
  _handleResponse(packet) {
    const records = [...(packet.answers || []), ...(packet.additionals || [])];

    const ptrs = records.filter((r) => r.type === 'PTR' && r.name === SERVICE_TYPE);
    for (const ptr of ptrs) {
      const instanceName = ptr.data; // e.g. "covio-abc123._covio._tcp.local"
      const srv = records.find((r) => r.type === 'SRV' && r.name === instanceName);
      const txt = records.find((r) => r.type === 'TXT' && r.name === instanceName);
      const target = srv && srv.data && srv.data.target;
      const a = target ? records.find((r) => r.type === 'A' && r.name === target) : null;

      const existing = this._devices.get(instanceName) || {};
      const txtMap = txt ? parseTxt(txt.data) : (existing.txt || {});

      const device = {
        instanceName,
        hostname: target || existing.hostname || null,
        port: (srv && srv.data && srv.data.port) || existing.port || 80,
        ip: (a && a.data) || existing.ip || null,
        hardwareId: txtMap.hardware_id || existing.hardwareId || null,
        fwVersion: txtMap.fw || existing.fwVersion || null,
        model: txtMap.model || existing.model || null,
        logicalDeviceId: txtMap.logical_device_id || existing.logicalDeviceId || '',
        txt: txtMap,
        lastSeenMs: Date.now(),
        stale: false,
        source: 'mdns',
      };
      this._devices.set(instanceName, device);
    }

    if (ptrs.length) this._emit();
  }
}

// multicast-dns's TXT record `data` is an array of Buffers, one per
// "key=value" entry (standard DNS-SD TXT encoding) -- decode into a plain
// object. Never throws on malformed entries; they are simply skipped, so
// one bad TXT entry can't take down discovery entirely.
function parseTxt(entries) {
  const out = {};
  if (!Array.isArray(entries)) return out;
  for (const entry of entries) {
    try {
      const s = Buffer.isBuffer(entry) ? entry.toString('utf8') : String(entry);
      const eq = s.indexOf('=');
      if (eq < 0) continue;
      out[s.substring(0, eq)] = s.substring(eq + 1);
    } catch (_e) { /* skip malformed entry */ }
  }
  return out;
}

// Manual add-by-IP fallback (§3.2). Only validates the *shape* of the
// input -- actually confirming a real Covio device answers there is
// deviceClient.js's job (GET /api/v1/info), invoked by the caller after
// this returns a normalized {host, port} pair.
function validateManualAddress(input) {
  const trimmed = String(input || '').trim();
  if (!trimmed) return { ok: false, error: 'Enter an IP address or hostname.' };
  // Accept "host", "host:port", bare IPv4, or IPv4:port. Reject anything
  // containing a scheme/path -- this field is a host, not a URL.
  if (/^https?:\/\//i.test(trimmed)) {
    return { ok: false, error: 'Enter just the IP or hostname, not a full URL.' };
  }
  const m = trimmed.match(/^([^\s:]+)(?::(\d+))?$/);
  if (!m) return { ok: false, error: 'That does not look like a valid IP or hostname.' };
  const port = m[2] ? parseInt(m[2], 10) : 80;
  if (port < 1 || port > 65535) return { ok: false, error: 'Port must be between 1 and 65535.' };
  return { ok: true, host: m[1], port };
}

module.exports = { Discovery, parseTxt, validateManualAddress, SERVICE_TYPE };
