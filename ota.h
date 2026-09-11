// ============================================================================
// ota.h  —  pull-based OTA with signed-manifest authenticity + rollback safety
// ----------------------------------------------------------------------------
// FLOW:
//   1. Poll server_url + PATH_OTA_MANIFEST -> a SIGNED manifest (RISK-16).
//   2. Verify: key id, expiry/skew, ECDSA-P256 signature over the manifest's
//      own canonical fields (ota_manifest_auth.h) -- BEFORE any anti-downgrade
//      check or download attempt.
//   3. Anti-downgrade/hardware/schema gate (RISK-15, unchanged):
//      evaluateOtaCandidate() (ota_version_policy.h).
//   4. Stream-download the image (NOT via the HTTPUpdate library -- see
//      doVerifiedUpdate_() below for why), computing its SHA-256 as bytes
//      arrive, writing to the inactive OTA partition via Update.h.
//   5. Compare the computed hash to the manifest's SIGNED hash. Only if it
//      matches is Update.end() called -- THIS is the actual moment the
//      candidate boot partition is set. Any failure before this point
//      (signature, expiry, hardware, schema, downgrade, size, hash, or a
//      truncated/interrupted download) leaves the CURRENT firmware active
//      and bootable, with nothing written to otadata.
//   6. On next boot, main.ino calls confirmHealthyBoot() AFTER the device has
//      proven it can run (WiFi up + one successful server contact). That call
//      marks the new image valid AND advances the anti-downgrade floor
//      (RISK-15). If the new image crashes/hangs BEFORE that, the bootloader
//      rolls back to the previous good partition automatically
//      (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1, confirmed compiled into
//      this exact toolchain -- see 06_OTA_STATIC_CERTIFICATION.md).
//
// ############################################################################
// # SECURITY — READ BEFORE FIELD DEPLOYMENT                                   #
// # Pull OTA means the device DOWNLOADS AND EXECUTES whatever is at that URL.  #
// # The minimum bar, and this file's status against each item:                #
// #   1. Serve the manifest AND the .bin over HTTPS with a valid cert, and    #
// #      pin the server CA (setCACert) — NOT setInsecure().                    #
// #      DM-Phase 5 / ADR-005: DONE (covioIsHttpsUrl() dispatch, both         #
// #      poll() and doVerifiedUpdate_()).                                     #
// #   2. Cryptographically authenticate the manifest AND the image itself,    #
// #      independent of the transport (TLS proves the network peer, not the  #
// #      artifact's authorization) — RISK-16: DONE below. ECDSA-P256 manifest #
// #      signature (ota_keys.h/ota_manifest_auth.h) + SHA-256 image-hash      #
// #      verification BEFORE the candidate boot partition is ever set.       #
// #   3. Enable Secure Boot + signed app images so a tampered/rogue binary     #
// #      is rejected by the BOOTLOADER even if every application-level check  #
// #      above were somehow bypassed — NOT DONE here — this is a one-way      #
// #      eFuse burn performed once at the first factory flash via            #
// #      espsecure.py/esptool.py, not firmware source code, and this mandate  #
// #      explicitly prohibits burning eFuses in this pass. See               #
// #      Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md for the full,     #
// #      not-yet-executed procedure. RISK-16 (this file) and Secure Boot are  #
// #      DIFFERENT, complementary protections — closing RISK-16 does not      #
// #      substitute for Secure Boot, and vice versa.                          #
// # This file ships with an HTTP path so your BENCH stub works — see          #
// # certs.h/config.h; that path is unaffected by any of the above.            #
// ############################################################################
//
// PARTITIONS: select an OTA-capable partition scheme in the Arduino IDE
// (Tools -> Partition Scheme -> "Minimal SPIFFS (1.9MB APP with OTA)" or
// "Default 4MB with spiffs"). Without two app slots, OTA cannot work.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "esp_ota_ops.h"
#include "store.h"
#include "certs.h"
#include "ota_version_policy.h"   // RISK-15 remediation (OTA anti-downgrade)
#include "ota_manifest_auth.h"    // RISK-16 remediation (canonical manifest + expiry)
#include "ota_keys.h"             // RISK-16 remediation (public verification key)
#include "sync.h"                 // RISK-16: for Sync::haveServerTime()/estimatedUnixNow()
#include "queue.h"                // for SCHEMA_VERSION_CURRENT (already #pragma once'd
                                    // by the time ota.h is reached in covio_firmware.ino)

// ---- DM-Phase 1 (local diagnostics, §13 A.4 ota_state enum) --------------
enum OtaState { OTA_STATE_NONE, OTA_STATE_PENDING_VERIFY, OTA_STATE_CONFIRMED, OTA_STATE_FAILED };

