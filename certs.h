// ============================================================================
// certs.h  —  DM-Phase 5 (ADR-005): pinned server CA certificate
// ----------------------------------------------------------------------------
// ADR-005: "the device trusts only the specific issuing CA for its known,
// single server host — not the general public CA ecosystem." This is that
// pin, shared by sync.h (push/config/ota-manifest) and ota.h (the .bin
// download itself) — the "all four cloud endpoints" ADR-005's Definition of
// Done requires moved to HTTPS.
//
// PRODUCTION PIN (release/erp-production): ISRG Root X1 + ISRG Root X2
// (Let's Encrypt roots; X1 notAfter 2035-06-04, X2 notAfter 2040-09-17),
// fetched verbatim from https://letsencrypt.org/certs/ and verified with
// openssl x509 before embedding. Bundle covers both the RSA (R-series ->
// X1) and ECDSA (E-series -> X2, X1-cross-signed) issuance chains of the
// production ERP host. mbedTLS parses multi-cert PEM bundles natively.
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

#define COVIO_CA_CERT_IS_PLACEHOLDER 0

static const char* COVIO_PINNED_CA_CERT = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
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
