#!/usr/bin/env python3
"""
Covio bench server stub  —  the receiver half of the shared contract.

This stands in for BOTH the LCS and the Covio cloud during development. The
device cannot tell the difference; it just POSTs to whatever URL it was given.
Any real receiver (LCS/Django or cloud) must expose these same routes with the
same payloads.

Implements:
  POST /api/iot/flow/push          -> idempotent upsert, returns {ack_seq}
  GET  /api/iot/flow/config        -> {K_factor, density, T_ref, version}
  GET  /api/iot/flow/ota/manifest  -> {version, url}   (firmware pull)
  GET  /                           -> admin page: edit K-factor, view consumption
  POST /admin/kfactor              -> set K-factor, bump version

KEY DESIGN POINTS
-----------------
* Idempotency: UNIQUE(device_id, boot_id, seq). Re-sent rows are ignored, so a
  lost ACK never double-counts. (Architecture requirement.)
* Cumulative ack_seq: we return the highest CONTIGUOUS seq stored for the
  device's current boot_id. The device prunes its queue up to that seq.
* Litres are computed HERE from raw pulses / K_factor. History is stored as
  raw pulses, so editing K on the admin page recomputes all consumption with
  no device involvement — this is the whole point of server-side calibration.

ADR-001 (Self-Describing, Versioned Telemetry Schema) — schema/version rule:
  Every record in a push batch now carries "schema_version" and "record_type".
  Acceptance rule (frozen by ADR-001, no silent fallback, no guessing):
      current version        -> accept
      immediately-previous REGISTERED version -> accept
      older than previous     -> reject (quarantine)
      newer/unknown (future)  -> reject (quarantine)
  "Previous" only exists once a second schema_version has actually been
  published (i.e. once CURRENT_SCHEMA_VERSION > 1). As of Phase 1,
  CURRENT_SCHEMA_VERSION == 1 and there is no registered previous version:
  ADR-001's Migration Strategy requires any pre-ADR-001 (unversioned) device
  to be fully drained under its old firmware before this OTA — a one-time,
  one-directional cutover, NOT a dual-acceptance window. A record with no
  "schema_version" key, or any value other than a currently-registered
  version, is therefore quarantined, exactly like any other unsupported
  version. See ACR-001 (Docs/ACR-001-schema-version-zero-acceptance-window.md)
  for the compliance finding this corrected. Quarantined records are stored
  raw, never silently dropped, never crash the request, and are excluded from
  the accepted/ack-eligible set; the rest of the batch is still processed
  normally. See SCHEMA_REGISTRY.md at the repository root for the full
  registry of (schema_version, record_type) layouts.

Run:
    pip install flask
    python server.py
    # device DEFAULT_SERVER_URL should point at http://<this-host>:8000
"""
import sqlite3, json, time, os
from flask import Flask, request, jsonify, Response, send_from_directory

DB = os.path.join(os.path.dirname(__file__), "covio.db")
app = Flask(__name__)

# ---- ADR-001: schema/record-type acceptance rule (frozen, do not soften) ----
# Per ACR-001: "previous" is only a real, accepted version once a second
# schema_version has actually been registered (CURRENT > 1). At Phase 1,
# CURRENT == 1, so PREVIOUS_SCHEMA_VERSION is None and only {1} is accepted
# — the pre-ADR-001 unversioned layout is retired via forced-drain, not
# accepted here (ADR-001 Migration Strategy), matches SCHEMA_VERSION_CURRENT
# in queue.h.
CURRENT_SCHEMA_VERSION  = 1
PREVIOUS_SCHEMA_VERSION = CURRENT_SCHEMA_VERSION - 1 if CURRENT_SCHEMA_VERSION > 1 else None
ACCEPTED_SCHEMA_VERSIONS = {CURRENT_SCHEMA_VERSION} | (
    {PREVIOUS_SCHEMA_VERSION} if PREVIOUS_SCHEMA_VERSION is not None else set())

RECORD_TYPE_TELEMETRY = 1       # matches RECORD_TYPE_TELEMETRY in queue.h
KNOWN_RECORD_TYPES = {RECORD_TYPE_TELEMETRY}

# ---------------------------------------------------------------- db helpers
def db():
    c = sqlite3.connect(DB)
    c.row_factory = sqlite3.Row
    return c

