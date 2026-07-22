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
import hashlib, secrets, sqlite3, json, time, os
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

# ---- DM-Phase 0B (Covio_Device_Manager_Live_Readiness_Plan.md, P0 Backend
# Authentication) — bootstrap key, single definition ------------------------
# BOOTSTRAP_DEFAULT_API_KEY exists ONLY so already-running bench devices
# (every one of which still holds config.h's DEFAULT_API_KEY, because no
# per-device key has ever been issued) are not locked out the moment
# X-Api-Key enforcement goes live. It preserves backward compatibility for
# exactly that transition and for no other reason.
# It is deliberately the ONE place this value is duplicated outside
# firmware's own config.h: every auth check in this file goes through
# hash_api_key()/require_api_key() below, never a second inline copy of the
# string itself. Do not read/compare this constant anywhere except in the
# seeding call inside init_db().
# It is temporary scaffolding, not a permanent credential: DM-Phase 4 below
# now provides the mechanism to issue a real unique API key per device
# (POST /admin/devices/provision) and to revoke this shared bootstrap row
# once every bench device has been migrated off it (POST
# /admin/devices/<id>/revoke-key, called against the "legacy-default-key"
# row) -- that revocation is an operational decision for whoever runs this
# bench server, not something this code does automatically, since doing so
# unprompted would lock out every bench device still using it.
BOOTSTRAP_DEFAULT_API_KEY = "dev-key-change-me"  # == config.h's DEFAULT_API_KEY


def hash_api_key(key):
    """SHA-256 hex digest of an API key. devices.api_key_hash never stores a
    key in plaintext, even in this bench-only reference receiver."""
    return hashlib.sha256(key.encode("utf-8")).hexdigest()


