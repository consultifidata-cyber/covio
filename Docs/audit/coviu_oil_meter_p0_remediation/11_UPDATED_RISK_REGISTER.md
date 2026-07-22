# 11 — Updated Risk Register

Historical findings from `docs/audit/coviu_oil_meter_enterprise_readiness/11_RISK_REGISTER.md`
are preserved below with a **Status** column added; nothing from the prior
register is deleted or silently rewritten. One new item (RISK-15) was
discovered during this session's P0-5 static review.

| Risk ID | Severity | Component | Original Finding | Status After This Session |
|---|---|---|---|---|
| RISK-01 | P0 | server.py + queue.h | Quarantined record permanently stalls ack/pruning | **CLOSED** — union-based ack computation, 14 tests passing |
| RISK-02 | P0 | config.h:37-38 | Real WiFi credential hardcoded | **CODE-CLOSED / HUMAN ACTION PENDING** — placeholder in place, never entered git history; real network password rotation is a human action not performed this session |
| RISK-03 | P0 | server.py /admin/* | Zero admin authentication | **CLOSED** — Basic Auth, 2 roles, rate limiting, audit logging, fail-closed production startup, 18 tests passing |
| RISK-04 | P0 | queue.h::append() | Silent data loss at flash-full/write failure | **CODE-CLOSED / UNIT-TEST GAP** — durable failure counter + CRITICAL alarm + real capacity %, compile-verified; no C++ host test harness exists in this repo to unit-test the failure branches directly (see doc 05/09) |
| RISK-05 | P0 | ota.h | OTA rollback never proven on hardware | **STILL OPEN — HARDWARE-PROOF-PENDING** — static confidence increased (bootloader rollback support directly confirmed compiled-in), physical test plan prepared (doc 07), not executed |
| RISK-06 | P1 | diagnostics.h | No last-error/retry/quarantine visibility | Unchanged this session — `failed_write_count`/`last_write_failure` (P0-4) partially improves this for queue-write failures specifically, but general retry-count/last-API-error visibility remains absent |
| RISK-07 | P1 | server.py dashboard | K-factor retroactivity in billing-adjacent view | Unchanged this session — explicitly out of P0 scope, cross-cutting requirement confirmed NOT made worse (no P0 fix touches the dashboard's litres calculation) |
| RISK-08 | P1 | provision.h + factory reset | No auth on serial console; reset not audited | Unchanged this session |
| RISK-09 | P1 | local_api.h/wifi_provision.h | No replay protection on config-write endpoints | Unchanged this session — explicitly the same gap now also disclosed for the NEW admin Basic-Auth surface (doc 04); tracked as one item, not duplicated |
| RISK-10 | P2 | totalizer.h checkpoint | Flash wear-endurance unmeasured | Unchanged this session. **Note:** P0-4 added two new rarely-written files (`failA.bin`/`failB.bin`); these are written ONLY on an actual failure (never on the hot path), so they do not change this risk's magnitude |
| RISK-11 | P2 | No RTC/NTP | No precise offline event-time attribution | Unchanged this session |
| RISK-12 | P2 | Watchdog/crash-loop detection | None configured | Unchanged this session |
| RISK-13 | P3 | sync.h::extractLong_() | Naive ack_seq parser | Unchanged this session — note the SAME naive-parser pattern is used by `extractLong_`/`extractFloat_`/`extractStr_` for the new `quarantined` field too (device firmware does not currently parse it at all, so this is dormant risk, not active) |
| RISK-14 | P3 | OTA polling/rollout | No jitter, no staged rollout | Unchanged this session |
| **RISK-15** | **P2 (new)** | ota.h::poll() | **No anti-downgrade policy** — `poll()` triggers an update whenever `manifest.version != FW_VERSION`, with no check that the offered version is actually NEWER. A misconfigured or compromised manifest could cause a downgrade to an older, potentially vulnerable firmware version. Discovered during this session's P0-5 static review (doc 06). Not a P0 itself (does not block proving RISK-05's rollback mechanism), but should be fixed before the OTA manifest mechanism is trusted at fleet scale | **OPEN — new finding this session, not yet remediated (deliberately out of P0 scope per mandate's "do not expand" instruction)** |

## Gate re-evaluation (mandate's Acceptance Gates, prior audit's lettering)

| Gate | Prior Result | Result After This Session |
|---|---|---|
| C — Idempotent Delivery | PARTIAL (RISK-01) | **PASS** — RISK-01 closed |
| E — Fleet Management | PARTIAL (RISK-03, RISK-09) | **PARTIAL** — RISK-03 closed; RISK-09 (replay protection) still open, now also covering the admin surface |
| B — Durable Persistence | PARTIAL (RISK-04) | **PARTIAL** — code-closed, hardware/unit-test proof still pending |
| F — Safe OTA | FAIL (RISK-05) | **FAIL, unchanged** — still requires hardware proof; one new static finding (RISK-15) |
| G — Security | FAIL (RISK-02, RISK-03) | **PARTIAL** — RISK-03 closed; RISK-02 code-closed but real-world rotation is an outstanding human action, so this gate cannot be marked PASS |
