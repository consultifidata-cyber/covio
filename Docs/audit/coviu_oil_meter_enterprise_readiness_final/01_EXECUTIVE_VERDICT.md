# 01 — Executive Verdict: Full Enterprise Device Readiness Re-Audit

Companion documents: `02` through `20` in this same directory. This
audit independently re-checked prior claims against current source
code, current git commit, current compiled firmware, the current
flashed device (real ESP32-S3, `esp32-F4E5B2858428`), current
configuration, and current server/database behavior — treating every
earlier report as a lead, not proof, per this mandate's own instruction.

## The single most important finding

**Live, this session, not assumed**: the device's `server_url` still
reads `http://192.168.1.3:8000` — the bench workstation's own address.
Per this mandate's own mandatory rule, this alone forces a plant
No-Go for the device in its current, unmodified state. The fix is a
five-minute serial-console reprovisioning step (doc 19), not an
architectural problem — but it has not been done, and this report does
not pretend otherwise.

## What this audit proved fresh, this session (not carried over as
assumption)

- Build identity: source commit, compiled binary, flashed binary, and
  live `/api/v1/info` all match exactly — **no `BUILD IDENTITY
  MISMATCH`**.
- Server-side exactly-once storage: **proven live** — 3 identical HTTP
  POSTs produced exactly 1 database row.
