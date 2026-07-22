# 03 — RISK-02 Remediation: Credential Containment and Rotation

## Immediate containment (this session)

- `config.h:37-38` — the real SSID/password were replaced with the obvious,
  non-functional placeholders `"YOUR_WIFI_SSID_PLACEHOLDER"` /
  `"YOUR_WIFI_PASSWORD_PLACEHOLDER"`, with a comment explaining why and
  pointing at the real fix (AP-mode provisioning, `wifi_provision.h`,
  already implemented and unaffected by this change).
- The real credential was redacted **before this repository's first-ever
  git commit** (see `01_INDEPENDENT_P0_VERIFICATION.md`'s Phase 0 note: no
  `.git` existed at session start). It therefore does not exist in any git
  history, in this or any other branch — **no history rewrite is required**,
  because it was never committed in the first place.
- Repo-wide grep for the literal exposed SSID/password strings across the
  entire tree (source, `Docs/`, `test/`, CI config, images, everything) —
  **zero matches** outside the single `config.h` occurrence that was fixed.
- The real credential is NOT reproduced anywhere in this report or in any
  file this session wrote.

## What this does NOT do

Per the mandate's own explicit instruction, this session did **not** rotate
the real WiFi password on the affected access point, and did not attempt to
determine whether the network is still using it. **Source-code removal does
not invalidate an already-exposed credential.**

> **HUMAN ACTION REQUIRED:** rotate the exposed WiFi password through the
> authorized network administrator for the affected network. This is not
> something this session is authorized to do, and no amount of source-code
> remediation substitutes for it.

## Status, stated separately as required

| Item | Status |
|---|---|
| Source-code remediation | **Done** — placeholder in `config.h`, confirmed no other occurrence in the tree |
| Git-history exposure | **Not applicable** — repo had no history before this session; credential was fixed before the first commit |
| Credential rotation (the real network) | **NOT DONE — requires human action**, explicitly out of this session's authority |
| Is the credential still usable in the real plant/network? | **Unknown to this session** — must be assumed YES until an authorized person confirms rotation |
| This P0 fully closed? | **No** — code-closed only; rotation is a human action this report cannot close on its own |

## Other required containment items

- **Firmware compilation fails safely when required production credentials
  are absent:** already true and unchanged, verified this session by
  actually compiling `env:release` — it fails at compile time
  (`certs.h:50`'s `#error`) if the placeholder CA cert is still in place,
  and `covio_firmware.ino`'s `RELEASE_BUILD` guard (lines 70-78) refuses to
  leave `setup()` if `DEFAULT_API_KEY` is still active. Neither of these
  guards currently checks the WiFi placeholder specifically — WiFi
  credentials are provisioned via NVS/AP-mode (`wifi_provision.h`), not via
  a release-build compile-time gate, so a build-time check for a
  placeholder WiFi SSID is not the correct enforcement point (a device is
  *expected* to ship with a placeholder default and get its real
  credentials via first-boot provisioning, unlike the API key/CA cert
  guards which gate against a **shared, unrotated default** reaching a
  customer). This is a deliberate architectural distinction, not an
  oversight, and is unchanged by this fix.
- **Provisioning path already exists and is not touched by this fix:**
  `wifi_provision.h` (SoftAP + captive portal, DM-Phase 2) is the intended
  real-credential path; confirmed still compiles and is unaffected.
- **Secrets never logged / never in diagnostics APIs:** confirmed by
  reading `diagnostics.h::apiKeyStatus_()` — it reports only `"missing"` /
  `"default"` / `"configured"`, never the key value itself; WiFi password is
  never included in any `/api/v1/*` JSON body (only `wifi.ssid` appears,
  which is a network *name*, not a secret). Unchanged by this fix, confirmed
  by code read, not modified.
- **Secret scanning:** this session's containment check was a manual,
  targeted grep for the two known-exposed literal strings, not an automated
  general-purpose secret scanner (e.g. gitleaks/trufflehog) integrated into
  CI. Adding that is a real, valuable follow-up but is explicitly out of
  this P0's scope (tracked as a P3 item in the updated risk register,
  matching the prior audit's own remediation-plan structure) — not silently
  claimed as done here.
