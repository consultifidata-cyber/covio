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
// Must match what the DEPLOYED fleet expects -- the device compares the
// manifest's key_id against this string and rejects UNKNOWN_KEY_ID on any
// mismatch. Reverted with the key itself; see the block above COVIO_OTA_
// PUBLIC_KEY_PEM for why.
#define COVIO_OTA_KEY_ID "covio-prod-erp-2026-07"

// PRODUCTION public key (Balaji V1 freeze remediation, 2026-07-26). Every
// release manifest must be signed with the matching private key (held per
// 03_OTA_SIGNING_KEY_CEREMONY.md's procedure) and key_id set to
// COVIO_OTA_KEY_ID above via sign_manifest.py's --key-id flag.
// ===========================================================================
// REVERTED 2026-08-11 TO THE KEY THE DEPLOYED FLEET ACTUALLY TRUSTS.
// ===========================================================================
// This file previously carried the public half of the 2026-07-26 ceremony key
// (id "covio-prod-key-2026-07"). THAT KEY'S PRIVATE HALF IS LOST. The ceremony
// wrote it to server/tools/.production_signing_key.pem "on this laptop" to be
// moved to a vault; it was never moved, the file is gone, and nobody holds it.
//
// Any image built with that key embedded can NEVER be sent a signed update
// again -- the device would verify against a key no one on earth can sign
// with, and the only recovery is a USB visit to each unit. v1.1.0 and v1.2.0
// shipped that way and are marked DO-NOT-FLASH prereleases for this reason.
//
// The key below is the one the commissioned Balaji meter is already running
// against, and whose private half genuinely exists and is in the owner's
// possession. Embedding it is therefore CONTINUITY, not a rotation: the fleet
// already trusts it, so an update carrying it cannot lock anything out. That
// property is the entire point of this change.
//
// !! HONEST PROVENANCE, DO NOT LET THIS SIT: this key was originally generated
// by sign_manifest.py --gen-test-key, and its id says "prod" only because the
// id string was chosen optimistically at the time. Its private half currently
// lives as an ordinary unencrypted file on an operator's laptop, which means
// anyone with that file can sign firmware the production meter will execute.
// It is being adopted here because a working signing key beats a lost one and
// because rotating anchors and flashing untested firmware in the same step is
// how fleets get bricked -- NOT because this custody is acceptable.
//
// ROTATE IT, deliberately, as its own change: generate a properly custodied
// key, ship an image embedding the new public half SIGNED WITH THIS ONE (the
// running image's key verifies, the shipped image sets future trust), confirm
// the fleet took it, then retire this key. Docs/audit/coviu_balaji_v1_freeze/
// 03_OTA_SIGNING_KEY_CEREMONY.md is the procedure -- including the step that
// was skipped last time, which is actually moving the private key somewhere
// it cannot be lost.
static const char* COVIO_OTA_PUBLIC_KEY_PEM = R"PEMKEY(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE7ZBAvBUFPk3RHkxuruueklHr7yGk
N4PmjI1ZpcfWt7lUd1udOsMBSsXN5rB6xiXY7/xc4c05vppYTISWdrWLEg==
-----END PUBLIC KEY-----
)PEMKEY";

#if RELEASE_BUILD && COVIO_OTA_KEY_IS_PLACEHOLDER
#error "RELEASE_BUILD=1 but ota_keys.h still has the placeholder OTA signing public key -- replace COVIO_OTA_PUBLIC_KEY_PEM with the real production key (and flip COVIO_OTA_KEY_IS_PLACEHOLDER to 0) before building a release image (RISK-16 / ADR-005 precedent)."
#endif
