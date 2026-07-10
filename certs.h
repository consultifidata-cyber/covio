// ============================================================================
// certs.h  —  DM-Phase 5 (ADR-005): pinned server CA certificate
// ----------------------------------------------------------------------------
// ADR-005: "the device trusts only the specific issuing CA for its known,
// single server host — not the general public CA ecosystem." This is that
// pin, shared by sync.h (push/config/ota-manifest) and ota.h (the .bin
// download itself) — the "all four cloud endpoints" ADR-005's Definition of
// Done requires moved to HTTPS.
//
// THIS FILE SHIPS WITH A PLACEHOLDER, NOT A REAL CERTIFICATE.
// Per ADR-005 ("RSA-3072 signing keys generated and held by Covio and never
// distributed to the field or committed to source control"), the same
// discipline applies here: a real production CA certificate is operational
// key-management material, not something to be authored or guessed into
// source control by a coding session. Before any field/release build:
//   1. Replace the PEM block in COVIO_PINNED_CA_CERT below with the real
//      pinned server CA's certificate.
//   2. Change COVIO_CA_CERT_IS_PLACEHOLDER from 1 to 0.
// See Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md for the full
// factory TLS/Secure-Boot provisioning procedure this fits into.
//
// With the placeholder left in place, an https:// connection correctly
// FAILS CLOSED — the TLS handshake is rejected because the placeholder does
// not trust anything real. This is CA-pinning's designed fail-safe
// behavior, not a bug (ADR-005: "CA-pinning fails closed by design").
// Plain-HTTP bench traffic (DEFAULT_SERVER_URL's dev default, and
// server/server.py, which is still plain HTTP) never touches this file at
// all — see sync.h/ota.h's own scheme dispatch (covioIsHttpsUrl()).
//
// RELEASE_BUILD (config.h) guard below: a build compiled with
// RELEASE_BUILD=1 while the placeholder is still present fails to compile
// outright, so a factory/release build can never accidentally ship without
// a real pin.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

#define COVIO_CA_CERT_IS_PLACEHOLDER 1

static const char* COVIO_PINNED_CA_CERT = R"CERT(
-----BEGIN CERTIFICATE-----
REPLACE THIS ENTIRE BLOCK WITH THE REAL PINNED SERVER CA'S PEM BEFORE ANY
FIELD/RELEASE BUILD. See this file's own header comment and
Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md.
-----END CERTIFICATE-----
)CERT";

#if RELEASE_BUILD && COVIO_CA_CERT_IS_PLACEHOLDER
#error "RELEASE_BUILD=1 but certs.h still has the placeholder CA cert -- replace COVIO_PINNED_CA_CERT with the real pinned server CA (and flip COVIO_CA_CERT_IS_PLACEHOLDER to 0) before building a release image (ADR-005)."
#endif

// Shared by sync.h and ota.h so the scheme check is written once, not
// duplicated across both call sites (matches this project's existing
// duplication-avoidance convention, e.g. ota.h's extractStr_ reuse).
//
// Audit fix (production-readiness audit, Transport security finding):
// URL schemes are case-insensitive per RFC 3986. A plain startsWith(
// "https://") would silently treat an operator-entered "HTTPS://..." or
// "Https://..." server_url (e.g. via the console's `set server <url>`
// command, or a copy-pasted URL) as NOT https, falling through to the
// plain-WiFiClient path with no pinned CA and no loud error -- a silent
// transport-security downgrade on nothing more than a casing typo.
// equalsIgnoreCase() on just the scheme prefix closes that.
static inline bool covioIsHttpsUrl(const String& url) {
  return url.length() >= 8 && url.substring(0, 8).equalsIgnoreCase("https://");
}
