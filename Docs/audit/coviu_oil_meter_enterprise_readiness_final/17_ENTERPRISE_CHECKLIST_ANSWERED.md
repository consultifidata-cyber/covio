# 17 — Enterprise Success Criteria (Part 19)

## Zero Data Loss During Unexpected Power Cuts

**Verdict: PARTIALLY PROVEN.**
Exact maximum remaining loss window: **one `QRow`** (up to ~1 second of
telemetry), specifically in the narrow gap between a row's durable write
and its checkpoint confirmation (doc 03 §9, doc 04). This is a real,
disclosed, bounded, intentional trade-off (never accept an unconfirmed
row), not an open-ended risk — but it is not "zero." True physical
power-cut testing was not performed this session (no physical
capability); the bound above is code-derived, not measured.

## Offline Buffering for Several Days

**Verdict: NOT PROVEN — real capacity math (doc 03) shows a theoretical
ceiling of ~1.14 days at this firmware's actual 1-second sampling rate**,
with live evidence suggesting real usable capacity may be lower still.
**"Several days" is not currently achievable without a configuration or
architecture change.**

## Exactly-Once or Idempotent Sync

**Real guarantee: at-least-once delivery with a genuinely idempotent
server-side commit** (proven live this session — 3 identical POSTs
produced exactly 1 stored row). Not "exactly-once" in the full
distributed-systems sense, and this report does not claim that.

## Remote Configuration

**Complete list** (doc 07): only **calibration/K-factor** is genuinely
remotely configurable without physical access. Endpoint, API token
delivery, Wi-Fi, device/plant identity, sync/sampling intervals, and
timezone all require physical/AP-mode access or a full reflash.
**Classified: PARTIAL, not complete.**

## OTA With Automatic Rollback

**Bootloader automatic rollback does NOT exist** — proven with
binary-level evidence (doc 08), not merely unobserved. Application-level
health confirmation is real and working, and is explicitly NOT described
as bootloader rollback anywhere in this audit.

## Automatic Recovery

Self-healing: network retries, software-restart recovery, brownout/
watchdog detection (config-confirmed) — proven or credibly designed.
**Does NOT self-heal**: filesystem mount failure (halts, needs a human),
sensor silence (no alarm at all), an unhealthy OTA candidate (needs USB
recovery). See doc 09/13 for the full per-failure-class table.

## Complete Audit Trail

**Traceable end-to-end for telemetry** (capture → sequence → local
persistence → upload → server commit → ack → prune) — proven, this
session, live (doc 05/16). **NOT traceable for OTA approval decisions**
(who authorized a given rollout) — no such record exists (doc 08).

## Health Dashboard

Extensive real-time diagnostics exist (doc 11) — firmware/build
identity, security floor, OTA state (including honest
bootloader-vs-app-level disclosure), queue/sync state, reset reason
(now including the raw numeric value). **Missing**: sensor-health signal,
oldest-pending-record age, true flash-bytes-used, plant/asset identity,
OTA-approval audit trail.

## Secure Communication

**Currently HTTP, not HTTPS** (live-confirmed, this session).
Certificate: none provisioned (release build fails closed specifically
because of this). Signing key: TEST classification. Credential
management: partial — per-device API keys are supported in design but
this unit still uses the shared bootstrap key; OTA signing key has no
rotation/revocation mechanism (RISK-19, still open).

## 30-Day Stability

**NOT STARTED.** A concrete soak-test plan is provided (doc 15),
including specific attention to the filesystem-usage upward trend this
session's own shorter-duration observation surfaced.
