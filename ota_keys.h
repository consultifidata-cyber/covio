// ============================================================================
// ota_keys.h — RISK-16 remediation: OTA manifest signature verification key
// ----------------------------------------------------------------------------
// Same pattern as certs.h (ADR-005's pinned CA cert), applied to OTA
// manifest authenticity: the device holds ONLY a PUBLIC verification key.
// The corresponding PRIVATE signing key never exists in this firmware, this
// repository, or any CI log -- it is operational key-management material
// held by whoever is authorized to cut Covio releases, exactly matching
// ADR-005's own already-established "signing keys generated and held by
// Covio, never distributed to the field or committed to source control"
// discipline, now applied to firmware-release signing instead of just TLS.
//
// Algorithm: ECDSA over the NIST P-256 curve (secp256r1), SHA-256 digest --
// chosen because ESP-IDF's bundled mbedTLS (already linked into every build
// via WiFiClientSecure's own TLS usage -- confirmed present at
// tools/sdk/esp32s3/include/mbedtls/mbedtls/include/mbedtls/{pk,ecdsa,sha256}.h)
// supports it natively via mbedtls_pk_verify(), with no new library
// dependency. 256-bit curve, ~128-bit security level -- an appropriate,
// widely-used choice for firmware signing (the same curve TLS itself
// commonly uses).
//
// BALAJI V1 FREEZE REMEDIATION (2026-07-26): the placeholder test key below
// has been replaced with the real Covio production OTA signing public key,
// generated via `python server/tools/sign_manifest.py --gen-production-key`
// -- see Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md
// for the full ceremony record (when it was run, by what process, where the
// private key now lives). The corresponding PRIVATE key was written ONLY to
// the local, gitignored server/tools/.production_signing_key.pem at
// generation time and is expected to have since been moved to this
// project's secrets vault/HSM and deleted from local disk per that
// document's own procedure -- it is NEVER committed to this repository, NOT
// printed in full anywhere in this repository or its documentation, and
// this firmware file holds ONLY the public verification half, exactly as
// this file's design has always intended (see the design notes above).
// ============================================================================
#pragma once
#include "config.h"

#define COVIO_OTA_KEY_IS_PLACEHOLDER 0

// Key identifier -- allows a future signed manifest to name WHICH public
// key it expects the device to verify against (supports key rotation:
// multiple trusted keys could be compiled in, keyed by this same id scheme
// -- see 21_SIGNING_KEY_MANAGEMENT_PLAN.md's rotation section for why only
// ONE is implemented this pass, not a trust-list).
#define COVIO_OTA_KEY_ID "covio-prod-key-2026-07"

// PRODUCTION public key (Balaji V1 freeze remediation, 2026-07-26). Every
// release manifest must be signed with the matching private key (held per
// 03_OTA_SIGNING_KEY_CEREMONY.md's procedure) and key_id set to
// COVIO_OTA_KEY_ID above via sign_manifest.py's --key-id flag.
static const char* COVIO_OTA_PUBLIC_KEY_PEM = R"PEMKEY(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE3dSVWNuBfHDwCwblo238KFHdo7a0
pbpJZNkac/GsOXOOvX02U0Fuj6Biqc1/mIVYzkkg97xlEpK7229OETkgYg==
-----END PUBLIC KEY-----
)PEMKEY";

#if RELEASE_BUILD && COVIO_OTA_KEY_IS_PLACEHOLDER
#error "RELEASE_BUILD=1 but ota_keys.h still has the placeholder OTA signing public key -- replace COVIO_OTA_PUBLIC_KEY_PEM with the real production key (and flip COVIO_OTA_KEY_IS_PLACEHOLDER to 0) before building a release image (RISK-16 / ADR-005 precedent)."
#endif
