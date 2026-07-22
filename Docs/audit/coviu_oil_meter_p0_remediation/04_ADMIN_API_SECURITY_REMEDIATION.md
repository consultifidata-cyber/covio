# 04 — RISK-03 Remediation: Admin API Authentication and Authorization

## Design

HTTP Basic Auth, applied uniformly to every `/admin/*` route and to `/`
(the K-factor dashboard, which contains a mutating HTML form). Chosen over a
token/session scheme because:
- A browser sends the `Authorization` header on every request **including a
  plain HTML `<form method="POST">` submit**, with zero client-side code —
  no cookie/session/CSRF-token machinery needed for a single-process
  bench/pilot tool at this scale.
- It is directly testable via Flask's test client (`auth=(user, pass)`) and
  via `curl -u`, with no bespoke client logic.

Two roles: `admin` (full read+write — K-factor, provisioning, revoke,
rotate) and `viewer` (read-only — dashboards, event log). This satisfies
"device-management roles are separate from read-only monitoring roles."

## Credential configuration and fail-closed behavior

- `COVIO_ADMIN_MODE` (env var): `"dev"` (default) or `"production"`.
- `COVIO_ADMIN_PASSWORD` / `COVIO_ADMIN_VIEWER_PASSWORD` (env vars) —
  **never a hardcoded default**, directly applying RISK-02's own lesson to
  this new surface.
- **`production` mode with no real password configured refuses to import
  the module at all** (`RuntimeError` raised from
  `_resolve_admin_credentials()`, called at module load) — this is the
  strongest form of "fail closed": the process cannot start, not merely
  "starts but rejects requests." A short denylist of obvious placeholder
  values (`admin`, `password`, `changeme`, `dev-key-change-me`, empty) is
  also rejected in production mode, so an operator setting
  `COVIO_ADMIN_PASSWORD=admin` does not count as "configured."
- **`dev` mode with no password set** (this repo's existing bench-stub
  posture, and what every test in this repo runs under) generates a random
  18-byte URL-safe token **per process**, printed once to stdout, never
  persisted, never a shared constant across runs — confirmed distinct
  across two independent calls in
  `test_dev_mode_with_no_password_set_generates_one_never_a_hardcoded_default`.

## Rate limiting and audit

- Per-source-IP sliding-window limiter (5 failures / 60s, in-memory) —
  explicitly documented as adequate for a single-process bench/pilot server
  and **not** a distributed rate limiter; a real multi-instance production
  deployment needs a shared store, called out as a P2 scale item in the
  updated risk register, not silently pretended away.
- Every failed admin auth attempt is recorded via the existing
  `record_event()` mechanism as an `ADMIN_AUTH_FAILED` event (same pattern
  already used for device-facing `API_AUTH_FAILED`), including path and
  source address.
- Every pre-existing sensitive-operation audit event
  (`DEVICE_PROVISIONED`/`KEY_REVOKED`/`KEY_ROTATED`/`CALIBRATION_CHANGED`)
  is confirmed to still fire correctly now that the auth decorator wraps
  those views (the decorator calls through to the original view function
  unchanged on success).

## What is explicitly NOT done here (documented, not silently assumed)

- **Replay protection** (nonce/timestamp) on admin requests — a captured
  Basic-Auth header is replayable until the password is rotated. This is
  already tracked as RISK-09 (P1) in the prior risk register; fixing it is
  a larger scope item (moving to signed/nonce'd requests) than this P0
  requires, and Basic Auth's core weakness here is bounded by requiring
  TLS transport in production (see next point) and a rate-limited attack
  surface.
- **TLS termination** — this bench server is plain HTTP by design (see
  `sync.h`'s existing ADR-005 comment: the bench stub is intentionally
  `http://`). A real production deployment of this server (or its LCS/cloud
  successor) MUST sit behind a TLS-terminating reverse proxy; this is a
  deployment-topology requirement, not something Flask's dev server itself
  can be made to enforce from inside this codebase. Documented here as a
  condition of safe production use, not fixed in code.
- **Per-device-scoped admin roles / cross-plant isolation** — this bench
  server has no concept of "plant" as a first-class entity yet (fleet-wide
  K-factor, no multi-tenant device grouping), so "cross-plant admin access
  rejected" is not yet a meaningful test in this codebase — noted as a P2
  gap for whenever a real multi-plant deployment is designed, not
  fabricated as tested here.

## Test evidence

`test/native/test_p0_3_admin_auth.py` — 18 tests, all passing:
anonymous rejection on every route, viewer-role rejection on
mutating routes, viewer-role acceptance on read-only routes, admin-role
success on every route, wrong-password rejection, rate limiting (both the
end-to-end 429 behavior and the limiter's per-source-key isolation),
audit-event creation on both failure and success, fail-closed production
startup (missing password, and each denylisted placeholder individually),
dev-mode random-password generation, no-secret-echo, and the Basic-Auth
`<form>` POST end-to-end path.

Additionally, `test_dm_phase_4_registry.py` and `test_dm_phase_6_logical_id.py`
(pre-existing test files that call `/admin/*` routes) were updated to pass
`auth=("admin", server.ADMIN_PASSWORD)` — these are genuine regression
tests, not disabled or weakened; they now prove the pre-existing DM-Phase 4/6
behavior (provisioning, key rotation, event history, dashboard rendering)
still works correctly **through** the new auth layer, not around it.
