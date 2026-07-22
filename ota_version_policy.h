// ============================================================================
// ota_version_policy.h — RISK-15 remediation: OTA anti-downgrade decision logic
// ----------------------------------------------------------------------------
// Deliberately dependency-free (no Arduino.h, no ESP-IDF, no HTTPClient) --
// this is the pure ACCEPT/REJECT decision ota.h::poll() makes once it has
// parsed a manifest, extracted so it is genuinely host-testable without
// pulling in any of ota.h's real network/flash/ESP-IDF machinery (which this
// phase does not attempt to make host-testable -- see
// 04_OTA_ANTI_DOWNGRADE_DESIGN.md for the explicit scope boundary).
//
// This function makes the DECISION; ota.h::poll() is responsible for parsing
// the manifest fields into an OtaCandidate and for acting on the verdict
// (proceeding to doUpdate_(), or logging + returning without ever touching
// the flash write path).
// ============================================================================
#pragma once
#include <stdint.h>
#include <string.h>

enum OtaVerdict {
  OTA_ACCEPT = 0,
  OTA_REJECT_MISSING_METADATA,   // manifest is missing a required field entirely
  OTA_REJECT_MALFORMED_VERSION,  // a version-shaped field parsed to a nonsensical value
  OTA_REJECT_DOWNGRADE,          // security_version < the device's accepted floor
  OTA_REJECT_HW_MISMATCH,        // hw_compat present and does not match this device's model
  OTA_REJECT_SCHEMA_MISMATCH,    // schema_version present and does not match this firmware's
};

struct OtaCandidate {
  bool hasSecurityVersion = false;
  long securityVersion = -1;     // manifest's claimed security_version (long, so -1 can mean "absent/unparsed")
  bool hasHwCompat = false;
  const char* hwCompat = nullptr;
  bool hasSchemaVersion = false;
  long schemaVersion = -1;
};

inline const char* otaVerdictStr(OtaVerdict v) {
  switch (v) {
    case OTA_ACCEPT:                 return "accept";
    case OTA_REJECT_MISSING_METADATA:return "missing_metadata";
    case OTA_REJECT_MALFORMED_VERSION:return "malformed_version";
    case OTA_REJECT_DOWNGRADE:       return "downgrade_rejected";
    case OTA_REJECT_HW_MISMATCH:     return "hardware_mismatch";
    case OTA_REJECT_SCHEMA_MISMATCH: return "schema_mismatch";
    default:                         return "unknown";
  }
}

// `acceptedSecurityFloor`: Store::securityVersion() -- the highest
// FW_SECURITY_VERSION this device has ever CONFIRMED healthy (see
// ota.h::confirmHealthyBoot()), never decreased except by a mechanism this
// pass does not implement (see store.h's factoryReset() comment).
// `deviceModel`: DEVICE_MODEL (config.h). `currentSchemaVersion`:
// SCHEMA_VERSION_CURRENT (queue.h) -- strict equality, matching this
// codebase's existing ADR-001 "no dual acceptance window" posture for
// schema versions (no migration-tolerant range exists yet).
inline OtaVerdict evaluateOtaCandidate(const OtaCandidate& c,
                                        uint32_t acceptedSecurityFloor,
                                        const char* deviceModel,
                                        long currentSchemaVersion) {
  // Missing required metadata is rejected outright -- an old-style manifest
  // (version/url only, no security_version) predates this policy and must
  // not be silently treated as security_version=0-and-therefore-always-a-
  // downgrade-risk-free-upgrade; it is simply not a valid candidate under
  // this policy. (Mandate requirement: "missing version metadata rejected.")
  if (!c.hasSecurityVersion) return OTA_REJECT_MISSING_METADATA;
  if (c.securityVersion < 0) return OTA_REJECT_MALFORMED_VERSION;   // parser sentinel for "absent/non-numeric"

  // Anti-downgrade: the actual point of this file. Strictly less-than --
  // equal is fine (e.g. re-offering the same security_version with a
  // patch-level semantic version bump is not a downgrade).
  if ((uint32_t)c.securityVersion < acceptedSecurityFloor) return OTA_REJECT_DOWNGRADE;

  if (c.hasHwCompat && (!deviceModel || strcmp(c.hwCompat, deviceModel) != 0)) {
    return OTA_REJECT_HW_MISMATCH;
  }
  if (c.hasSchemaVersion && c.schemaVersion != currentSchemaVersion) {
    return OTA_REJECT_SCHEMA_MISMATCH;
  }
  return OTA_ACCEPT;
}
