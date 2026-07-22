# 24 — Revised Physical Test Authorization Request

Supersedes `Docs/audit/coviu_oil_meter_p0_remediation/11_PHYSICAL_TEST_AUTHORIZATION_REQUEST.md`.

## Exact physical commands proposed (none executed)

1. **Read-only preflight**: `GET /api/v1/info`, `GET /api/v1/status` over
   the LAN; a read-only serial monitor session on COM6.
2. **OTA write operations** (via the real OTA path, not USB): placing
   signed manifests + binaries on the bench server's `server/firmware/`
   directory — this triggers the device's OWN poll/download/flash cycle;
   no direct command is sent to the device to cause this.
3. **USB recovery** (doc 09), used ONLY: (a) once, to establish the known
   baseline before Test 1 if not already confirmed current, or (b) if a
   stop condition triggers.

## Exact serial port
`COM6` — re-confirm present at authorization time.

## Exact firmware images and hashes
All six artifacts in doc 22, with recorded SHA-256 hashes for the two
`.bin` files and three of the JSON manifests (the unhealthy-candidate
manifest and hash-mismatch manifest are constructed fresh at test time,
per doc 22's stated rationale).

## Exact OTA endpoint
`http://192.168.1.3:8000` — **must be re-confirmed**, not assumed current,
as this plan's own first preflight step.

## Will any reboot occur?
Yes — Tests 1 and 2 (and any USB recovery). Tests 3, 4, 5, 6 are designed
NOT to reboot the device (that's what "acceptance" means for each).

## Will any flash write occur?
Yes, for Tests 1, 2, and 5 (Test 5 writes to the inactive partition but
never commits it — see doc 23). Tests 3, 4, and 6 are designed to reject
BEFORE any flash write.

## Could production data be affected?
Only if preflight step 2 (endpoint re-confirmation) is skipped or fails —
the explicit first, mandatory step of every test in doc 23.

## Recovery commands
`09_USB_RECOVERY_RUNBOOK.md` (phase 1, unchanged, still accurate — the
partition layout and recovery command are unaffected by this phase's
code changes).

## Maximum potential failure impact
Same bound as phase 1: worst case is a USB recovery reflash using a
real, hash-verified known-good binary. No eFuse operations, no full-chip
erase, no scenario in this plan risks permanent device loss or reaches
any production system.

## Requested authorization wording

> **PHYSICAL HARDWARE AUTHORIZATION REQUIRED:** All listed non-hardware
> gates have passed only if supported by executed CI results above. No
> serial port, device HTTP API, reset, USB flash, or OTA action was
> performed. Authorization must identify the device, serial port,
> isolated endpoint, approved test cases, and permitted recovery actions.

Per the mandate's own required wording, e.g.:
> "Authorized: execute the isolated authenticated-OTA success, rollback,
> anti-downgrade-rejection, invalid-signature-rejection, hash-mismatch-
> rejection, and interrupted-download tests on COM6 using the documented
> signed artifacts and recovery plan."

This document does not itself constitute that authorization.
