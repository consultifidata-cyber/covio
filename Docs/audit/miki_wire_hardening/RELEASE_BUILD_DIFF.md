# RELEASE_BUILD=0 vs RELEASE_BUILD=1 — Complete Behavioral Diff

Phase-2 §23. Derived by exhaustive grep of every functional `RELEASE_BUILD`
reference at candidate `ef6bf16` (locations cited). There are exactly
**four** differences — nothing else in the firmware changes.

| # | Feature | Where | Purpose | Security impact | Runtime impact | Deployment impact | Balaji compat | Miki compat |
|---|---|---|---|---|---|---|---|---|
| 1 | Default-API-key boot refusal | `covio_firmware.ino:169` | A release device that still has `dev-key-change-me` halts (serviceable console loop) instead of running | Prevents fleet-default-credential deployment | Boot-time check only | Device must be provisioned (`set key`) BEFORE flashing release, or it sits in the halt console until provisioned | Deployed Balaji unit runs `RELEASE_BUILD=0` and per commissioning docs has a real key — a release reflash would boot normally; must verify key state on the bench first | MW-001 has a real key pinned (release cert) — same verification step |
| 2 | Placeholder-CA compile guard | `certs.h:89` | Refuses to COMPILE a release with a placeholder CA | Prevents unpinned-TLS release | None (compile-time) | Satisfied: real ISRG roots since `e5a593b` | OK | OK |
| 3 | Placeholder-OTA-key compile guard | `ota_keys.h:59` | Refuses to COMPILE a release with the placeholder OTA verify key | Prevents test-key-trusting release | None (compile-time) | Satisfied in source (production key present since the freeze work). **BUT the deployed Balaji unit still trusts the OLD test key** — flashing it a release image also swaps its trust anchor; its OTA server-side signing must move to the production key at the same time (see freeze doc 09's migration plan) | ⚠ coupled migration | MW-001: same consideration if OTA is ever served to it |
| 4 | Dirty-tree build refusal | `config.h:308` | Refuses an unidentified/dirty release image | Build provenance | None (compile-time) | Requires a clean committed tree (now the norm post-snapshot) | OK | OK |

Also relevant (unchanged by the flag): USB-CDC serial logging and the
provisioning console remain fully active in release builds — RELEASE_BUILD
is a *provisioning-hygiene* gate, **not** a debug-surface reduction. Closing
serial/local-API exposure is part of the separate security migration plan,
not this flag.

**Conclusion:** `release-mikiwire` is safe to adopt for Miki once (a) the
bench confirms the device's real key is in NVS, and (b) the standard flash
procedure is followed. For Balaji, a release reflash is entangled with the
OTA trust-anchor migration and must follow the freeze docs' migration plan —
do not treat it as a drop-in.