// Public stringifier for OtaState. It lives here, next to the enum, because
// there are now two consumers -- the LAN diagnostics JSON and the cloud push
// envelope -- and two hand-maintained copies of a switch over the same enum is
// how they drift apart. diagnostics.h delegates to this.
inline const char* otaStateStr(OtaState s) {
  switch (s) {
    case OTA_STATE_PENDING_VERIFY: return "pending_verify";
    case OTA_STATE_CONFIRMED:      return "confirmed";
    case OTA_STATE_FAILED:         return "failed";
    default:                       return "none";
  }
}

// RISK-16 remediation: manifest-authenticity rejection reasons, reported
// alongside RISK-15's OtaVerdict via /api/v1/status so an operator can tell
// "rejected for being a downgrade" apart from "rejected for a bad signature"
// apart from "rejected because the device has no time estimate yet."
enum OtaAuthVerdict {
  OTA_AUTH_OK,
  OTA_AUTH_MISSING_FIELDS,        // manifest lacks a required signed field
  OTA_AUTH_UNKNOWN_KEY_ID,        // key_id doesn't match COVIO_OTA_KEY_ID
  OTA_AUTH_NO_TIME_SOURCE,        // device has never synced server_time_ms yet -- fails closed
  OTA_AUTH_TIME_INVALID,          // expired or not-yet-valid per checkManifestTimeValidity()
  OTA_AUTH_BAD_SIGNATURE_ENCODING,// signature field isn't valid base64
  OTA_AUTH_SIG_VERIFY_FAILED,     // ECDSA verification itself failed (tampered manifest, or wrong key)
};

inline const char* otaAuthVerdictStr(OtaAuthVerdict v) {
  switch (v) {
    case OTA_AUTH_OK:                      return "ok";
    case OTA_AUTH_MISSING_FIELDS:          return "missing_signed_fields";
    case OTA_AUTH_UNKNOWN_KEY_ID:          return "unknown_key_id";
    case OTA_AUTH_NO_TIME_SOURCE:          return "no_time_source";
    case OTA_AUTH_TIME_INVALID:            return "time_invalid";
    case OTA_AUTH_BAD_SIGNATURE_ENCODING:  return "bad_signature_encoding";
    case OTA_AUTH_SIG_VERIFY_FAILED:       return "signature_verify_failed";
    default:                               return "unknown";
  }
}

// Allowed clock-skew tolerance for manifest issued_at/expires_at checks,
// given this device's ONLY time source is server_time_ms piggybacked on
// its own last successful push (sync.h) -- see that file's comment on why
// there is no real RTC/NTP (RISK-11, unchanged, pre-existing gap). One hour
// is generous specifically because that estimate can be stale by however
// long it's been since the last successful push (up to OTA_POLL_MS=5min in
// normal operation, much longer if offline) -- tightening this requires
// closing RISK-11 first, not this file.
#define OTA_MANIFEST_TIME_SKEW_S (3600L)

class Ota {
public:
  void begin(Store* st) { st_ = st; }

  // Miki Wire hardening (Phase-0 findings F2/F3): optional callback invoked
  // on every iteration of the blocking image-download loop, so the caller
  // can prove watchdog liveness and keep the totalizer's PCNT drain /
  // checkpoint alive during the one legitimate multi-minute block in the
  // firmware. Unset (nullptr) = exactly the pre-hardening behavior.
  void setServiceCallback(void (*fn)()) { serviceCb_ = fn; }

  // DM-Phase 1: getter only. Precedence mirrors this class's own existing
  // fields: a confirmed trial image reports CONFIRMED even though
  // pendingVerify_ is still true (confirmHealthyBoot() never clears it);
  // otherwise a still-on-trial image reports PENDING_VERIFY; otherwise the
  // most recent update attempt's own outcome (failed_) is reported.
  OtaState state() {
    if (confirmed_)     return OTA_STATE_CONFIRMED;
    if (pendingVerify_) return OTA_STATE_PENDING_VERIFY;
    if (failed_)        return OTA_STATE_FAILED;
    return OTA_STATE_NONE;
  }

  // On boot, if the running image is pending verification, we are in the
  // trial window after an OTA. We defer marking it valid until the app has
  // demonstrably worked (see confirmHealthyBoot()).
  //
  // Root-cause audit instrumentation (35_OTA_TIME_SOURCE_REMEDIATION.../
  // this remediation): captures the RAW esp_ota_get_state_partition()
  // return code and image-state value, plus running/boot/next-update
  // partition identity, so the confirmation lifecycle is directly
  // observable instead of inferred from names. Never affects control
  // flow -- pendingVerify_'s own condition is unchanged; these fields are
  // read-only evidence, exposed via /api/v1/status's "ota_debug" object
  // (diagnostics.h), explicitly labeled as removable after certification.
  void noteBoot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    runningPartitionLabel_ = running ? String(running->label) : "?";
    runningPartitionAddr_  = running ? running->address : 0;

