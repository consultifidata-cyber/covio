// ============================================================================
// boot_health.h — P1 hardening: app-level unhealthy-boot rollback safety net
// ----------------------------------------------------------------------------
// Deliberately dependency-free (no Arduino.h, no NVS/Preferences, no
// esp_ota_ops.h), matching the same extraction pattern already used by
// ack_validation.h/ota_version_policy.h/ota_manifest_auth.h -- this is the
// PURE decision logic ("is this boot running an image that has never been
// application-confirmed healthy before?"), host-testable independent of
// NVS/bootloader coupling. The actual NVS reads/writes live in store.h
// (lastConfirmedFwVersion/lastConfirmedSecurityVersion/recordHealthyBoot),
// the actual rollback call lives in covio_firmware.ino's setup().
//
// WHY THIS EXISTS (see config.h's UNHEALTHY_BOOT_STREAK_LIMIT comment and
// Docs/audit/coviu_oil_meter_p0_remediation_phase2/
// 36_OTA_CONFIRMATION_ROOT_CAUSE_AUDIT_AND_REMEDIATION.md): the bootloader's
// own PENDING_VERIFY rollback trial does not reliably arm on this hardware/
// toolchain, so this project cannot rely on it as the only safety net for a
// bad OTA image that crash-loops before reaching application-level health
// confirmation. isBootOnTrial() answers the question independent of
// whatever the bootloader reports.
// ============================================================================
#pragma once
#include <string.h>
#include <stdint.h>

// True if the currently-running image's version pair does not match the
// last pair that ever reached application-level health confirmation --
// i.e. this boot is "on trial" and should count toward the unhealthy-boot
// streak. A fresh device (lastConfirmedVersion == "") is always on trial
// until its first confirmation, exactly like any other never-confirmed
// image -- not a special case.
inline bool isBootOnTrial(const char* lastConfirmedVersion, uint32_t lastConfirmedSecVer,
                           const char* runningVersion, uint32_t runningSecVer) {
  return strcmp(lastConfirmedVersion, runningVersion) != 0 || lastConfirmedSecVer != runningSecVer;
}