def require_api_key():
    """Shared auth gate for the three device-facing cloud endpoints DM-Phase
    0B covers (push / config / ota-manifest). Must be called before any
    request body is parsed or any row is written, so a rejected request has
    zero side effect.

    Returns None if the presented X-Api-Key header is valid and not revoked.
    Otherwise returns a (response, 401) tuple ready to be returned directly
    by the caller.
    """
    key = request.headers.get("X-Api-Key")
    if not key:
        return jsonify(error={"code": "MISSING_API_KEY",
                               "message": "X-Api-Key header is required"}), 401
    c = db()
    row = c.execute(
        "SELECT device_id, revoked FROM devices WHERE api_key_hash=?",
        (hash_api_key(key),)).fetchone()
    if row is None or row["revoked"]:
        # DM-Phase 4 (§11.4 API_AUTH_FAILED): a side effect only, added
        # here rather than at each of the three call sites -- this does
        # NOT change require_api_key()'s own accept/reject decision or
        # return contract (still None on success, the exact same 401 tuple
        # on failure) that push()/config()/ota_manifest() already depend
        # on. device_id is attributed when resolvable (a revoked key is
        # still tied to a known row); an unrecognized key's device_id is
        # genuinely unknown, recorded as NULL rather than guessed.
        record_event(c, row["device_id"] if row else None, "API_AUTH_FAILED", "WARNING",
                     "Rejected request with a missing, unrecognized, or revoked API key.")
        c.commit()
        c.close()
        return jsonify(error={"code": "INVALID_API_KEY",
                               "message": "API key is unrecognized or revoked"}), 401
    c.close()
    return None

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

    -- DM-Phase 4: tracks which numbered migrations have been applied, so
    -- init_db() is safe to call on every process start (already this
    -- file's existing convention) without re-running a one-time migration
    -- -- the same "version it explicitly" discipline this project applies
    -- to every other schema it owns (ADR-001, ADR-007).
    CREATE TABLE IF NOT EXISTS schema_migrations (
        version       INTEGER PRIMARY KEY,
        applied_at_ms INTEGER NOT NULL
    );

    -- DM-Phase 4 (§11.5 Event Timeline): ONE unified, append-only table
    -- that calibration-change and provisioning/key-lifecycle events all
    -- write into as typed rows, rather than separate parallel history
    -- tables -- exactly the design §11.5 states explicitly ("implement one
    -- device_events table... rather than three parallel history
    -- mechanisms"). device_id is nullable: most events are device-specific,
    -- but a calibration change is fleet-wide in this bench server's
    -- existing single-global-K-factor model (see set_kfactor() below), so
    -- it is recorded once with device_id=NULL rather than fabricating a
    -- per-device attribution the data doesn't actually have.
    CREATE TABLE IF NOT EXISTS device_events (
        id            INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id     TEXT,
        event_type    TEXT NOT NULL,
        severity      TEXT NOT NULL,   -- INFO | WARNING | CRITICAL (§13 A.4 alarm_severity convention, reused)
        message       TEXT NOT NULL,
        detail_json   TEXT,
        created_at_ms INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_device_events_device_id ON device_events(device_id, id);

    -- DM-Phase 6 (§11.2 Logical Device ID): a tiny, generic named-counter
    -- table -- 'logical_device_id' is its first and only user today, but
    -- the shape is deliberately generic (name/value) rather than a single
    -- bare column, in case a future ID series needs the same atomic-
    -- increment mechanism without another migration.
    CREATE TABLE IF NOT EXISTS id_counters (
        name  TEXT PRIMARY KEY,
        value INTEGER NOT NULL
    );
    INSERT OR IGNORE INTO id_counters (name, value) VALUES ('logical_device_id', 0);
    """)

    _migrate_devices_table(c)
    _migrate_logical_device_id_column(c)

    # DM-Phase 0B bootstrap seed -- see BOOTSTRAP_DEFAULT_API_KEY's own
    # comment above for why this single row exists and when it goes away.
    # A parameterized INSERT (not string-interpolated into the executescript
    # above) because the value stored is a computed hash, not a literal.
    # Unaffected by the DM-Phase 4 schema change: device_id/api_key_hash are
    # still exactly what's written; the new Twin/registry columns simply
    # default to NULL for this synthetic, non-per-device row.
    c.execute(
        "INSERT OR IGNORE INTO devices (device_id, api_key_hash, revoked) "
        "VALUES (?, ?, 0)",
        ("legacy-default-key", hash_api_key(BOOTSTRAP_DEFAULT_API_KEY)))

    c.commit(); c.close()


# DM-Phase 4: bump this whenever the `devices` table's shape changes again;
# _migrate_devices_table() below runs its migration body at most once per
# version, tracked in schema_migrations.
DEVICES_SCHEMA_VERSION = 2  # 1 == DM-Phase 0B's original (device_id, api_key_hash, revoked)

# DM-Phase 6: a second, independent migration step (see _migrate_logical_device_id_column()
# below) -- kept as its own numbered version rather than bumped into
# DEVICES_SCHEMA_VERSION above, since this one is a plain ADD COLUMN (SQLite
# supports this directly, unlike DEVICES_SCHEMA_VERSION 2's PRIMARY-KEY
# change, which needed the full create/copy/drop/rename dance). Reusing the
# SAME schema_migrations table/convention, just a different, independently
# tracked version number.
DEVICES_SCHEMA_VERSION_LOGICAL_ID = 3


def _table_exists(c, name):
    return c.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (name,)
    ).fetchone() is not None


def _column_names(c, table):
    return {row["name"] for row in c.execute("PRAGMA table_info(%s)" % table).fetchall()}


def _migrate_devices_table(c):
    """DM-Phase 4: the DM-Phase 0B `devices` table (device_id, api_key_hash,
    revoked -- device_id NOT unique, existing solely to check a presented
    key) becomes the Device Registry + Twin's 'current' half (§11.3),
    device_id as the real primary key. SQLite's ALTER TABLE cannot add a
    PRIMARY KEY to an existing table, so this creates the new-shape table,
    copies every existing row forward, drops the old table, and renames --
    guarded by schema_migrations so it runs at most once, ever, and is safe
    to call on every process start (this file's existing init_db()
    convention)."""
    already = c.execute(
        "SELECT 1 FROM schema_migrations WHERE version=?", (DEVICES_SCHEMA_VERSION,)
    ).fetchone()
    if already:
        return

    needs_migration = _table_exists(c, "devices") and "asset_label" not in _column_names(c, "devices")
    fresh_install = not _table_exists(c, "devices")

    if needs_migration or fresh_install:
        # Explicit transaction boundary around the whole swap (audit fix --
        # production-readiness audit, Database migration safety / Failure
        # recovery findings): CREATE/copy/DROP/RENAME must land atomically
        # together, or not at all. Relying on the sqlite3 module's own
        # per-statement implicit-transaction heuristics (which treat some
        # DDL differently from DML across Python versions) is not
        # trustworthy enough for a step this consequential -- an interrupted
        # migration must never leave a half-renamed table behind (which
        # would crash every subsequent init_db() call with "table already
        # exists", permanently preventing the server from starting) or a
        # dropped-but-not-yet-replaced `devices` table (which would lose
        # the entire pre-existing registry). An explicit BEGIN/commit/
        # rollback here removes that ambiguity entirely and is scoped to
        # exactly the statements that must be atomic.
        c.execute("BEGIN")
        try:
            # Clean up any orphaned leftover from a previously interrupted
            # attempt at this exact migration -- without this, retrying
            # after a crash mid-migration would itself fail with "table
            # devices_new already exists" and the server could never start
            # again without manual DB surgery.
            c.execute("DROP TABLE IF EXISTS devices_new")
            c.execute("""
            CREATE TABLE devices_new (
                device_id            TEXT PRIMARY KEY,
                api_key_hash         TEXT UNIQUE,
                asset_label          TEXT,
                revoked              INTEGER NOT NULL DEFAULT 0,
                provisioned_at_ms    INTEGER,
                -- Device Twin "current" half (§11.3) -- populated only from
                -- data the existing push/config/ota_manifest wire contract
                -- actually carries (see push()/config()/ota_manifest() below).
                last_fw_version      TEXT,
                last_kfactor_version INTEGER,
                last_seen_ms         INTEGER,
                last_push_totalizer  INTEGER,
                derived_health_state TEXT,
                -- Reserved, intentionally NEVER populated by this phase: the
                -- current wire contract has no channel for a device to report
                -- these to the backend at all (they exist only in DM-Phase 1's
                -- local-only /api/v1/status). Left honestly NULL rather than
                -- fabricated, matching this project's own established
                -- precedent (e.g. temperature_c, §13 A.3).
                server_url           TEXT,
                wifi_ssid             TEXT,
                queue_backlog         INTEGER,
                ota_state              TEXT
            );
            """)

            if needs_migration:
                old_rows = c.execute("SELECT device_id, api_key_hash, revoked FROM devices").fetchall()
                carried = 0
                for row in old_rows:
                    try:
                        c.execute(
                            "INSERT INTO devices_new (device_id, api_key_hash, revoked) VALUES (?, ?, ?)",
                            (row["device_id"], row["api_key_hash"], row["revoked"]))
                        carried += 1
                    except sqlite3.IntegrityError as e:
                        # device_id had no uniqueness constraint pre-DM-Phase-4;
                        # a real conflict here is unexpected in this codebase's
                        # actual data (only the single bootstrap row exists in
                        # practice) but is logged loudly, never silently
                        # dropped, matching this file's own quarantine
                        # convention above.
                        print("[migrate] WARNING: dropped conflicting devices row "
                              "device_id=%r during DM-Phase 4 migration: %s" % (row["device_id"], e))
                if carried != len(old_rows):
                    print("[migrate] WARNING: %d of %d pre-DM-Phase-4 devices rows could not be "
                          "carried forward -- review manually if unexpected" % (len(old_rows) - carried, len(old_rows)))
                c.execute("DROP TABLE devices")

            c.execute("ALTER TABLE devices_new RENAME TO devices")
            c.commit()
        except Exception:
            c.rollback()
            raise

    c.execute("INSERT OR IGNORE INTO schema_migrations (version, applied_at_ms) VALUES (?, ?)",
              (DEVICES_SCHEMA_VERSION, int(time.time() * 1000)))


def _migrate_logical_device_id_column(c):
    """DM-Phase 6 (§11.2 / ADR-018): adds `devices.logical_device_id`, the
    manufacturing-serial identity tier ADR-018 reserves. Unlike
    DEVICES_SCHEMA_VERSION 2's migration, this one is a plain, SQLite-
    supported `ALTER TABLE ... ADD COLUMN` -- no create/copy/drop/rename
    dance needed, since adding a nullable column (not a PRIMARY KEY) is
    directly supported. A separate UNIQUE index (not an inline UNIQUE
    constraint, which ADD COLUMN cannot express) enforces no two *assigned*
    IDs ever collide, while allowing any number of NULL/unassigned rows --
    exactly ADR-018's own "null until assigned at factory commissioning"
    semantics."""
    already = c.execute(
        "SELECT 1 FROM schema_migrations WHERE version=?",
        (DEVICES_SCHEMA_VERSION_LOGICAL_ID,)).fetchone()
    if already:
        return

    if "logical_device_id" not in _column_names(c, "devices"):
        c.execute("ALTER TABLE devices ADD COLUMN logical_device_id TEXT")
    c.execute(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_devices_logical_device_id "
        "ON devices(logical_device_id)")
    c.execute("INSERT OR IGNORE INTO schema_migrations (version, applied_at_ms) VALUES (?, ?)",
              (DEVICES_SCHEMA_VERSION_LOGICAL_ID, int(time.time() * 1000)))
    c.commit()


def allocate_logical_device_id(c):
    """DM-Phase 6 (§11.2): the sole generator of Logical Device IDs --
    ADR-018's `COV-000123` format, sequential and globally unique, backed by
    id_counters' atomic increment. Uses the CALLER's own connection (same
    convention as record_event()) so allocation is part of whatever larger
    transaction is provisioning the device -- a crash between allocating and
    actually storing the ID would simply leave that counter value unused
    forever, never double-assigned (the increment itself is committed
    separately below specifically to guarantee that -- see comment inline)."""
    # Committed as its own tiny step, deliberately separate from the
    # caller's larger transaction: this guarantees the counter never
    # rewinds/repeats even if the caller's own subsequent write fails and
    # rolls back -- at worst a counter value is silently skipped (never
    # reused), which is exactly the safe failure direction for something
    # that must never collide.
    c.execute("UPDATE id_counters SET value = value + 1 WHERE name='logical_device_id'")
    c.commit()
    row = c.execute("SELECT value FROM id_counters WHERE name='logical_device_id'").fetchone()
    return "COV-%06d" % row["value"]


# ---------------------------------------------- DM-Phase 4: Twin + Events helpers
def resolve_device_id_from_key():
    """Separate, read-only lookup by the SAME presented X-Api-Key used by
    require_api_key() above -- kept as its own function (a second query)
    rather than changing require_api_key()'s own return contract, so the
    already-approved DM-Phase 0B auth check is not touched at all. Returns
    the device_id associated with the key, or None if the key is missing or
    unrecognized (callers below only ever call this after require_api_key()
    has already accepted the request, so it should always resolve in
    practice; None is handled defensively regardless)."""
    key = request.headers.get("X-Api-Key")
    if not key:
        return None
    c = db()
    row = c.execute("SELECT device_id FROM devices WHERE api_key_hash=?",
                     (hash_api_key(key),)).fetchone()
    c.close()
    return row["device_id"] if row else None


def touch_last_seen(device_id):
    """DM-Phase 4 status synchronization: updates last_seen_ms for an
    EXISTING device row only -- never creates one. A device is only ever
    added to the registry by an actual push (which carries the device's own
    claimed identity) or by an explicit /admin/devices/provision call
    (§8 acceptance criteria: "a newly commissioned device's key is unique
    and traceable to a provisioning event") -- a bare config-poll or
    OTA-manifest GET, which carries no device_id of its own, must not
    silently fabricate a registry entry."""
    if not device_id:
        return
    c = db()
    c.execute("UPDATE devices SET last_seen_ms=?, derived_health_state=? WHERE device_id=?",
              (int(time.time() * 1000), "ok", device_id))
    c.commit(); c.close()


def derive_health_state(last_seen_ms):
    """Bench-level heuristic only -- NOT the real ADR-002 health channel
    (unimplemented). Mirrors the local API's own §13 A.4 precedence
    reasoning (offline past a threshold, otherwise ok) at a coarser,
    backend-only grain, since this receiver has no visibility into queue
    backlog/SD status/alarms the device-local /api/v1/health does."""
    if last_seen_ms is None:
        return "unknown"
    age_ms = int(time.time() * 1000) - last_seen_ms
    return "offline" if age_ms > 300000 else "ok"  # 300000ms/5min, matching §13 A.4's own offline threshold


def record_event(c, device_id, event_type, severity, message, detail=None):
    """DM-Phase 4 (§11.5): the single write path into device_events --
    every event this phase produces (calibration change, provisioning, key
    rotation/revocation, auth failure) goes through this one function, not
    a separate INSERT at each call site, so the row shape can never drift
    between event types. Uses the CALLER's own connection (never opens/
    commits its own) so an event write is always atomic with whatever
    change it is recording -- never partially applied."""
    c.execute(
        "INSERT INTO device_events (device_id, event_type, severity, message, detail_json, created_at_ms) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        (device_id, event_type, severity, message,
         json.dumps(detail) if detail is not None else None,
         int(time.time() * 1000)))


def _event_to_dict(row):
    return {
        "id": row["id"],
        "device_id": row["device_id"],
        "event_type": row["event_type"],
        "severity": row["severity"],
        "message": row["message"],
        "detail": json.loads(row["detail_json"]) if row["detail_json"] else None,
        "created_at_ms": row["created_at_ms"],
    }


def generate_api_key():
    """DM-Phase 4 (§6, closes ADR-005's "unique per-device key at
    commissioning" requirement): cryptographically random and unpredictable
    -- secrets.token_hex, not the `random` module, matching this project's
    existing security-conscious posture (ADR-005) even at this bench-tool
    scale."""
    return secrets.token_hex(24)  # 192 bits

# ------------------------------------------------------------- push endpoint
@app.route("/api/iot/flow/push", methods=["POST"])
def push():
    auth_error = require_api_key()
    if auth_error:
        return auth_error
    body = request.get_json(force=True, silent=True)
    if not body or "records" not in body:
        return jsonify(error="bad payload"), 400
    dev = body.get("device_id", "unknown")
    kver = body.get("kfactor_version", 0)
    c = db()
    now_ms = int(time.time() * 1000)
    accepted_totals = []  # DM-Phase 4 Twin sync: only records that actually
                           # pass the ADR-001 acceptance rule below may feed
                           # last_push_totalizer -- a quarantined record's
                           # totalizer is unvalidated data and must never
                           # reach the Twin.
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
        if isinstance(totalizer, (int, float)):
            accepted_totals.append(totalizer)

    # DM-Phase 4 status synchronization (§8 acceptance criteria / test case
    # 3: "a device's twin 'current' row updates correctly after each
    # successful push"). Part of the SAME transaction as the records/
    # quarantine writes above (committed together below) -- either the
    # whole push's effects land atomically, or none do, on a crash mid-
    # request. Only touches fields this wire payload actually carries (fw,
    # kfactor_version, the batch's own totalizer values) -- server_url/
    # wifi_ssid/queue_backlog/ota_state stay untouched/NULL, per this
    # file's own DM-Phase 4 header note; a device that only ever sent
    # quarantined records still updates last_seen/fw here, since a
    # quarantined-but-parseable request still proves the device is alive
    # and reachable.
    fw = body.get("fw")
    last_totalizer = max(accepted_totals) if accepted_totals else None

    cur = c.execute(
        "UPDATE devices SET last_fw_version=COALESCE(?, last_fw_version), "
        "last_kfactor_version=?, last_seen_ms=?, "
        "last_push_totalizer=COALESCE(?, last_push_totalizer), derived_health_state='ok' "
        "WHERE device_id=?",
        (fw, kver, now_ms, last_totalizer, dev))
    if cur.rowcount == 0:
        # First time this device_id has ever pushed -- the registry row is
        # created here from the push's own claimed identity, independent of
        # whether it has been through /admin/devices/provision yet (it may
        # still be using the shared DM-Phase 0B bootstrap key).
        c.execute(
            "INSERT INTO devices (device_id, last_fw_version, last_kfactor_version, "
            "last_seen_ms, last_push_totalizer, derived_health_state) VALUES (?, ?, ?, ?, ?, 'ok')",
            (dev, fw, kver, now_ms, last_totalizer))

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
    auth_error = require_api_key()
    if auth_error:
        return auth_error
    # DM-Phase 4 status synchronization: a config-poll is a legitimate
    # "device is alive" signal too (the same reasoning already applied on
    # the firmware side's own last_sync_ms_ago field, §13 A.3) -- touches
    # last_seen_ms only for an EXISTING registry row (see touch_last_seen()'s
    # own docstring for why it never creates one from a bare GET).
    touch_last_seen(resolve_device_id_from_key())
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
    auth_error = require_api_key()
    if auth_error:
        return auth_error
    touch_last_seen(resolve_device_id_from_key())  # DM-Phase 4: see config()'s identical comment above
    mf = os.path.join(FW_DIR, "manifest.json")
    if os.path.exists(mf):
        with open(mf) as f:
            return Response(f.read(), mimetype="application/json")
    return jsonify(version="", url="")     # empty => firmware treats as no-op

@app.route("/firmware/<path:fname>", methods=["GET"])
def firmware_file(fname):
    return send_from_directory(FW_DIR, fname)

# ------------------------------------------------- DM-Phase 4: registration workflow
# POST /admin/devices/provision -- §6/§8: generates and returns a fresh,
# unique API key for a device, for the desktop app (DM-Phase 3) to write
# into it during AP-mode setup. This is the ONE time the raw key is ever
# returned by this server; only its hash is stored from this point on,
# matching hash_api_key()'s own existing never-store-plaintext contract.
#
# Not authenticated by device X-Api-Key (this is an operator/admin action,
# not a device request) -- and, matching this file's own existing
# /admin/kfactor endpoint, has no admin-level authentication of its own
# either. That is an already-accepted, already-documented bench/pilot-scope
# limitation (ADR-016), not something this phase invents; see this file's
# module docstring and this turn's own report for why it is not silently
# "fixed" here without a real admin-auth decision, which is outside what
# DM-Phase 4's own card asks for.
@app.route("/admin/devices/provision", methods=["POST"])
def provision_device():
    body = request.get_json(force=True, silent=True) or {}
    device_id = body.get("device_id")
    asset_label = body.get("asset_label")
    if not device_id:
        return jsonify(error={"code": "DEVICE_ID_REQUIRED", "message": "device_id is required"}), 400

    api_key = generate_api_key()
    key_hash = hash_api_key(api_key)
    now_ms = int(time.time() * 1000)

    c = db()
    # Upsert: a device may already have a registry row purely from having
    # pushed telemetry via the shared bootstrap key before ever being
    # formally provisioned (push() below populates the Twin regardless of
    # provisioning status) -- this mints/replaces ITS OWN dedicated key
    # without disturbing any Twin "current" fields already recorded.
    existing = c.execute(
        "SELECT device_id, logical_device_id FROM devices WHERE device_id=?", (device_id,)).fetchone()
    if existing:
        c.execute(
            "UPDATE devices SET api_key_hash=?, asset_label=COALESCE(?, asset_label), "
            "revoked=0, provisioned_at_ms=? WHERE device_id=?",
            (key_hash, asset_label, now_ms, device_id))
    else:
        c.execute(
            "INSERT INTO devices (device_id, api_key_hash, asset_label, revoked, provisioned_at_ms) "
            "VALUES (?, ?, ?, 0, ?)",
            (device_id, key_hash, asset_label, now_ms))

    # DM-Phase 6 (§11.2/ADR-018): allocate a Logical Device ID exactly once
    # per physical unit -- an already-provisioned device (e.g. a routine key
    # rotation/re-provisioning re-touch, not its first-ever commissioning)
    # keeps its existing ID rather than being assigned a second one, which
    # would both waste a counter value and violate ADR-018's "manufacturing
    # serial, permanent per unit" semantics.
    logical_device_id = existing["logical_device_id"] if existing else None
    if not logical_device_id:
        logical_device_id = allocate_logical_device_id(c)
        c.execute("UPDATE devices SET logical_device_id=? WHERE device_id=?",
                  (logical_device_id, device_id))

    # §8 acceptance criteria: "a newly commissioned device's key is unique
    # and traceable to a provisioning event" -- this event row is that trace.
    record_event(c, device_id, "DEVICE_PROVISIONED", "INFO",
                 "Device provisioned with a new unique API key.",
                 {"asset_label": asset_label, "logical_device_id": logical_device_id})
    c.commit(); c.close()

    return jsonify(device_id=device_id, api_key=api_key, provisioned_at_ms=now_ms,
                   logical_device_id=logical_device_id)


@app.route("/admin/devices/<device_id>/revoke-key", methods=["POST"])
def revoke_device_key(device_id):
    """§7 (API key rotation/revocation): POST /admin/devices/<id>/rotate-key
    + revoked flag -- this is the revoke half. Reuses require_api_key()'s
    already-existing, unchanged `revoked` check (DM-Phase 0B) -- test case 4:
    "revoking a key via the admin dashboard causes the next request from
    that device to be rejected"."""
    c = db()
    row = c.execute("SELECT device_id FROM devices WHERE device_id=?", (device_id,)).fetchone()
    if not row:
        c.close()
        return jsonify(error={"code": "DEVICE_NOT_FOUND", "message": "no such device_id"}), 404
    c.execute("UPDATE devices SET revoked=1 WHERE device_id=?", (device_id,))
    record_event(c, device_id, "KEY_REVOKED", "WARNING", "API key revoked via admin action.")
    c.commit(); c.close()
    return jsonify(success=True)


@app.route("/admin/devices/<device_id>/rotate-key", methods=["POST"])
def rotate_device_key(device_id):
    """§7: generates a fresh key for an already-registered device (e.g.
    suspected key compromise) without a full re-provisioning event. Per
    §7's own stated limitation: "a rotated key can only be delivered to an
    already-fielded device via a provisioning-mode re-touch" -- this
    endpoint only mints the new key server-side; getting it onto the
    physical device is the operator's job via the DM-Phase 3 desktop app's
    provisioning wizard, not this endpoint's concern."""
    c = db()
    row = c.execute("SELECT device_id FROM devices WHERE device_id=?", (device_id,)).fetchone()
    if not row:
        c.close()
        return jsonify(error={"code": "DEVICE_NOT_FOUND", "message": "no such device_id"}), 404
    api_key = generate_api_key()
    c.execute("UPDATE devices SET api_key_hash=?, revoked=0 WHERE device_id=?",
              (hash_api_key(api_key), device_id))
    record_event(c, device_id, "KEY_ROTATED", "INFO", "API key rotated via admin action.")
    c.commit(); c.close()
    return jsonify(device_id=device_id, api_key=api_key)


# ------------------------------------------------------ DM-Phase 4: event history
# §11.5 / §8 test case 2: "three consecutive K-factor edits produce a
# correct, retrievable device_events history". Both bounded (LIMIT, capped
# at 1000) -- this table is append-only and grows indefinitely on a
# long-running bench server, and an unbounded SELECT here would be a real
# performance/memory concern on that timescale even though today's
# pilot-fleet data volume makes it moot in practice.
def _parse_limit():
    # Audit fix: a non-numeric ?limit= (or any other malformed value) must
    # fall back to the default rather than raise an uncaught ValueError --
    # this is a read-only query parameter, not a value ever worth a 500 over.
    try:
        limit = int(request.args.get("limit", 200))
    except (TypeError, ValueError):
        limit = 200
    return max(1, min(limit, 1000))


@app.route("/admin/events", methods=["GET"])
def list_events():
    limit = _parse_limit()
    c = db()
    rows = c.execute(
        "SELECT id, device_id, event_type, severity, message, detail_json, created_at_ms "
        "FROM device_events ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
    c.close()
    return jsonify(events=[_event_to_dict(r) for r in rows])


@app.route("/admin/devices/<device_id>/events", methods=["GET"])
def list_device_events(device_id):
    limit = _parse_limit()
    c = db()
    rows = c.execute(
        "SELECT id, device_id, event_type, severity, message, detail_json, created_at_ms "
        "FROM device_events WHERE device_id=? ORDER BY id DESC LIMIT ?", (device_id, limit)).fetchall()
    c.close()
    return jsonify(events=[_event_to_dict(r) for r in rows])


# ------------------------------------------------------ DM-Phase 4: admin dashboard
# §6: "/admin/devices dashboard: registry + last-seen + revoke action" --
# a NEW page, distinct from the existing "/" K-factor dashboard (unchanged
# below), matching the plan's own naming exactly.
@app.route("/admin/devices", methods=["GET"])
def devices_dashboard():
    c = db()
    rows = c.execute(
        "SELECT device_id, asset_label, revoked, provisioned_at_ms, last_fw_version, "
        "last_kfactor_version, last_seen_ms, last_push_totalizer, derived_health_state, "
        "logical_device_id "
        "FROM devices ORDER BY (last_seen_ms IS NULL), last_seen_ms DESC").fetchall()
    c.close()
    now_ms = int(time.time() * 1000)

    def fmt_ago(ms):
        if ms is None:
            return "never"
        secs = int((now_ms - ms) / 1000)
        return f"{secs}s ago" if secs < 3600 else f"{secs // 3600}h ago"

    body = """<html><head><title>Covio Device Registry</title>
    <style>body{font-family:system-ui;margin:2rem;max-width:960px}
    table{border-collapse:collapse;width:100%}td,th{border:1px solid #ccc;padding:.4rem .6rem;text-align:left}
    .revoked{color:#b3261e}.ok{color:#146c2e}.offline{color:#6e7781}
    form{display:inline}button{margin-right:.3rem}</style></head><body>
    <h2>Covio Device Registry</h2>
    <p><a href="/">&larr; K-factor dashboard</a> &middot; <a href="/admin/events">Fleet event log (JSON)</a></p>
    <table><tr><th>Logical ID</th><th>Device ID</th><th>Asset Label</th><th>Status</th><th>Health</th>
    <th>Firmware</th><th>K ver</th><th>Last Seen</th><th>Provisioned</th><th>Actions</th></tr>"""
    for r in rows:
        status = "REVOKED" if r["revoked"] else "active"
        status_cls = "revoked" if r["revoked"] else "ok"
        # Audit fix (production-readiness audit, Device Twin requirements):
        # the stored derived_health_state column is written once, at
        # push/config-poll time, and is never revisited afterward -- reading
        # it directly here made every device that had ever pushed even once
        # show "ok" forever, even after going silently offline for weeks.
        # "Offline" is a property of elapsed time versus last_seen_ms, not a
        # fact that can be captured once and cached -- it must be
        # recomputed live, at read time, via derive_health_state().
        health = derive_health_state(r["last_seen_ms"])
        health_cls = "ok" if health == "ok" else ("offline" if health == "offline" else "")
        body += f"""<tr>
          <td>{r['logical_device_id'] or '-'}</td>
          <td>{r['device_id']}</td>
          <td>{r['asset_label'] or '-'}</td>
          <td class="{status_cls}">{status}</td>
          <td class="{health_cls}">{health}</td>
          <td>{r['last_fw_version'] or '-'}</td>
          <td>{r['last_kfactor_version'] if r['last_kfactor_version'] is not None else '-'}</td>
          <td>{fmt_ago(r['last_seen_ms'])}</td>
          <td>{fmt_ago(r['provisioned_at_ms']) if r['provisioned_at_ms'] else 'not provisioned'}</td>
          <td>
            <form method="POST" action="/admin/devices/{r['device_id']}/revoke-key"
                  onsubmit="return confirm('Revoke this device\\'s API key?');">
              <button type="submit">Revoke</button>
            </form>
            <a href="/admin/devices/{r['device_id']}/events">Events</a>
          </td>
        </tr>"""
    body += "</table></body></html>"
    return Response(body, mimetype="text/html")

# ------------------------------------------------------------ admin: K-factor
@app.route("/admin/kfactor", methods=["POST"])
def set_kfactor():
    k  = float(request.form["k_factor"])
    d  = float(request.form.get("density", 0.84))
    tr = float(request.form.get("t_ref", 15.0))
    c = db()
    # DM-Phase 4 (§6/ADR-010/N-04, flagged P0 in the original audit): every
    # calibration change is now attributable via device_events -- ADR-010
    # itself already names an audit trail as its own anticipated future
    # extension (§11.8: "No new ADR is required for this"), and its
    # frozen decision that the current-value `calibration` row remains a
    # fast-lookup view is unchanged, still updated exactly as before.
    # device_id=NULL: this bench server's calibration is one shared,
    # fleet-wide K-factor (unchanged by this phase), not per-device, so the
    # event is recorded once rather than attributed to a device it doesn't
    # actually belong to.
    old = c.execute("SELECT k_factor, density, t_ref, version FROM calibration WHERE id=1").fetchone()
    c.execute("UPDATE calibration SET k_factor=?, density=?, t_ref=?, version=version+1 WHERE id=1",
              (k, d, tr))
    record_event(c, None, "CALIBRATION_CHANGED", "INFO",
                 "K-factor/density/T_ref changed via admin dashboard.",
                 {"old": {"k_factor": old["k_factor"], "density": old["density"], "t_ref": old["t_ref"],
                          "version": old["version"]},
                  "new": {"k_factor": k, "density": d, "t_ref": tr, "version": old["version"] + 1}})
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