def init_db():
    c = db()
    c.executescript("""
    CREATE TABLE IF NOT EXISTS records (
        device_id TEXT NOT NULL,
        boot_id   INTEGER NOT NULL,
        seq       INTEGER NOT NULL,
        ts        INTEGER NOT NULL,
        totalizer INTEGER NOT NULL,   -- raw lifetime pulses
        quality   INTEGER NOT NULL,
        rssi      INTEGER,
        recv_ms   INTEGER NOT NULL,
        kfactor_version INTEGER,
        schema_version INTEGER NOT NULL DEFAULT 0,  -- ADR-001
        record_type    INTEGER NOT NULL DEFAULT 1,  -- ADR-001 (1=TELEMETRY)
        PRIMARY KEY (device_id, seq)   -- idempotency guard: seq is GLOBALLY
                                       -- monotonic across reboots on the device,
                                       -- so (device,seq) is the true identity.
                                       -- boot_id is kept as diagnostic data.
    );
    CREATE TABLE IF NOT EXISTS calibration (
        id INTEGER PRIMARY KEY CHECK (id = 1),
        k_factor REAL NOT NULL,
        density  REAL NOT NULL,
        t_ref    REAL NOT NULL,
        version  INTEGER NOT NULL
    );
    INSERT OR IGNORE INTO calibration (id, k_factor, density, t_ref, version)
        VALUES (1, 1000.0, 0.84, 15.0, 1);   -- seed: 1000 pulses/litre, editable

    -- ADR-001: records rejected by the schema/record_type acceptance rule are
    -- quarantined here rather than silently dropped or allowed to crash the
    -- request. Never used for ack computation.
    CREATE TABLE IF NOT EXISTS quarantined_records (
        device_id      TEXT NOT NULL,
        boot_id        INTEGER,
        seq            INTEGER,
        schema_version INTEGER,
        record_type    INTEGER,
        reason         TEXT NOT NULL,
        raw_json       TEXT NOT NULL,
        recv_ms        INTEGER NOT NULL,
        PRIMARY KEY (device_id, seq, schema_version)
    );
    """)
    c.commit(); c.close()

# ------------------------------------------------------------- push endpoint
@app.route("/api/iot/flow/push", methods=["POST"])
def push():
    body = request.get_json(force=True, silent=True)
    if not body or "records" not in body:
        return jsonify(error="bad payload"), 400
    dev = body.get("device_id", "unknown")
    kver = body.get("kfactor_version", 0)
    c = db()
    now_ms = int(time.time() * 1000)
    for r in body["records"]:
        # ADR-001 acceptance rule: current + registered-previous version only
        # (see ACR-001 — there is no registered previous version until a
        # second schema_version exists). A record with no "schema_version"
        # key at all is therefore unsupported, not defaulted to an accepted
        # value. This check is per-record (not per-batch) so a batch mixing
        # current+previous is handled correctly, and one malformed/rejected
        # record never crashes or blocks the rest of the batch.
        sv = r.get("schema_version", None)
        rt = r.get("record_type", RECORD_TYPE_TELEMETRY)
        reason = None
        if sv not in ACCEPTED_SCHEMA_VERSIONS:
            reason = "unsupported_schema_version:%r" % (sv,)
        elif rt not in KNOWN_RECORD_TYPES:
            reason = "unknown_record_type:%r" % (rt,)

        if reason:
            # Quarantine, never crash, never silently drop, never fall back
            # to a guessed interpretation of the record.
            c.execute("""INSERT OR IGNORE INTO quarantined_records
                (device_id,boot_id,seq,schema_version,record_type,reason,raw_json,recv_ms)
                VALUES (?,?,?,?,?,?,?,?)""",
                (dev, r.get("boot_id"), r.get("seq"), sv, rt,
                 reason, json.dumps(r), now_ms))
            continue

        # Required fields for an accepted TELEMETRY record. A record that
        # claims an accepted schema_version/record_type but is missing a
        # required field is itself malformed -> quarantine it too, rather
        # than raising a KeyError and crashing the whole request.
        try:
            boot_id, seq, ts, totalizer = r["boot_id"], r["seq"], r["ts"], r["totalizer"]
        except KeyError as e:
            c.execute("""INSERT OR IGNORE INTO quarantined_records
                (device_id,boot_id,seq,schema_version,record_type,reason,raw_json,recv_ms)
                VALUES (?,?,?,?,?,?,?,?)""",
                (dev, r.get("boot_id"), r.get("seq"), sv, rt,
                 "missing_field:%s" % e, json.dumps(r), now_ms))
            continue

        # INSERT OR IGNORE = idempotent: duplicate (dev,boot,seq) is a no-op.
        c.execute("""INSERT OR IGNORE INTO records
            (device_id,boot_id,seq,ts,totalizer,quality,rssi,recv_ms,kfactor_version,
             schema_version,record_type)
            VALUES (?,?,?,?,?,?,?,?,?,?,?)""",
            (dev, boot_id, seq, ts, totalizer,
             r.get("quality",0), r.get("rssi",0), now_ms, kver, sv, rt))
    c.commit()

    # cumulative ack: highest CONTIGUOUS seq held for this device, across ALL
    # boots — because the device's seq never resets, only continues. Computing
    # this per-boot (expecting each boot to restart at seq 1) would freeze the
    # ack after the first reboot and the device queue would grow forever.
    rows = c.execute(
        "SELECT DISTINCT seq FROM records WHERE device_id=? ORDER BY seq",
        (dev,)).fetchall()
    contig = 0
    for row in rows:
        if row["seq"] == contig + 1:
            contig = row["seq"]
        elif row["seq"] > contig + 1:
            break
    ack_seq = contig
    c.close()
    return jsonify(ack_seq=ack_seq, server_time_ms=int(time.time()*1000))

