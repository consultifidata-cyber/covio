# 08 — Physical OTA and Recovery Test Plan (Authorization Required)

**No step in this document has been executed.** This is a plan only. See
`11_PHYSICAL_TEST_AUTHORIZATION_REQUEST.md` for the exact authorization
boundary and the required wording to proceed.

## Precondition checks

| Item | Value | Source |
|---|---|---|
| Device model | ESP32-S3 (Waveshare ESP32-S3-Relay-1CH), `covio-oilflow-v1` | prior session's live device query (not re-queried this session — COM6 not touched) |
| Device ID | `esp32-F4E5B2858428` | prior session's live device query |
| Current firmware version | `1.0.0` (assumed unchanged since last query — **must be re-confirmed at authorization time**, not assumed stale-safe) | config.h at last known-connected state |
| Current queue size / highest local seq / highest acked seq | **UNKNOWN to this session** — requires a fresh, authorized `GET /api/v1/status` query against the physical device, not performed this session | pending authorization |
| Endpoint classification | Prior query showed `http://192.168.1.3:8000` (private LAN) — **must be RE-confirmed at authorization time** before any step below proceeds | pending re-confirmation |
| Current configuration backup plan | No live NVS/queue backup exists from this device (would require COM6 access) — the ONLY backup possible without physical access is: this branch's committed source + the two built `.bin` artifacts in doc 07 | this session |
| Current firmware recovery binary | `evidence/known-good-1.0.0.bin` (doc 07) | this session |
| Partition table | `default_16MB.csv`, sha256 `4a6aaf11525dab5d49a336aa52ef7e65f8e83d824c9a7a931c72943cbd40d63c` | this session, re-confirmed |
| USB recovery procedure | See `09_USB_RECOVERY_RUNBOOK.md` | this session |
| Expected rollback partition | The OTHER `app0`/`app1` slot from whichever is currently active — not independently determined this session (would require `esptool.py --port COM6 read_flash` or similar, prohibited without authorization) | pending authorization |
| Power stability requirements | Standard USB power via COM6's own connection is assumed adequate for a bench test; no additional stabilization identified as necessary | this session, unverified against the physical unit |
| Stop conditions | See "Stop conditions" section below | this session |

## Physical Test 1 — Successful OTA

1. Confirm (fresh query, post-authorization) the device's `server_url` is
   still the private-LAN bench address, not production.
2. `GET /api/v1/status` — record `queue.backlog`, `queue.acked_seq`,
   `totalizer_raw_pulses`, `security_version`, `accepted_security_floor`.
3. Place `evidence/candidate-1.0.1-candidate.bin` at
   `server/firmware/covio-1.0.1-candidate.bin` on the bench server; write
   `server/firmware/manifest.json`:
   ```json
   {"version":"1.0.1-candidate","url":"http://<bench-host>:8000/firmware/covio-1.0.1-candidate.bin",
    "security_version":1,"hw_compat":"covio-oilflow-v1","schema_version":1}
   ```
4. Wait up to `OTA_POLL_MS` (5 min) for the device to poll and accept.
5. Observe (serial monitor, once authorized) manifest retrieval, download,
   verification acceptance (`evaluateOtaCandidate()` returning
   `OTA_ACCEPT`), reboot, and `confirmHealthyBoot()` firing.
6. `GET /api/v1/info` — confirm `fw_version` now reports `1.0.1-candidate`.
7. `GET /api/v1/status` again — confirm `queue.backlog`/`queue.acked_seq`
   are consistent with step 2 plus whatever new samples were generated
   during the test window (no unexplained loss), and
   `accepted_security_floor` is still `1` (same-security-version upgrade,
   floor unchanged per design — see doc 04).

**Acceptance:** no missing sequence, no duplicate DB row (reconcile against
`server/covio.db`), no configuration loss, no queue loss, correct firmware
version, `ota.state` reports a healthy/confirmed state.

## Physical Test 2 — Forced Rollback

