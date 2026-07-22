# 23 — Revised Physical Test Plan (Authenticated OTA Path)

Supersedes `Docs/audit/coviu_oil_meter_p0_remediation/08_PHYSICAL_OTA_TEST_PLAN.md`.
**No step in this document has been executed.**

## Critical correction from this mandate, applied throughout

`pio run -e esp32dev -t upload --upload-port COM6` is a **direct USB
flash**, not proof of OTA. It is used ONLY for: (a) establishing an
isolated known-good baseline before testing begins, and (b) recovery if
OTA rollback fails. Every OTA/rollback/downgrade/authenticity test below
goes through the REAL path:

```
signed manifest on the bench server
→ device poll (ota.h::poll())
→ manifest fetch + authenticity verification (verifyManifestAuthenticity_())
→ anti-downgrade/hw/schema gate (evaluateOtaCandidate())
→ streamed download + incremental SHA-256 (doVerifiedUpdate_())
→ hash compare → Update.end() (boot partition set) → reboot
→ health confirmation (confirmHealthyBoot()) or bootloader rollback
```

## Read-only preflight (required before ANY write/reboot step)

1. Confirm device identity matches `esp32-F4E5B2858428` — via read-only
   `GET /api/v1/info`.
2. Confirm `server_url` is still the private-LAN bench address
   (`http://192.168.1.3:8000` or its current equivalent) — via read-only
   `GET /api/v1/status`. **Abort if this is anything else.**
3. Confirm the test device's identity cannot collide with any production
   device: its `hardware_id` is a MAC-derived value
   (`esp32-<MAC-derived-hex>`), globally unique by construction; confirm
   no other device is configured to point at the same bench server
   endpoint during this test window.
4. Confirm test uploads cannot enter a production database: the bench
   server (`server/server.py`) writes to `server/covio.db`, a local
   SQLite file on the SAME machine running the test — not reachable from
   or connected to any production database by any code path in this
   repository.
5. Confirm admin commands cannot affect another device: `server.py`'s
   `/admin/devices/<id>/...` routes are scoped by `device_id` path
   parameter; confirm no other device_id is used in this session's test
   commands.
6. Confirm test WiFi is authorized: the device's current WiFi connection
   was established during a prior, already-authorized session — this
   phase does not touch WiFi credentials at all (RISK-02 remains
   completely separate, still pending human rotation regardless).
7. Confirm credentials used are non-production: the device's `X-Api-Key`
   is whatever bootstrap/test key it was provisioned with in prior
   sessions — confirm via `GET /api/v1/status`'s `api_key_status` field
   (should read `"default"` or `"configured"` for a bench unit, never
   claimed as a production key).

**If any of the above cannot be confirmed without first reading the
device, that reading IS this preflight step — a read-only HTTP GET or
serial-monitor observation — and requires its own explicit authorization
before any WRITE or reboot step proceeds.**

## Physical Test 1 — Successful Authenticated OTA

1. Complete preflight above.
2. `GET /api/v1/status` — record `queue.backlog`, `queue.acked_seq`,
   `totalizer_raw_pulses`, `security_version`, `accepted_security_floor`.
3. Place `evidence/candidate-1.0.1-candidate.bin` and
   `evidence/signed_manifest_candidate.json` (renamed to
   `manifest.json`) on the bench server's `server/firmware/` directory.
4. Wait up to `OTA_POLL_MS` (5 min) for the device to poll.
5. Observe (serial monitor, read-only, once authorized): manifest fetch,
   `[OTA] update offered: ... -- authenticity verified`, streamed
   download progress, `[OTA] verified OK — rebooting into new image`.
6. `GET /api/v1/info` post-reboot — confirm `fw_version` reports
   `1.0.1-candidate`.
7. `GET /api/v1/status` — confirm queue/config preserved,
   `accepted_security_floor` still `1` (same-security-version upgrade).

**Acceptance:** no missing sequence, no duplicate DB row, correct firmware
version, `ota.state` reports confirmed, `last_auth_reject_reason` is null.

## Physical Test 2 — Automatic Rollback (Validly-Signed, Unhealthy Candidate)