- Sequence-gap handling: **proven live** — a deliberately-skipped
  sequence correctly halted the ack, then correctly resolved once
  filled, via real HTTP calls against the real running server (not just
  the pre-existing unit test's in-process client).
- Malformed/empty request bodies: **proven live** — rejected gracefully,
  server remained healthy.
- Real partition-table math: LittleFS queue partition is genuinely
  3,538,944 bytes (read directly from the actual installed CSV, not a
  code comment) — theoretical ceiling ~98,304 rows / ~1.14 days at this
  firmware's real 1-second sampling rate. **"Several days" of offline
  buffering is NOT achievable as currently configured** — a real,
  quantified gap, not a comfortable assumption.
- Security config: **secure boot and flash encryption are both
  confirmed OFF** (read directly from the installed sdkconfig); the
  serial console's `show` command leaks the raw API key with zero
  authentication (confirmed by code read).
- A private-key tracking scare was investigated with five independent
  methods and resolved as a false alarm, not silently dropped either
  way — demonstrating the audit's own diligence, not just its
  conclusions.
- End-to-end reconciliation balances exactly: `58,210 = 58,209 + 1 + 0`,
  zero unexplained difference, live, this session.

## What remains exactly as the prior remediation chain established (independently re-confirmed, not blindly repeated)

- Signature/hash/hardware-compatibility/anti-downgrade/interrupted-
  download OTA protections: real hardware-proven across this whole
  engagement, re-confirmed consistent this session.
- Bootloader automatic rollback: **confirmed absent, with binary-level
  evidence** (the actual flashed bootloader contains no app-rollback
  logic at all) — a genuine, structural hardware/toolchain limitation,
  not a firmware bug fixable by another patch.
- Real sensor/oil-flow accuracy: **still completely unproven** — the
  totalizer has read the identical static value across this entire
  multi-day, multi-session audit trail. No pulse simulation was
  substituted for real evidence.

## Final Verdicts

### Firmware and Data Integrity

**`FIRMWARE AND DATA INTEGRITY — CERTIFIED WITH LIMITATIONS`**

The core data pipeline (capture → sequence → durable queue → upload →
idempotent server commit → ack → prune) is real-hardware-proven sound,
with an exact end-to-end reconciliation and zero observed data loss,
duplication, or corruption across this entire audit trail. Limitations
that prevent an unqualified certification: the ~1-row power-loss window
(disclosed, bounded, by design, but real), the unproven multi-day
offline capacity, and the confirmed-absent bootloader rollback.

### Controlled Plant Pilot

**`CONTROLLED PLANT PILOT — CONDITIONAL GO`**

Conditional on, at minimum: (1) reprovisioning the endpoint away from
the bench address — the single highest-priority item; (2) a physically
present, trained person with USB recovery equipment for the entire pilot
window; (3) the sensor/totalizer manual cross-check (doc 14/07 of the
plant-readiness series) before trusting any device-reported quantity;
(4) restricted, supervised-only OTA per the already-documented policy.
See `19_PLANT_GO_NO_GO.md` for the complete condition list.

### Normal Plant Production

**`NORMAL PLANT PRODUCTION — CONDITIONAL GO`**

Conditional on real sensor accuracy being proven (currently not),
calibration verified within business tolerance, at least one full
production cycle reconciled, a completed 30-day soak (not started), the
sensor-silence alarm gap closed, and the endpoint/credentials genuinely
migrated off bench/test values. None of these are met today — this is
not yet reachable, only conditionally reachable pending real work.

### Remote Unattended Operation

**`REMOTE UNATTENDED OPERATION — NO-GO`**

Blocked structurally by the confirmed-absent bootloader rollback and by
several self-healing gaps (sensor silence, filesystem-mount-failure
requiring a human) that are acceptable only when a person is reachable.

### Remote Unattended OTA

**`REMOTE UNATTENDED OTA — NO-GO`**

Same structural reason — an authentic-but-unhealthy candidate has no
automatic recovery path on this hardware.

### Real Sensor Accuracy

**`REAL SENSOR ACCURACY — NOT CERTIFIED`**

No real or simulated flow has been observed at any point across this
entire multi-session audit trail. This requires a physically present
person, a real or calibrated sensor, and (for a true accuracy
certification) a known reference quantity — none available to this
remote session.

### Long-Duration Stability

**`30-DAY STABILITY — NOT CERTIFIED`**

No continuous multi-day run has occurred. A concrete soak-test plan is
provided (doc 15), specifically flagging the filesystem-usage upward
trend this session's own shorter-duration observation surfaced as the
single most important thing that plan needs to watch.

### Overall Enterprise Grade

**`ENTERPRISE GRADE — CERTIFIED FOR CONTROLLED PILOT ONLY`**

## Direct answers to the owner's questions

1. **Can this exact device go to the plant now?** Not as currently
   configured — its endpoint still points at this bench workstation.
   Once reprovisioned (a real but small step), yes, as a **supervised
   pilot**, not unattended production.
2. **Under what operating restrictions?** Physical presence required;
   OTA restricted to supervised maintenance windows with USB recovery
   physically present; sensor readings manually cross-checked, not
   trusted blindly; endpoint must be TLS in a genuine production
   context (currently plain HTTP).
3. **What can still cause data loss?** A ~1-second window between a
   row's write and its checkpoint during a genuine power interruption
   (bounded, disclosed); queue exhaustion beyond ~1.14 days offline at
   the current sampling rate; a genuinely full filesystem (fails loud,
   not silently, but still stops new records).
4. **How long can it operate offline?** Real capacity math: ~1.14 days
   theoretical ceiling at the firmware's actual 1-second sampling rate,
   with live evidence suggesting real usable capacity may be somewhat
   lower — NOT "several days" as-configured.
5. **Can duplicate records occur?** Not server-side (proven, live,
   exactly-once storage via `PRIMARY KEY` + `INSERT OR IGNORE`).
6. **Can it recover from power loss?** Sound code-level design
   (CRC32, truncate-before-append, cursor-then-delete ordering) proven
   across 8 real (non-power-loss) reboots this audit trail; genuine
   physical power-cut testing was not performed (no capability to do
   so remotely).
7. **Can it recover from bad firmware?** Only via physical USB
   recovery — bootloader automatic rollback is confirmed absent.
8. **Are credentials and communication production-secure?** No — plain
   HTTP, test signing key, no secure boot/flash encryption, an
   unauthenticated serial console that leaks the raw API key.
9. **Can all required settings be managed remotely?** No — only
   calibration/K-factor is genuinely remote; everything else needs
   physical/AP-mode access or a reflash.
10. **Is real sensor accuracy proven?** No.
11. **Is the system enterprise grade?** Certified for a controlled,
    supervised pilot only — not for unattended or full-scale production
    yet.
12. **What must be fixed before normal production?** Real sensor/
    calibration validation, TLS + production credentials, sensor-silence
    alarm, K-factor input validation, a completed 30-day soak, and the
    endpoint/credential migration off bench/test values.
13. **What must be fixed before unattended remote deployment?** All of
    the above, PLUS genuine bootloader-level rollback (a framework/
    toolchain decision, not a firmware patch) or formal business
    acceptance that a bad OTA requires onsite recovery — the same
    structural conclusion this audit's own prior phase already reached,
    now independently re-confirmed rather than merely repeated.

## Cleanup

All temporary test processes (bench server, `task bj01c2wgp`) are
stopped at the end of this audit, per the mandate's own rule 15 — see
confirmation in `20_EVIDENCE_INDEX.md`'s parent session log.
