# 10 — Credential Rotation Checklist (Human Operator Action — RISK-02)

This session (and the prior remediation session) did **not** and **will
not** rotate the real, exposed WiFi credential. Neither the old nor the
new password is recorded anywhere in this document or any other audit
artifact.

## Checklist for the authorized network administrator

1. **Network/SSID affected:** the network the physically-connected bench
   device was observed connecting to in the prior session's serial
   capture (see `Docs/audit/coviu_oil_meter_enterprise_readiness/evidence/serial_capture.log`
   — the SSID itself is redacted there too, consistent with this
   requirement).
2. **Confirmation the credential was previously present in local source:**
   Yes — `config.h:37-38` (pre-remediation), replaced with placeholders in
   commit `c83fcd7` on this branch, before this repository's first-ever
   git commit (so it never entered git history — see
   `Docs/audit/coviu_oil_meter_p0_remediation/03_CREDENTIAL_CONTAINMENT_AND_ROTATION.md`).
3. **Authorized person responsible:** _to be filled in by the business —
   whoever administers the affected network._
4. **Safe rotation window:** _to be scheduled by the network
   administrator; no technical constraint from this codebase dictates a
   specific window._
5. **Devices affected:** the single physically-connected bench unit
   (`esp32-F4E5B2858428`) is the only device confirmed to have used this
   credential in this session's evidence; any other device sharing the
   same network would also be affected and should be inventoried by the
   network administrator, not assumed to be only this one unit.
6. **New credential provisioning method:** AP-mode captive-portal
   provisioning (`wifi_provision.h`, already implemented, unaffected by
   this remediation) — NOT a rebuilt firmware image with a new hardcoded
   default. Re-flashing with a new hardcoded credential would repeat the
   exact mistake RISK-02 identified.
7. **Rollback method:** if the new credential causes connectivity issues,
   the device falls back to SoftAP + captive portal automatically after
   `AP_FALLBACK_TIMEOUT_MS` (15s) of failed station connection
   (`covio_firmware.ino`'s existing, unchanged boot-time logic) — no
   special rollback procedure is needed beyond re-running the
   provisioning flow with corrected credentials.
8. **Confirmation the old credential no longer works:** _to be performed
   and attested by the network administrator after rotation — not
   verifiable by this codebase or this session._
9. **Confirmation source/logs/diagnostics do not expose the new
   credential:** By code construction — `diagnostics.h::apiKeyStatus_()`
   pattern (status-only, never the value) applies to the API key; WiFi
   password is NEVER included in any `/api/v1/*` JSON response body
   (confirmed by reading `diagnostics.h`: only `wifi.ssid`, a network
   *name*, appears, never `wifi.pass`) — unchanged by this phase. This
   holds for whatever new credential is provisioned via AP-mode, since the
   provisioning flow writes directly to NVS (`store.h::setWifi()`) and
   never echoes the value back through any diagnostic endpoint.
10. **Date, time, operator, and approval evidence:** _to be filled in by
    the authorized person performing rotation._

## RISK-02 status rule (restated, unchanged from phase 1)

- **CODE-CLOSED / HUMAN ACTION PENDING** until items 3, 4, 8, and 10 above
  are filled in by an authorized person.
- **CLOSED** only once rotation is confirmed and the old credential is
  confirmed invalid.