    const esp_partition_t* boot = esp_ota_get_boot_partition();
    bootPartitionLabel_ = boot ? String(boot->label) : "?";
    bootPartitionAddr_  = boot ? boot->address : 0;

    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    nextUpdatePartitionLabel_ = next ? String(next->label) : "?";

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t rc = esp_ota_get_state_partition(running, &state);
    stateReadErr_ = rc;
    rawImgState_ = (int)state;
    haveRawState_ = true;
    if (rc == ESP_OK) {
      pendingVerify_ = (state == ESP_OTA_IMG_PENDING_VERIFY);
      if (pendingVerify_)
        Serial.println("[OTA] running a NEW image on trial — must confirm health");
    }
    Serial.printf("[OTA][DEBUG] noteBoot: running=%s@0x%06x boot=%s@0x%06x next=%s "
                  "state_read_err=%d(%s) raw_state=%d(%s) pendingVerify=%d\n",
                  runningPartitionLabel_.c_str(), (unsigned)runningPartitionAddr_,
                  bootPartitionLabel_.c_str(), (unsigned)bootPartitionAddr_,
                  nextUpdatePartitionLabel_.c_str(),
                  (int)rc, esp_err_to_name(rc),
                  (int)state, otaImgStateStr_(state), (int)pendingVerify_);
  }

  // ---- root-cause audit getters (read-only evidence, see noteBoot()'s comment) ----
  const String& runningPartitionLabel() const { return runningPartitionLabel_; }
  uint32_t      runningPartitionAddr()  const { return runningPartitionAddr_; }
  const String& bootPartitionLabel()    const { return bootPartitionLabel_; }
  uint32_t      bootPartitionAddr()     const { return bootPartitionAddr_; }
  const String& nextUpdatePartitionLabel() const { return nextUpdatePartitionLabel_; }
  bool          haveRawState()          const { return haveRawState_; }
  esp_err_t     rawStateReadErr()       const { return stateReadErr_; }
  int           rawImgState()           const { return rawImgState_; }
  bool          confirmAttempted()      const { return confirmAttempted_; }
  esp_err_t     confirmReturnCode()     const { return confirmReturnCode_; }
  bool          floorWriteAttempted()   const { return floorWriteAttempted_; }
  bool          floorWriteOk()          const { return floorWriteOk_; }
  uint32_t      floorReadAfterWrite()   const { return floorReadAfterWrite_; }
  bool          bootloaderRollbackEngaged() const { return bootloaderRollbackEngaged_; }

  static const char* otaImgStateStr_(esp_ota_img_states_t s) {
    switch (s) {
      case ESP_OTA_IMG_NEW:             return "NEW";
      case ESP_OTA_IMG_PENDING_VERIFY:  return "PENDING_VERIFY";
      case ESP_OTA_IMG_VALID:           return "VALID";
      case ESP_OTA_IMG_INVALID:         return "INVALID";
      case ESP_OTA_IMG_ABORTED:         return "ABORTED";
      case ESP_OTA_IMG_UNDEFINED:       return "UNDEFINED";
      default:                          return "UNKNOWN_ENUM_VALUE";
    }
  }

  // Call once the device has proven itself healthy this boot
  // (WiFi connected AND at least one successful server ack or config fetch).
  //
  // Root-cause remediation (35_OTA_TIME_SOURCE_REMEDIATION.../this pass) --
  // PROVEN by raw hardware evidence, not inferred: esp_ota_get_state_partition()
  // reports "VALID" (never "NEW"/"PENDING_VERIFY") immediately after a
  // genuine, fresh OTA transition on this exact board/build, despite
  // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1 being compiled into the app's
  // own sdkconfig -- the BOOTLOADER is evidently not arming a rollback
  // trial for these transitions. Since floor advancement was previously
  // gated entirely behind `pendingVerify_` (only ever true when the
  // bootloader DOES arm a trial), the accepted-security-floor could never
  // advance on this hardware, for ANY successful build, OTA-installed or
  // even a fresh USB-flashed baseline's very first boot -- not a symptom
  // specific to OTA, a foundational gap in RISK-15's anti-downgrade floor
  // ever initializing at all.
  //
  // Per this remediation's own explicit instruction ("do not fake
  // bootloader confirmation... implement only the intended supported
  // mechanism"): bootloader-level trial cancellation
  // (esp_ota_mark_app_valid_cancel_rollback()) and APPLICATION-level
  // health confirmation (this function's actual job: proving THIS
  // build works, and advancing the floor accordingly) are now two
  // separate steps. The bootloader call is attempted -- and its result
  // checked, fail-closed -- ONLY when pendingVerify_ is genuinely true
  // (nothing to cancel otherwise; avoids a false failure against an
  // already-valid image, and stays idempotent). Floor advancement
  // proceeds on genuine app-level health proof regardless of whether a
  // bootloader trial was ever armed -- exactly what actually protects
  // against a downgrade attack (the compiled FW_SECURITY_VERSION of a
  // build that has PROVEN it can reach the server), decoupled from a
  // bootloader mechanism proven not to engage on this hardware. Which
  // path was taken is recorded (bootloaderRollbackEngaged_) and exposed
  // via diagnostics so no report ever implies bootloader protection that
  // was not actually active for a given confirmation.
  void confirmHealthyBoot() {
    if (confirmed_) return;   // idempotent -- second call is always a safe no-op

    if (pendingVerify_) {
      confirmAttempted_ = true;
      esp_err_t rc = esp_ota_mark_app_valid_cancel_rollback();
      confirmReturnCode_ = rc;
      if (rc != ESP_OK) {
        Serial.printf("[OTA][DEBUG] esp_ota_mark_app_valid_cancel_rollback FAILED: "
                      "%d(%s) -- a genuine bootloader trial existed and its "
                      "cancellation failed; NOT marking confirmed, floor NOT advanced\n",
                      (int)rc, esp_err_to_name(rc));
        return;   // fail closed -- only when a real trial existed and failed to cancel
      }
    }

    confirmed_ = true;
    bootloaderRollbackEngaged_ = pendingVerify_;
    Serial.printf("[OTA] application-level health confirmed (bootloader rollback %s)\n",
                  pendingVerify_ ? "was engaged and cancelled" : "was NOT engaged for this boot");

    // P1 hardening: records THIS image's version pair as "last known good"
    // and clears the unhealthy-boot streak (store.h/covio_firmware.ino's
    // setup()) -- the bootloader-independent half of the rollback safety
    // net. Placed alongside the (also independent) security-floor advance
    // below, both gated on the identical health proof, for the identical
    // reason: neither should ever fire on a mere reboot, only on genuine
    // proven health.
    if (st_) {
      st_->recordHealthyBoot(FW_VERSION, (uint32_t)FW_SECURITY_VERSION);
    }

    // RISK-15 remediation (OTA anti-downgrade): the accepted security-
    // version floor advances ONLY here -- once THIS running image has
    // genuinely proven itself healthy (WiFi + a real server contact this
    // boot, the caller's own gating), never at flash-write time and never
    // merely on reboot. A build that never reaches this call (crashes,
    // hangs, or -- on hardware where it genuinely IS armed -- is rolled
    // back by the bootloader before ever proving healthy) never raises
    // the floor. Monotonic: never LOWERS the floor even if this build's
    // own FW_SECURITY_VERSION were somehow less than what's already
    // stored (defensive; should not happen in normal operation, but a
    // floor must never move backward for any reason).
    if (st_ && FW_SECURITY_VERSION > (long)st_->securityVersion()) {
      floorWriteAttempted_ = true;
      uint32_t newFloor = (uint32_t)FW_SECURITY_VERSION;
      st_->setSecurityVersion(newFloor);
      uint32_t readBack = st_->securityVersion();
      floorReadAfterWrite_ = readBack;
      floorWriteOk_ = (readBack == newFloor);
      if (floorWriteOk_) {
        Serial.printf("[OTA] security-version floor advanced to %u (read-after-write verified)\n",
                      (unsigned)newFloor);
      } else {
        Serial.printf("[OTA][DEBUG] floor write MISMATCH: wrote %u, read back %u -- "
                      "NVS write may have failed\n", (unsigned)newFloor, (unsigned)readBack);
      }
    }
  }

  // Poll manifest; if a different, authentic, accepted version is offered,
  // attempt the update. DM-Phase 2 (ADR-017 WiFi-authority consolidation):
  // takes the caller's already-computed connectivity state instead of
  // independently querying WiFi.status() itself.
  //
  // RISK-16: now also takes the Sync engine, purely to read its
  // haveServerTime()/estimatedUnixNow() (this class never touches WiFi/
  // push logic itself; Sync remains the sole module owning connectivity
  // decisions, per this method's own pre-existing comment below).
  void poll(bool online, Sync& sync) {
    if (!online) return;

    // Miki Wire hardening (Phase-0 finding F7, OTA half): a persistently
    // failing download/verify cycle previously re-erased and re-downloaded
    // the inactive partition every OTA_POLL_MS (5 min) forever -- flash wear
    // plus bandwidth for zero progress. After a failed attempt the next one
    // waits out an exponential window: 10min doubling to a 1h cap. Cleared
    // by any attempt that does not end in failure. Manifest 404s ("no
    // update", the designed-safe outcome) never enter this path.
    if (otaBackoffMs_ && (millis() - lastOtaFailMs_) < otaBackoffMs_) return;

    String url = st_->serverUrl() + PATH_OTA_MANIFEST;

    // DM-Phase 5 (ADR-005): same scheme dispatch as sync.h's pushOnce()/pollConfig().
    HTTPClient http;
    WiFiClientSecure secureClient;
    bool began;
    if (covioIsHttpsUrl(url)) {
      secureClient.setCACert(COVIO_PINNED_CA_CERT);
      secureClient.setHandshakeTimeout(HTTPS_HANDSHAKE_TIMEOUT_S);   // v1.3.1: see config.h
      began = http.begin(secureClient, url);
    } else {
      began = http.begin(url);
    }
    if (!began) return;
    http.addHeader("X-Api-Key", st_->apiKey());
    http.setConnectTimeout(HTTPS_CONNECT_TIMEOUT_MS);   // v1.3.1: explicit, budgeted
    http.setTimeout(HTTPS_IO_TIMEOUT_MS);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    // RISK-16: bound the manifest body size BEFORE parsing -- an oversized
    // response (malicious or misconfigured server) must not be loaded
    // wholesale into RAM on a memory-constrained device. A real signed
    // manifest for this fixed field set is well under 1KB; 4KB is a
    // generous ceiling with margin, not a tight fit.
    int len = http.getSize();
    if (len > 4096) {
      Serial.printf("[OTA] REJECTED: manifest body too large (%d bytes)\n", len);
      http.end();
      return;
    }
    String body = http.getString();
    http.end();

    String ver = extractStr_(body, "version");
    String bin = extractStr_(body, "url");
    if (ver.length() == 0 || bin.length() == 0) return;
    if (ver == FW_VERSION) {
      // DM-Phase 1 validation-pass fix: confirming we're already on the
      // manifest's version is itself evidence any earlier failure no longer
      // applies -- without this, a device whose failed update was resolved
      // by reverting the manifest (rather than by a new successful attempt)
      // would report ota_state=failed indefinitely.
      failed_ = false;
      return;                                     // already current
    }

    // RISK-15 remediation (OTA anti-downgrade): build the candidate from
    // the manifest's OWN fields and hand the actual ACCEPT/REJECT decision
    // to evaluateOtaCandidate() (ota_version_policy.h) -- a pure function,
    // host-tested independent of this network/flash-coupled method.
    long secVer = extractLong_(body, "security_version");
    long schemaVer = extractLong_(body, "schema_version");
    String hwCompat = extractStr_(body, "hw_compat");

    OtaCandidate candidate;
    candidate.hasSecurityVersion = (secVer >= 0);
    candidate.securityVersion = secVer;
    candidate.hasHwCompat = (hwCompat.length() > 0);
    candidate.hwCompat = hwCompat.c_str();   // valid only for this call's lifetime -- fine, evaluateOtaCandidate() doesn't retain it
    candidate.hasSchemaVersion = (schemaVer >= 0);
    candidate.schemaVersion = schemaVer;

    OtaVerdict verdict = evaluateOtaCandidate(candidate, st_->securityVersion(),
                                               DEVICE_MODEL, SCHEMA_VERSION_CURRENT);
    if (verdict != OTA_ACCEPT) {
      Serial.printf("[OTA] REJECTED candidate %s: %s\n", ver.c_str(), otaVerdictStr(verdict));
      lastRejectReason_ = verdict;
      return;
    }

    // RISK-16 remediation: manifest AUTHENTICITY gate -- signature, key id,
    // and expiry/skew, ALL evaluated before any download attempt. This is
    // deliberately checked AFTER the anti-downgrade gate above (cheaper,
    // no crypto, rejects the overwhelmingly common "nothing new" case
    // fastest) but BEFORE any network fetch of the actual image.
    long imageSize = extractLong_(body, "image_size");
    String imageSha256 = extractStr_(body, "image_sha256");
    String channel = extractStr_(body, "channel");
    long issuedAt = extractLong_(body, "issued_at");
    long expiresAt = extractLong_(body, "expires_at");
    String manifestId = extractStr_(body, "manifest_id");
    String keyId = extractStr_(body, "key_id");
    String sigB64 = extractStr_(body, "signature");

    // P1 hardening: this presence check moved to manifestHasRequiredFields()
    // in ota_manifest_auth.h (host-testable, see that file's comment) --
    // hw_compat is now included in the required set, where it was
    // previously optional-by-presence (see that function's own comment).
    if (!manifestHasRequiredFields(imageSize, imageSha256.length(), channel.length(),
                                    issuedAt, expiresAt, manifestId.length(),
                                    keyId.length(), sigB64.length(), hwCompat.length())) {
      Serial.println("[OTA] REJECTED: manifest missing one or more required signed fields");
      lastAuthReject_ = OTA_AUTH_MISSING_FIELDS;
      return;
    }

    SignedManifestFields fields;
    fields.hwCompat = hwCompat.c_str();
    fields.version = ver.c_str();
    fields.securityVersion = secVer;
    fields.schemaVersion = schemaVer;
    fields.imageSize = imageSize;
    fields.imageSha256Hex = imageSha256.c_str();
    fields.imageUrl = bin.c_str();
    fields.channel = channel.c_str();
    fields.issuedAt = issuedAt;
    fields.expiresAt = expiresAt;
    fields.manifestId = manifestId.c_str();

    OtaAuthVerdict authVerdict = verifyManifestAuthenticity_(fields, keyId, sigB64, sync);
    if (authVerdict != OTA_AUTH_OK) {
      Serial.printf("[OTA] REJECTED candidate %s: authenticity check failed (%s)\n",
                    ver.c_str(), otaAuthVerdictStr(authVerdict));
      lastAuthReject_ = authVerdict;
      return;
    }

    Serial.printf("[OTA] update offered: %s (running %s) -- authenticity verified\n",
                  ver.c_str(), FW_VERSION);
    doVerifiedUpdate_(bin, (size_t)imageSize, imageSha256);
    // F7 (OTA half): a successful update never returns here (ESP.restart);
    // reaching this line means the attempt failed -- widen the retry window.
    // doVerifiedUpdate_ clears failed_ at entry, so this reflects THIS attempt.
    if (failed_) {
      lastOtaFailMs_ = millis();
      otaBackoffMs_ = otaBackoffMs_
                        ? min<uint32_t>(otaBackoffMs_ * 2, 3600000UL)
                        : (uint32_t)(2 * OTA_POLL_MS);
      Serial.printf("[OTA] download/verify failed -- next attempt in >= %us\n",
                    (unsigned)(otaBackoffMs_ / 1000));
    } else {
      otaBackoffMs_ = 0;
    }
  }

  // DM-Phase 1 diagnostics: getter only, so /api/v1/status can report WHY
  // the most recent candidate was rejected (if it was), not just that OTA
  // is idle. RISK-15 remediation.
  OtaVerdict lastRejectReason() { return lastRejectReason_; }
  // RISK-16 remediation: same idea, for the authenticity gate specifically.
  OtaAuthVerdict lastAuthRejectReason() { return lastAuthReject_; }

  // DM-Phase 2: promoted from private to public (no behavior/signature
  // change) so wifi_provision.h's /api/v1/config JSON handler can reuse this
  // exact extractor rather than reimplementing it a third time (sync.h
  // already has its own copy for a different response shape).
  static String extractStr_(const String& s, const char* key) {
    String pat = "\"" + String(key) + "\"";
    int i = s.indexOf(pat);
    if (i < 0) return "";
    i = s.indexOf(':', i); if (i < 0) return "";
    i = s.indexOf('"', i); if (i < 0) return "";
    int j = s.indexOf('"', i+1); if (j < 0) return "";
    return s.substring(i+1, j);
  }

  // RISK-15 remediation: a small integer extractor for the new manifest
  // fields (security_version/schema_version/image_size/issued_at/expires_at).
  // Deliberately a SEPARATE tiny copy rather than sharing sync.h's private
  // extractLong_ -- matches this codebase's own already-established
  // per-class-extractor precedent. Returns -1 for "key absent, or value not
  // a plain non-negative integer" -- callers treat -1 as "not present"/
  // "malformed", never as a literal value.
  static long extractLong_(const String& s, const char* key) {
    String pat = "\"" + String(key) + "\"";
    int i = s.indexOf(pat);
    if (i < 0) return -1;
    i = s.indexOf(':', i);
    if (i < 0) return -1;
    i++;
    while (i < (int)s.length() && (s[i] == ' ' || s[i] == '"')) i++;
    long v = 0; bool any = false;
    while (i < (int)s.length() && isdigit(s[i])) { v = v * 10 + (s[i] - '0'); i++; any = true; }
    return any ? v : -1;
  }

