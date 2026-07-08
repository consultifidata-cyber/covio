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

Run:
    pip install flask
    python server.py
    # device DEFAULT_SERVER_URL should point at http://<this-host>:8000
"""
import sqlite3, json, time, os
from flask import Flask, request, jsonify, Response, send_from_directory

DB = os.path.join(os.path.dirname(__file__), "covio.db")
app = Flask(__name__)

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
    for r in body["records"]:
        # INSERT OR IGNORE = idempotent: duplicate (dev,boot,seq) is a no-op.
        c.execute("""INSERT OR IGNORE INTO records
            (device_id,boot_id,seq,ts,totalizer,quality,rssi,recv_ms,kfactor_version)
            VALUES (?,?,?,?,?,?,?,?,?)""",
            (dev, r["boot_id"], r["seq"], r["ts"], r["totalizer"],
             r.get("quality",0), r.get("rssi",0), int(time.time()*1000), kver))
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
