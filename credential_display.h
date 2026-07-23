// ============================================================================
// credential_display.h — pure classification logic for redacted credential
// display (plant-pilot activation remediation, serial-console credential-
// exposure closure).
// ----------------------------------------------------------------------------
// Deliberately dependency-free (no Arduino.h, no mbedtls, no Store) --
// same "pure function extracted for testability" pattern this project
// already uses for ota_version_policy.h/ota_manifest_auth.h/timestamp_parse.h.
// The fingerprint's own SHA-256 computation is NOT duplicated here (it
// stays a thin mbedtls call in provision.h, the only place it's needed) --
// only the three-way missing/default/configured classification, which is
// the part that actually decides what gets displayed and is worth
// guaranteeing correct independent of any crypto library.
// ============================================================================
#pragma once

enum CredentialDisplayStatus {
  CRED_STATUS_MISSING,
  CRED_STATUS_DEFAULT,
  CRED_STATUS_CONFIGURED,
};

inline const char* credentialDisplayStatusStr(CredentialDisplayStatus s) {
  switch (s) {
    case CRED_STATUS_MISSING:    return "missing";
    case CRED_STATUS_DEFAULT:    return "default";
    case CRED_STATUS_CONFIGURED: return "configured";
    default:                     return "unknown";
  }
}

// `key`/`defaultKey` are plain C strings (not Arduino::String) so this is
// callable identically from host tests and from firmware. Never returns
// or exposes `key` itself -- classification only.
inline CredentialDisplayStatus classifyCredential(const char* key, const char* defaultKey) {
  if (key == nullptr || key[0] == '\0') return CRED_STATUS_MISSING;
  if (defaultKey != nullptr) {
    // Plain strcmp-equivalent without pulling in <string.h> here -- this
    // header is included by files that already have it (provision.h via
    // Arduino.h), but staying header-only/dependency-free for the host
    // test means comparing byte-by-byte directly.
    const char* a = key;
    const char* b = defaultKey;
    while (*a && *b && *a == *b) { a++; b++; }
    if (*a == '\0' && *b == '\0') return CRED_STATUS_DEFAULT;
  }
  return CRED_STATUS_CONFIGURED;
}