1. Construct the unhealthy candidate: from the SAME source tree, apply a
   temporary, throwaway edit to `covio_firmware.ino`'s `setup()` —
   comment out the `syncEngine.wifiConnect();` call (and the bounded
   WiFi-wait loop immediately after it) so the device can never reach a
   successful sync and therefore never calls `confirmHealthyBoot()`. Bump
   `FW_VERSION` to `"1.0.2-unhealthy"` in the same throwaway edit. Build
   (`pio run -e esp32dev`), record the hash, then revert both edits via
   `git checkout` (same pattern as doc 07's candidate build) so nothing is
   committed.
2. Publish a manifest offering this unhealthy candidate (same
   `security_version: 1`, so it passes the anti-downgrade gate and is
   attempted).
3. Observe the device download, verify, and reboot into the candidate.
4. Confirm the candidate never confirms healthy (no WiFi, no sync — by
   construction) within a reasonable bounded window (a few minutes).
5. Confirm the bootloader's compiled-in rollback mechanism
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`) automatically reboots the
   device back into the previous good partition.
6. `GET /api/v1/info` — confirm `fw_version` reports `1.0.1-candidate`
   (or `1.0.0`, whichever was actually running before this test) again,
   with ZERO manual USB intervention.
7. `GET /api/v1/status` — confirm queue/config are intact, and
   `accepted_security_floor` is UNCHANGED from before this test (the
   failed candidate must never have raised it — doc 04's core invariant).

**Acceptance:** previous firmware restored automatically, no USB recovery
needed, no sequence loss, no duplicate rows, rollback visible via
`ota.state`, security floor unchanged.

## Physical Test 3 — Downgrade Rejection

1. Publish a manifest offering ANY built `.bin` (doc 07's known-good is
   fine) but with `security_version: 0` — strictly below whatever the
   device's `accepted_security_floor` is at this point (`>= 1` after Test
   1/2 above).
2. Wait for the device to poll.
3. Confirm via serial log / `GET /api/v1/status`'s new `last_reject_reason`
   field that the candidate was rejected with reason `downgrade_rejected`
   — BEFORE any download or flash write occurred.

**Acceptance:** device rejects before flashing, current firmware remains
active, rejection reason visible via `/api/v1/status`, no reboot, no queue
impact.

## Physical Test 4 — Interrupted OTA

1. Begin an OTA to a valid, higher/equal-security-version candidate.
2. Where safely possible (e.g., temporarily disable the bench server's
   WiFi/network interface, or block the firmware-file route at the
   server), interrupt the download mid-transfer WITHOUT cutting the
   device's own power.
3. Confirm the device's current firmware remains active (the download
   failure occurs before any partition switch/reboot).
4. Confirm `ota.state` reports a failed state and `pulseFreqHz`/queue
   collection continues uninterrupted throughout (telemetry capture and
   OTA are on independent timers, per `covio_firmware.ino`'s existing
   `tOta`/`tTelemetry` structure).
5. Confirm the device retries on its next `OTA_POLL_MS` cycle (5 min)
   once connectivity is restored.

**Acceptance:** current firmware remains active, no bootable-but-corrupt
inactive partition, bounded retry, queue collection uninterrupted, failure
visible remotely.

## Stop conditions

- Abort immediately if the device's `server_url` is ever found to be
  anything other than the pre-confirmed private-LAN bench address.
- Abort Test 2 if the device does not recover automatically within 5
  minutes of the forced-failure attempt starting — proceed to
  `09_USB_RECOVERY_RUNBOOK.md` with fresh authorization for that specific
  step.
- Do not proceed to Test 2 or 3 until Test 1 has fully passed and been
  reconciled.
- Never send a deliberately invalid/interrupted image to any endpoint
  other than the local bench server.

## Recovery plan

See `09_USB_RECOVERY_RUNBOOK.md` for exact commands. Not executed unless a
stop condition triggers or all four tests above are otherwise complete and
a final restore-to-known-good is explicitly requested.