private:
  // RISK-16 remediation: verifies key_id, time validity, and the ECDSA-P256
  // signature over the manifest's canonical field string -- via mbedTLS,
  // already linked into every build via WiFiClientSecure's own TLS usage
  // (no new library dependency). Returns OTA_AUTH_OK only if every check
  // passes; the caller never proceeds to doVerifiedUpdate_() otherwise.
  OtaAuthVerdict verifyManifestAuthenticity_(const SignedManifestFields& fields,
                                              const String& keyId, const String& sigB64,
                                              Sync& sync) {
    if (keyId != COVIO_OTA_KEY_ID) return OTA_AUTH_UNKNOWN_KEY_ID;

    // RISK-16 + RISK-11 interaction, stated explicitly: this device has no
    // RTC/NTP. If it has never successfully synced server_time_ms yet
    // (fresh boot, no connectivity yet), there is NO way to check
    // issued_at/expires_at at all -- failing OPEN (accepting regardless of
    // expiry) would defeat the entire point of an expiry check. This fails
    // CLOSED instead: no time source yet means no OTA yet. In practice this
    // is a narrow window (OTA polling only starts after the device is
    // already online, and telemetry pushes every 5s, so a real
    // server_time_ms is almost always already cached by the time
    // OTA_POLL_MS's first 5-minute mark arrives).
    if (!sync.haveServerTime()) return OTA_AUTH_NO_TIME_SOURCE;

    ManifestTimeVerdict tv = checkManifestTimeValidity(
        fields.issuedAt, fields.expiresAt, sync.estimatedUnixNow(), OTA_MANIFEST_TIME_SKEW_S);
    if (tv != MANIFEST_TIME_OK) return OTA_AUTH_TIME_INVALID;

    char canonical[512];
    int n = buildCanonicalManifestString(fields, canonical, sizeof(canonical));
    if (n < 0) return OTA_AUTH_MISSING_FIELDS;

    uint8_t sigDer[256];
    size_t sigLen = 0;
    int b64rc = mbedtls_base64_decode(sigDer, sizeof(sigDer), &sigLen,
                                       (const unsigned char*)sigB64.c_str(), sigB64.length());
    if (b64rc != 0) return OTA_AUTH_BAD_SIGNATURE_ENCODING;

    uint8_t hash[32];
    mbedtls_sha256((const unsigned char*)canonical, n, hash, 0);

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int prc = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)COVIO_OTA_PUBLIC_KEY_PEM,
                                           strlen(COVIO_OTA_PUBLIC_KEY_PEM) + 1);
    if (prc != 0) {
      mbedtls_pk_free(&pk);
      Serial.println("[OTA] compiled-in public key failed to parse -- ota_keys.h misconfigured?");
      return OTA_AUTH_SIG_VERIFY_FAILED;
    }
    int vrc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sigDer, sigLen);
    mbedtls_pk_free(&pk);
    if (vrc != 0) return OTA_AUTH_SIG_VERIFY_FAILED;

    return OTA_AUTH_OK;
  }

  // RISK-16 remediation: REPLACES the previous httpUpdate.update()-based
  // doUpdate_(). The Arduino HTTPUpdate library downloads AND commits the
  // new boot partition in one opaque call, with no hook to verify the
  // image's hash BEFORE that commit happens -- incompatible with "set the
  // candidate boot partition only after every verification succeeds"
  // (mandate requirement). This streams the download manually via
  // HTTPClient + Update.h instead: SHA-256 is computed incrementally as
  // bytes arrive, and Update.end() (the actual esp_ota_set_boot_partition()
  // moment) is only ever called if the completed hash matches the signed
  // manifest's claimed hash. Any failure path calls Update.abort() instead
  // -- the inactive partition may contain partial/wrong bytes, but nothing
  // ever points the bootloader at it.
  void doVerifiedUpdate_(const String& binUrl, size_t expectedSize, const String& expectedSha256Hex) {
    failed_ = false;

    HTTPClient http;
    WiFiClientSecure secureClient;
    bool began;
    if (covioIsHttpsUrl(binUrl)) {
      secureClient.setCACert(COVIO_PINNED_CA_CERT);
      secureClient.setHandshakeTimeout(HTTPS_HANDSHAKE_TIMEOUT_S);   // v1.3.1: see config.h
      began = http.begin(secureClient, binUrl);
    } else {
      began = http.begin(binUrl);   // bench/dev http:// fallback -- see config.h
    }
    if (!began) { Serial.println("[OTA] download begin FAILED"); failed_ = true; return; }
    // v1.3.1: the pre-stream part of the download (connect, handshake,
    // headers) must fit the same watchdog budget as every other call; the
    // streaming loop below keeps its own 15 s stall detector and feeds the
    // watchdog through serviceCb_ while bytes flow.
    http.setConnectTimeout(HTTPS_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTPS_IO_TIMEOUT_MS);

    int code = http.GET();
    if (code != 200) {
      Serial.printf("[OTA] download HTTP %d -- current firmware unaffected\n", code);
      http.end();
      failed_ = true;
      return;
    }

    // Content-Length vs. the SIGNED manifest's declared image_size: reject
    // before writing a single byte if they disagree (mandate: "image size
    // mismatch rejected"). A server returning no Content-Length at all
    // (getSize() < 0, e.g. chunked transfer) is not rejected here -- the
    // post-download total-bytes-written check below still catches a
    // truncated/oversized transfer either way.
    int contentLen = http.getSize();
    if (contentLen >= 0 && (size_t)contentLen != expectedSize) {
      Serial.printf("[OTA] REJECTED: Content-Length (%d) != manifest image_size (%u) -- "
                    "current firmware unaffected\n", contentLen, (unsigned)expectedSize);
      http.end();
      failed_ = true;
      return;
    }

    if (!Update.begin(expectedSize, U_FLASH)) {
      Serial.printf("[OTA] Update.begin FAILED: %s -- current firmware unaffected\n",
                    Update.errorString());
      http.end();
      failed_ = true;
      return;
    }

    WiFiClient* stream = http.getStreamPtr();
    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts(&shaCtx, 0);   // 0 = SHA-256, not SHA-224

    uint8_t buf[1024];
    size_t totalWritten = 0;
    uint32_t lastDataMs = millis();
    bool ioError = false;

    while (totalWritten < expectedSize) {
      // Miki Wire hardening (Phase-0 findings F2/F3): this download is the
      // one legitimate multi-minute block in the firmware. The service
      // callback (wired by covio_firmware.ino) feeds the task watchdog and
      // keeps the totalizer's PCNT drain/checkpoint alive while bytes flow.
      // Liveness here is genuine, not blind: a dead transfer exits within
      // 15s via the stall detector below, after which the main loop's own
      // feed takes over.
      if (serviceCb_) serviceCb_();
      size_t avail = stream->available();
      if (avail == 0) {
        if (!http.connected()) break;   // connection closed -- loop exit check below decides truncated vs. complete
        if (millis() - lastDataMs > 15000) { ioError = true; break; }   // stalled too long
        delay(5);
        continue;
      }
      size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
      size_t got = stream->readBytes(buf, toRead);
      if (got == 0) continue;
      lastDataMs = millis();
      mbedtls_sha256_update(&shaCtx, buf, got);
      size_t written = Update.write(buf, got);
      if (written != got) { ioError = true; break; }
      totalWritten += got;
    }
    http.end();

    uint8_t computedHash[32];
    mbedtls_sha256_finish(&shaCtx, computedHash);
    mbedtls_sha256_free(&shaCtx);

    if (ioError || totalWritten != expectedSize) {
      Serial.printf("[OTA] download incomplete (%u/%u bytes) -- aborting, "
                    "current firmware unaffected\n", (unsigned)totalWritten, (unsigned)expectedSize);
      Update.abort();
      failed_ = true;
      return;
    }

    char computedHex[65];
    for (int i = 0; i < 32; i++) snprintf(computedHex + i * 2, 3, "%02x", computedHash[i]);
    computedHex[64] = '\0';

    if (!expectedSha256Hex.equalsIgnoreCase(computedHex)) {
      Serial.printf("[OTA] HASH MISMATCH -- expected %s got %s -- aborting, "
                    "current firmware unaffected\n", expectedSha256Hex.c_str(), computedHex);
      Update.abort();
      failed_ = true;
      return;
    }

    // Hash matches the SIGNED value -- NOW, and only now, commit: this is
    // the actual esp_ota_set_boot_partition() moment (inside Update.end()).
    if (!Update.end(true)) {
      Serial.printf("[OTA] Update.end FAILED: %s -- current firmware unaffected\n",
                    Update.errorString());
      failed_ = true;
      return;
    }

    Serial.println("[OTA] verified OK — rebooting into new image");
    ESP.restart();
  }

  Store* st_ = nullptr;
  void (*serviceCb_)() = nullptr;   // Miki Wire hardening (F2/F3): see doVerifiedUpdate_
  uint32_t otaBackoffMs_ = 0;       // F7 (OTA half): 0 = no backoff active
  uint32_t lastOtaFailMs_ = 0;
  bool pendingVerify_ = false;
  bool confirmed_ = false;
  bool failed_ = false;   // DM-Phase 1: outcome of the most recent update attempt
  OtaVerdict lastRejectReason_ = OTA_ACCEPT;  // RISK-15: OTA_ACCEPT here means "nothing
                                              // has been rejected yet this boot", not that
                                              // a candidate was accepted -- see poll()
  OtaAuthVerdict lastAuthReject_ = OTA_AUTH_OK;  // RISK-16: same "nothing rejected yet" meaning

  // ---- root-cause audit instrumentation (this remediation) -- read-only
  // evidence, never affects control flow, see noteBoot()'s own comment ----
  String   runningPartitionLabel_ = "?";
  uint32_t runningPartitionAddr_ = 0;
  String   bootPartitionLabel_ = "?";
  uint32_t bootPartitionAddr_ = 0;
  String   nextUpdatePartitionLabel_ = "?";
  bool     haveRawState_ = false;
  esp_err_t stateReadErr_ = ESP_FAIL;
  int      rawImgState_ = -1;
  bool     confirmAttempted_ = false;
  esp_err_t confirmReturnCode_ = ESP_FAIL;
  bool     floorWriteAttempted_ = false;
  bool     floorWriteOk_ = false;
  uint32_t floorReadAfterWrite_ = 0;
  bool     bootloaderRollbackEngaged_ = false;
};
