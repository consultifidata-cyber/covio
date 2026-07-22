# 07 — Security Review

Full findings are in `03_ENTERPRISE_CHECKLIST_15_SECTIONS.md` §12; this file is the focused security deep-dive plus the secret-scan result.

## Secret scan (manual, this session — not an automated exhaustive scanner)

**Finding SEC-01 (P0): a real, non-placeholder WiFi password is hardcoded in `config.h:38`** (`DEFAULT_WIFI_PASS`), paired with a real SSID at `config.h:37` (`DEFAULT_WIFI_SSID`). This is distinct from `DEFAULT_API_KEY = "dev-key-change-me"` (`config.h:29`), which is an obvious, intentional placeholder — the WiFi password is a genuine working credential. **This audit directly confirmed, via the live connected device's real serial log and WiFi connection, that this exact credential is in active use right now.** The actual secret value is not reproduced anywhere in this report or its evidence files. **Remediation: rotate this WiFi password immediately (on the real access point it belongs to, independent of this firmware) and remove the literal value from `config.h`, replacing it with an empty/obviously-fake placeholder plus a documented first-boot provisioning requirement (the AP-mode captive portal already exists and is the correct mechanism for this — `wifi_provision.h`).** No other non-placeholder secret (private key, second password, hardcoded token) was found in the files read during this audit; this is a manual-review-level finding, not a certified clean bill across the entire binary/repository.

## What is genuinely solid (found and demonstrated, not just claimed)

- TLS/CA-pinning implementation is real, `setInsecure()` is never called, and the release-build compile-time guard against a placeholder CA cert was **directly proven** by deliberately triggering it (`pio run -e release` failed exactly as designed on `certs.h:50`).
- Device-facing API authentication (`X-Api-Key` on push/config/OTA-manifest) is real, checked before any request parsing, and covered by 6 real passing tests (`test_dm_phase_0b_auth.py`).
- Idempotency is DB-enforced (`PRIMARY KEY(device_id,seq)`), not merely application-level.
- Per-device key issuance, rotation, and revocation all exist and are covered by real passing tests.
- URL scheme matching is case-insensitive by design (`certs.h:65-67`, an explicit, documented audit fix against a real "HTTPS://" typo silently downgrading to plaintext) — evidence of a real, prior security-review pass on this codebase, independently re-verified here.

## What is genuinely open (disclosed by the project itself, re-confirmed here directly in code)

- **SEC-02 (P0):** every `/admin/*` route in `server.py` (`kfactor`, `devices/provision`, `<id>/revoke-key`, `<id>/rotate-key`) has **zero authentication**. Confirmed by reading every `@app.route` definition — none check any credential. Anyone who can reach the server's port can revoke a device, mint arbitrary provisioning keys, or change the K-factor (a billing-relevant value). Explicitly disclosed as an accepted bench/pilot-scope limitation by the project's own ADR-016 — real and current, not newly discovered, but independently re-verified in code rather than taken on faith.
- **SEC-03 (P1):** no API rate limiting anywhere — confirmed absent by a full read of `server.py`.
- **SEC-04 (P1):** no replay protection on any write endpoint (config or admin) — no nonce, timestamp, or signature.
- **SEC-05 (P2, disclosed, not independently re-verified either way):** Secure Boot and Flash Encryption status on the physical eFuses was **not queried in this audit** (would require `espefuse.py summary` against the connected device, deliberately deferred as outside the agreed non-destructive/static-analysis scope). Carrying forward RE10's prior disclosure that both are off, without independently confirming it this session.
- **SEC-06 (P2):** the serial provisioning console (`show`/`set key`/`set wifi`/`factory`) has no authentication of any kind — anyone with physical USB access has full read/write control, including reading the raw API key back via `show`. This matches the project's own stated "physical possession is the trust boundary" design philosophy — a reasonable, disclosed tradeoff, not a hidden bug, but worth the customer's explicit sign-off before an unsupervised plant install.

## Three-tier risk summary (as required)

| Threat actor | Posture |
|---|---|
| Remote attacker (no physical access, no valid API key) | Reasonably protected for the device-facing data path (real TLS capability + real auth); **not** protected against the unauthenticated admin endpoints if that port is reachable from an untrusted network |
| Casual physical access (can plug in USB) | **Not protected** — full unauthenticated serial console |
| Determined physical extraction (chip-level attack) | **Not protected** — no Secure Boot, flash encryption status unconfirmed/likely absent by prior disclosure |

## Severity and deployment gate

SEC-01 and SEC-02 are each independently **P0** and **block plant deployment** until resolved. SEC-03/04 are **P1**, should be resolved before any pilot exposed beyond a fully trusted internal network. SEC-05/06 are **P2**, acceptable for a short, supervised, physically-secured pilot with the customer's explicit informed sign-off, not acceptable for unattended long-term field service without further hardening.