# ------------------------------------------------------------ config endpoint
@app.route("/api/iot/flow/config", methods=["GET"])
def config():
    c = db()
    row = c.execute("SELECT * FROM calibration WHERE id=1").fetchone()
    c.close()
    return jsonify(K_factor=row["k_factor"], density=row["density"],
                   T_ref=row["t_ref"], version=row["version"])

# ------------------------------------------------------------ OTA manifest
# The manifest is a FILE you edit: server/firmware/manifest.json, e.g.
#   {"version":"1.0.1","url":"http://<host>:8000/firmware/covio_firmware.ino.bin"}
# Drop the exported .bin in server/firmware/ next to it. No manifest file =
# empty response = device sees "no update". FIELD: serve over HTTPS.
FW_DIR = os.path.join(os.path.dirname(__file__), "firmware")
os.makedirs(FW_DIR, exist_ok=True)

@app.route("/api/iot/flow/ota/manifest", methods=["GET"])
def ota_manifest():
    mf = os.path.join(FW_DIR, "manifest.json")
    if os.path.exists(mf):
        with open(mf) as f:
            return Response(f.read(), mimetype="application/json")
    return jsonify(version="", url="")     # empty => firmware treats as no-op

@app.route("/firmware/<path:fname>", methods=["GET"])
def firmware_file(fname):
    return send_from_directory(FW_DIR, fname)

# ------------------------------------------------------------ admin: K-factor
@app.route("/admin/kfactor", methods=["POST"])
def set_kfactor():
    k  = float(request.form["k_factor"])
    d  = float(request.form.get("density", 0.84))
    tr = float(request.form.get("t_ref", 15.0))
    c = db()
    c.execute("UPDATE calibration SET k_factor=?, density=?, t_ref=?, version=version+1 WHERE id=1",
              (k, d, tr))
    c.commit(); c.close()
    return ("", 303, {"Location": "/"})   # redirect back to dashboard

# ------------------------------------------------------------ admin dashboard
@app.route("/", methods=["GET"])
def dashboard():
    c = db()
    cal = c.execute("SELECT * FROM calibration WHERE id=1").fetchone()
    # consumption per device = (max totalizer - min totalizer) / K  (raw pulses!)
    rows = c.execute("""
        SELECT device_id,
               MIN(totalizer) AS lo, MAX(totalizer) AS hi, COUNT(*) AS n,
               MAX(recv_ms) AS last
        FROM records GROUP BY device_id""").fetchall()
    c.close()
    K = cal["k_factor"]
    body = f"""<html><head><title>Covio Bench Server</title>
    <style>body{{font-family:system-ui;margin:2rem;max-width:720px}}
    table{{border-collapse:collapse;width:100%}}td,th{{border:1px solid #ccc;padding:.4rem .6rem;text-align:left}}
    .card{{background:#f6f6f6;padding:1rem;border-radius:8px;margin:1rem 0}}</style></head><body>
    <h2>Covio Bench Server</h2>
    <div class="card">
      <h3>Calibration (version {cal['version']})</h3>
      <form method="POST" action="/admin/kfactor">
        K-factor (pulses/litre): <input name="k_factor" value="{K}" step="any"><br><br>
        Density: <input name="density" value="{cal['density']}" step="any">
        T_ref: <input name="t_ref" value="{cal['t_ref']}" step="any"><br><br>
        <button type="submit">Update K &amp; bump version</button>
      </form>
      <p><small>Editing K recomputes all consumption below from stored raw pulses.
      Device stamps which version was in effect; no reflash needed.</small></p>
    </div>
    <h3>Devices</h3>
    <table><tr><th>Device</th><th>Records</th><th>Raw pulses</th>
    <th>Litres (÷{K})</th><th>Last seen</th></tr>"""
    for r in rows:
        pulses = (r["hi"] or 0) - (r["lo"] or 0)
        litres = pulses / K if K else 0
        ago = int(time.time() - (r["last"] or 0)/1000)
        body += (f"<tr><td>{r['device_id']}</td><td>{r['n']}</td>"
                 f"<td>{pulses}</td><td>{litres:.3f}</td><td>{ago}s ago</td></tr>")
    body += "</table></body></html>"
    return Response(body, mimetype="text/html")

if __name__ == "__main__":
    init_db()
    print("Covio bench server on http://0.0.0.0:8000")
    app.run(host="0.0.0.0", port=8000)
