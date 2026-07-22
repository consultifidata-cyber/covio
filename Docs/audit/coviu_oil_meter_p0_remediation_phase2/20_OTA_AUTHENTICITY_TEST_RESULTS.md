# 20 — OTA Authenticity Test Results (RISK-16)

## Result: 13/13 PASSED, real, executed this session (8 C++ + 5 Python)

| Test | Suite | Result |
|---|---|---|
| Canonical string matches real Python tool output (real candidate binary's actual hash) | C++ | PASS |
| Buffer-too-small fails loudly, never truncates | C++ | PASS |
| Time validity: ok/expired/not-yet-valid/skew (5 tests) | C++ | PASS |
| Time validity: exact boundary values | C++ | PASS |
| `--gen-test-key` refuses to overwrite an existing key | Python | PASS |
| Canonical string field order/delimiter (Python side) | Python | PASS |
| **Real sign→verify round trip**, independent `cryptography` call | Python | PASS |
| **Tampered field fails verification** (`InvalidSignature` raised) | Python | PASS |
| Image hash/size in the signed manifest are the REAL SHA-256/size of the image file | Python | PASS |

## Coverage against the mandate's 22-item authenticity test list

| # | Requirement | Status |
|---|---|---|
| 1 | Valid signed manifest + matching image accepted | **PARTIAL** — the signature-verification LOGIC is proven (canonical string + a real sign/verify round trip); the full end-to-end device-side accept path (parse → verify → download → hash-check → commit) is compile-verified only, not executed (needs hardware or a much larger host mock of HTTPClient/Update/mbedTLS) |
| 2 | Manifest modified after signing rejected | **PROVEN** — `test_tampered_manifest_field_fails_verification` |
| 3 | Image modified after signing rejected | **Traced by design, not executed** — `doVerifiedUpdate_()`'s hash comparison would catch this, but no test exercises an actual mismatched download |
| 4 | Invalid signature rejected | **PROVEN** at the crypto-library level (test 3 above); device-side `mbedtls_pk_verify()` call itself not host-tested |
| 5 | Unknown signing-key ID rejected | **Traced by code review** (`verifyManifestAuthenticity_()`'s `keyId != COVIO_OTA_KEY_ID` check) — not executed |
| 6 | Expired manifest rejected | **PROVEN** — `test_time_validity_expired` |
| 7 | Future-issued manifest outside skew rejected | **PROVEN** — `test_time_validity_not_yet_valid`, `test_time_validity_not_yet_valid_respects_skew` |
| 8 | Replayed manifest handled per documented policy | **Documented policy: safely idempotent** (doc 05/19) — not independently re-tested this phase beyond RISK-15's existing idempotency test |
| 9 | Wrong hardware ID rejected | Unchanged from RISK-15, re-confirmed passing (doc 18) |
| 10 | Wrong schema rejected | Unchanged from RISK-15, re-confirmed passing (doc 18) |
| 11 | Downgrade rejected | Unchanged from RISK-15, re-confirmed passing (doc 18) |
| 12 | Image size mismatch rejected | **Traced by code review** (`doVerifiedUpdate_()`'s Content-Length check) — not executed |
| 13 | Hash mismatch rejected | **Traced by code review** (the core of `doVerifiedUpdate_()`) — not executed |
| 14 | Truncated image rejected | **Traced by code review** (`totalWritten != expectedSize` check) — not executed |
| 15 | Oversized manifest rejected | **Traced by code review** (`http.getSize() > 4096` check added this phase) — not executed |
| 16 | Malformed signature encoding rejected | **Traced by code review** (`mbedtls_base64_decode()`'s return check) — not executed |
| 17 | Missing signature rejected | **Traced by code review** (the field-presence check in `poll()`) — not executed |
| 18 | Missing hash rejected | Same field-presence check — not executed |
| 19 | Download failure leaves boot partition unchanged | **Traced by design** — `Update.abort()` is called, `Update.end()` (the only call that touches otadata) is never reached on any failure path. Not executed. |
| 20 | Verification failure leaves boot partition unchanged | Same reasoning as #19 |
| 21 | Test/private signing key does not exist in firmware binary or repository | **PROVEN this session** — `git grep`/binary inspection confirmed no private key material anywhere in tracked files or the compiled `.bin` artifacts (the one "PRIVATE KEY" match was mbedTLS's own PEM-label table, verified byte-for-byte, doc 16) |
| 22 | Production build fails closed if no approved public key configured | **PROVEN this session** — `pio run -e release` fails on the `ota_keys.h` placeholder guard, re-verified after every subsequent code change this phase |

## Why so much of this table says "traced by code review, not executed"

The device-side verification call itself
(`Ota::verifyManifestAuthenticity_()`, `Ota::doVerifiedUpdate_()`) depends
on `HTTPClient`, `WiFiClientSecure`, `Update`, and mbedTLS's ESP-IDF
integration — none of which can be meaningfully mocked on a host machine
without a substantially larger undertaking than this phase's scope (a full
HTTP/TLS/flash-partition simulation layer). This is the SAME honest
pattern used for `pending()`/`ackThrough()` in RISK-04 (doc 02/17) and the
OTA rollback mechanism in RISK-05 (doc 06): the PURE, extractable logic
(canonical string, time validity, the actual cryptographic sign/verify
operation via a real library) is genuinely tested; the ESP32-coupled
wiring around it is compile-verified and code-reviewed, not executed.

## RISK-16 status: **CODE-CLOSED / CI-PROVEN (LOCAL, PARTIAL) — HARDWARE-PENDING**

The cryptographic core (canonical serialization + real ECDSA sign/verify)
is proven. The device-side integration and every failure-path behavior
listed above as "traced, not executed" require either a much larger host
mocking effort or the physical hardware test in doc 23.
