'use strict';

// deviceStore.js -- local persistence of the known-devices list, so a
// technician's tags/location survive between app sessions. Explicitly NOT
// a database (§3.2: "it never needs direct DB access") -- a single JSON
// file under Electron's standard per-user app-data directory.
//
// SECURITY (test case 4 -- "the raw API key is never... persisted
// insecurely on the PC"): this store's schema has NO field for api_key or
// wifi_pass, and saveDevice() below only ever copies the specific
// allow-listed fields out of whatever object it's given -- even if a
// caller accidentally passed a larger object containing a secret, it could
// not leak into the persisted file. This is a deliberate allow-list, not a
// deny-list, for exactly that reason.

const fs = require('fs');
const path = require('path');

const ALLOWED_FIELDS = [
  'hardwareId', 'instanceName', 'hostname', 'ip', 'port',
  'model', 'logicalDeviceId', 'tags', 'location', 'lastKnownFwVersion', 'lastSeenMs', 'source',
];

// Audit fix: this project has repeatedly applied "version it from day one,
// cheap now, expensive to retrofit later" to every other data format it
// owns (ADR-001's wire/SD schema, §3.1a's /v1/ local API, ADR-007's NVS
// config schema) -- this local file was the one persisted format that
// didn't. A future DM-Phase 4 migration (e.g. reconciling this local list
// against a backend Device Registry) needs to be able to tell "old-format
// file, needs migration" apart from "field legitimately absent".
const SCHEMA_VERSION = 1;

class DeviceStore {
  constructor(userDataDir) {
    this._file = path.join(userDataDir, 'devices.json');
    this._data = { schemaVersion: SCHEMA_VERSION, devices: {} };
    this._load();
  }

  _load() {
    try {
      const raw = fs.readFileSync(this._file, 'utf8');
      const parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object' && parsed.devices) {
        // No prior unversioned file has ever shipped (DM-Phase 3 is this
        // store's first release), so there is nothing to migrate FROM yet
        // -- an unrecognized future version is the only real case, and is
        // handled the same safe way as a corrupt file: start fresh rather
        // than risk misinterpreting a structure this code doesn't understand.
        if (parsed.schemaVersion === SCHEMA_VERSION) {
          this._data = parsed;
        } else if (parsed.schemaVersion == null) {
          this._data = { schemaVersion: SCHEMA_VERSION, devices: parsed.devices };
        } else {
          console.error(`[deviceStore] devices.json has unrecognized schemaVersion ${parsed.schemaVersion} (expected ${SCHEMA_VERSION}) -- starting fresh rather than risk misreading it`);
        }
      }
    } catch (_e) {
      // First run, or a corrupt/missing file -- start fresh rather than crash.
      // Never overwrite an unreadable file automatically; only the next
      // explicit save() does that.
    }
  }

  _save() {
    try {
      this._data.schemaVersion = SCHEMA_VERSION;
      fs.mkdirSync(path.dirname(this._file), { recursive: true });
      fs.writeFileSync(this._file, JSON.stringify(this._data, null, 2), 'utf8');
    } catch (e) {
      console.error('[deviceStore] failed to persist devices.json:', e.message);
    }
  }

  list() {
    return Object.values(this._data.devices);
  }

  get(hardwareId) {
    return this._data.devices[hardwareId] || null;
  }

  // Merges (not replaces) so a background discovery update and a foreground
  // tag/location edit never clobber each other's fields.
  upsert(hardwareId, fields) {
    if (!hardwareId) return null;
    const existing = this._data.devices[hardwareId] || { hardwareId, tags: [], location: '' };
    const clean = {};
    for (const key of ALLOWED_FIELDS) {
      if (Object.prototype.hasOwnProperty.call(fields, key)) clean[key] = fields[key];
    }
    this._data.devices[hardwareId] = { ...existing, ...clean, hardwareId };
    this._save();
    return this._data.devices[hardwareId];
  }

  remove(hardwareId) {
    delete this._data.devices[hardwareId];
    this._save();
  }
}

module.exports = { DeviceStore, ALLOWED_FIELDS };