1. Construct the unhealthy candidate per phase-1 doc 07's Test-2
   recipe (comment out `syncEngine.wifiConnect()`, bump `FW_VERSION` to
   `1.0.2-unhealthy`), build, then **sign it for real**:
   ```
   python server/tools/sign_manifest.py --sign \
     --private-key server/tools/.test_signing_key.pem \
     --hw-compat covio-oilflow-v1 --version 1.0.2-unhealthy \
     --security-version 1 --schema-version 1 \
     --image <freshly-built-unhealthy.bin> \
     --image-url http://192.168.1.3:8000/firmware/covio-1.0.2-unhealthy.bin \
     --channel stable --key-id covio-test-key-2026-07 \
     --out server/firmware/manifest.json
   ```
   Revert the throwaway source edit immediately after building (same
   discipline as every prior candidate build this session — never
   committed).
2. Serve the signed manifest; wait for the device to poll, verify, and
   download.
3. Confirm the device boots the candidate, never proves healthy (no WiFi
   → no sync → `confirmHealthyBoot()` never called).
4. Confirm the bootloader's compiled-in rollback
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`) automatically restores the
   previous image within a few minutes, with zero USB intervention.
5. `GET /api/v1/info` — confirm `fw_version` reverted.
6. `GET /api/v1/status` — confirm `accepted_security_floor` UNCHANGED
   (the unhealthy candidate never reached `confirmHealthyBoot()`, so it
   never raised the floor).

**Acceptance:** automatic recovery, no queue/config loss, floor unchanged,
rollback visible via `ota.state`.

## Physical Test 3 — Anti-Downgrade Rejection

1. Serve `evidence/signed_manifest_downgrade_test.json` (renamed
   `manifest.json`) alongside `evidence/known-good-1.0.0.bin`.
2. Wait for the device to poll.
3. `GET /api/v1/status` — confirm `last_reject_reason` reads
   `downgrade_rejected`, BEFORE any download/reboot occurred.

**Acceptance:** rejected before flashing, current firmware unaffected, no
reboot, reason visible remotely.

## Physical Test 4 — Invalid-Signature Rejection

1. Serve `evidence/signed_manifest_tampered.json` (renamed
   `manifest.json`) alongside `evidence/candidate-1.0.1-candidate.bin`.
2. Wait for the device to poll.
3. `GET /api/v1/status` — confirm `last_auth_reject_reason` reads
   `signature_verify_failed`, BEFORE any download occurred.

**Acceptance:** rejected before flashing, current firmware unaffected.

## Physical Test 5 — Hash-Mismatch Rejection

1. Serve a manifest whose signed `image_sha256` does not match the
   actual bytes at `url` (doc 22, artifact 6's construction recipe).
2. Confirm the device downloads (passes the pre-download gates, since the
   SIGNATURE over the WRONG hash is still internally consistent — this is
   specifically testing the POST-download comparison), then rejects after
   computing the real hash and finding it doesn't match the signed claim.
3. `GET /api/v1/status`/serial log — confirm a hash-mismatch message,
   `Update.abort()` was called, current firmware remains active.

**Acceptance:** current firmware unaffected despite bytes having been
written to the inactive partition (never committed via `Update.end()`).

## Physical Test 6 — Interrupted Download

1. Begin Test 1's flow, then interrupt network delivery mid-transfer
   (block the bench server's firmware route temporarily) without cutting
   device power.
2. Confirm current firmware remains active throughout and after.
3. Confirm queue collection is uninterrupted (independent timers).
4. Confirm the device retries on its next `OTA_POLL_MS` cycle once the
   route is restored.

**Acceptance:** bounded retry, no boot-partition corruption, queue
unaffected, failure visible remotely.

## Reconciliation (all six tests)

For every test: `GET /api/v1/status` before and after; reconcile
`queue.backlog`/`acked_seq`/`totalizer_raw_pulses` deltas against whatever
new samples were legitimately generated during the test window, with zero
unexplained difference.

## Stop conditions

Identical to phase-1 doc 08's, plus: abort Test 2/4/5 immediately if the
device EVER reboots into a candidate whose manifest was NOT one of this
plan's own signed artifacts (would indicate the anti-downgrade/
authenticity gates were bypassed somehow, a genuine emergency requiring
immediate USB recovery per doc 09 and a full re-review of `ota.h` before
any further testing).
