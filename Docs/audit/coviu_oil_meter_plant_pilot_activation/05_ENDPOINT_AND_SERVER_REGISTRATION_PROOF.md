# 05 — Endpoint and Server Registration Proof (Parts 6-7)

## Part 6 — Every occurrence of the bench address, found and classified

Comprehensive search this phase, tracked source only (`.h`/`.ino`/`.py`/
`.js`/`.ini`) plus `Docs/`:

```
./config.h:38: #define DEFAULT_SERVER_URL "http://192.168.1.3:8000"
  -> Classification: DEFAULT (compile-time first-boot seed) AND, live-
     confirmed this session, ALSO the device's CURRENT ACTIVE RUNTIME
     value (NVS currently holds this same address as its live override
     -- confirmed via /api/v1/status throughout this entire audit
     trail). This is the one real, active-runtime bench dependency.

./server/tools/sign_manifest.py:34 (inside the tool's own --help usage
  example, a triple-quoted docstring, not executable logic)
  -> Classification: DOCUMENTATION-ONLY (example command text)

./test/native/test_sign_manifest_tool.py:69,110 (fake URLs used inside
  unit-test fixtures)
  -> Classification: TEST-ONLY

Docs/ (42 files)
  -> Classification: DOCUMENTATION-ONLY (this entire audit trail's own
     historical record of what was tested against the bench address --
     expected and correct for an audit trail, not a runtime concern)
```

**No occurrence in `tools/device-manager` (the desktop provisioning
app) was found** — not separately deep-audited this phase (out of this
phase's focused scope), flagged as not separately re-checked.

**Conclusion: exactly one real, active runtime dependency on the bench
address exists** — the device's own currently-configured `server_url`,
both as compiled default AND live NVS value. Every other occurrence is
test/documentation and carries no runtime risk.

## Part 7 — Plant server identity and registration: STOPPED

**No real plant server exists for this session to test against.** Every
sub-question in this part (device authentication, device/plant ID
matching registration, records landing under the correct
device/plant, cross-plant isolation, malformed-ack rejection against the
real server) **requires the real endpoint from doc 04, which is not yet
available.**

## What WAS proven this phase, against the only reachable server (the bench stub) — explicitly NOT a substitute for real plant-server proof

- Device authenticates successfully against the bench stub (`X-Api-Key`,
  unchanged mechanism this entire audit trail).
- Duplicate POSTs remain idempotent (re-confirmed, this whole chain).
- Malformed acknowledgements remain rejected (re-confirmed, prior
  phase's live tests, mechanism unchanged this phase).
- No cross-device record confusion is structurally possible (schema-
  level `PRIMARY KEY (device_id, seq)`, unchanged).

**This proves the MECHANISM works — it does not prove the real plant
server (which doesn't exist yet for this session) will behave
identically.** A real plant-server integration test must be repeated
against the actual production/plant backend once doc 04's values are
available, before this specific sub-part can be marked verified.

## Verdicts

`PLANT ENDPOINT: NOT VERIFIED`
`PLANT SERVER REGISTRATION: NOT VERIFIED`
