# 18 — Gaps and Remediation Plan (Part 21)

## P0 — can lose, duplicate, misattribute, or corrupt oil-flow data

| Gap | Evidence | Impact | Likelihood | Workaround | Permanent fix | Blocks pilot? | Blocks normal prod? | Blocks unattended? |
|---|---|---|---|---|---|---|---|---|
| **Endpoint still points at bench infrastructure** | Live, this session — `server_url: http://192.168.1.3:8000` | All plant data would land in the wrong (bench) database, or nowhere if unreachable from the plant | Certain, if deployed as-is | Manual reprovisioning via serial console (`set url`) before deployment | Same — this is a one-time provisioning step, not a code fix | **Yes — already a mandatory NO-GO trigger** | Yes | Yes |
| Sensor silence has no alarm (#15/16/17 in doc 13) | Live, this entire session (zero pulses, zero alarm) | A genuinely failed sensor produces confidently-wrong "flow = 0" data with no operator signal | High in real plant conditions (cables do fail) | Manual visual/periodic check of `pulse_frequency_hz` | Add a firmware-level "expected activity but none seen" alarm | No (mitigated by mandatory manual cross-check, doc 07 of plant-readiness series) | **Yes, until fixed** | Yes |
| K-factor has no input validation (server.py, `/admin/kfactor`) | Code-confirmed this session | A typo'd K-factor silently re-scales ALL historical + future consumption reporting | Low (requires an authenticated admin mistake) | Admin-side manual review before submitting | Add a sane bounds check server-side | No | Yes | Yes |

## P1 — can cause extended outage, unsafe recovery, security compromise, or unrecoverable device state

| Gap | Evidence | Impact | Blocks pilot? | Blocks normal prod? | Blocks unattended? |
|---|---|---|---|---|---|
| **Bootloader automatic rollback absent** | Binary-level proof, this chain | An authentic-but-unhealthy OTA candidate has no automatic recovery path | No (with mandatory USB-recovery-present OTA policy) | No (same mitigation) | **Yes, structurally** |
| **Unauthenticated serial console leaks the raw API key** (`show` command) | Code-confirmed this session | Anyone with physical USB access reads the live API key in plaintext | No (physical access already implies significant trust in a supervised pilot) | Yes — unacceptable at scale with untrusted physical access | Yes |
| **No secure boot / no flash encryption** | Confirmed from actual sdkconfig, this session | Physical access can extract all NVS credentials and/or flash arbitrary firmware | No, for a supervised pilot | Yes, for genuine production hardening | Yes |
| **Single fleet-wide OTA signing key, no rotation/revocation** (RISK-19) | Code-confirmed, still open | One key compromise affects every device trusting it | No | Yes | Yes |
| Server admin API has no replay protection | Code-confirmed (pre-existing, re-verified) | A captured admin request could be replayed | No (LAN-scoped bench admin surface today) | Yes for a real network-exposed admin surface | Yes |
| Unauthenticated `factory` reset command | Code-confirmed this session | Accidental/malicious reset wipes Wi-Fi/API-key/calibration cache with no confirmation | No (requires physical access + intent) | Yes | Yes |

## P2 — important operational or diagnostic limitation

- Offline buffering ceiling (~1.14 days theoretical, real usage
  suggests less) below the "several days" target — doc 03/06.
- Missing diagnostics: oldest-pending-record age, true flash-bytes-used,
  plant/asset identity field, OTA-approval audit trail — doc 11.
- No admin authentication distinction between "server down" and "401/
  403 auth failure" on the device side — doc 13 (#5/#6).
- Segment-deletion return value not checked (`LittleFS.remove()`) —
  theoretical slow flash-usage creep if deletion silently and repeatedly
  fails — doc 05 §7.
- No firmware-level OTA maintenance-window or operator-authorization
  gate — procedural mitigation only — doc 08.
- Filesystem-usage upward trend observed this session (43%→59%) not
  yet explained or bounded — doc 03/15.

## P3 — documentation, usability, cosmetic

- No timezone concept at all (may or may not matter depending on how
  consuming systems expect timestamps) — doc 09.
- `ota.state`'s CONFIRMED-over-FAILED precedence can mask a failed OTA
  attempt behind an already-confirmed image's status in the summary
  field (the underlying `failed_` flag and alarms still surface it
  correctly) — doc 08/13.
- Reset-reason mapping's earlier `ESP_RST_EXT` guess, now superseded by
  the correct raw-value read (`ESP_RST_UNKNOWN`, resolved this session)
  — cosmetic, already fixed in effect (the `unknown(<n>)` format never
  hid anything either way).

## Explicitly NOT remediated this session (per execution boundary)

No code was changed this session. Every gap above is reported for a
future, separately authorized remediation pass — consistent with the
mandate's own instruction not to change firmware merely to make the
report look better.
