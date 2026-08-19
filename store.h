// ============================================================================
// store.h  —  NVS-backed global config + identity
// ----------------------------------------------------------------------------
// INVARIANT (from your architecture doc): NVS holds ONLY globals —
// server_url, api_key, wifi creds, config_version, boot_id. NEVER per-event
// state (seq counters per record, queue contents). Per-event durability is the
// SD queue's job. Keeping these separate is what prevents NVS flash wear and
// keeps recovery reasoning simple.
// ============================================================================
#pragma once
#include <Preferences.h>
#include <Arduino.h>
#include "config.h"

class Store {
public:
  void begin() {
    // RISK-15 remediation (OTA anti-downgrade): a SEPARATE NVS namespace,
    // deliberately never touched by factoryReset() below. If the
    // accepted-security-version floor lived in the same namespace as
    // everything else, an ordinary factory reset would silently erase it --
    // and a downgrade-after-reset is exactly the attack this floor exists
    // to prevent (a stolen or "refurbished" device could otherwise be
    // reset, then downgraded to an older, vulnerable image, then have its
    // credentials re-provisioned). See ota_version_policy.h /
    // Docs/audit/coviu_oil_meter_p0_remediation_phase2/04_OTA_ANTI_DOWNGRADE_DESIGN.md.
    // A genuine secure factory-refurbishment procedure that legitimately
    // needs to lower this floor is intentionally NOT implemented here --
    // out of scope for this pass, would need its own authorized, audited
    // mechanism, not a side effect of the existing consumer-facing reset.
    p_sec_.begin(NVS_NS_SECURITY, false);
    p_.begin(NVS_NS, false);
    // On very first boot the keys are absent -> seed from build defaults.
    if (!p_.isKey("seeded")) {
      p_.putString("server_url", DEFAULT_SERVER_URL);
      p_.putString("api_key",    DEFAULT_API_KEY);
      p_.putString("wifi_ssid",  DEFAULT_WIFI_SSID);
      p_.putString("wifi_pass",  DEFAULT_WIFI_PASS);
      p_.putUInt  ("cfg_ver",    0);
      p_.putULong ("boot_id",    0);
      p_.putBool  ("seeded",     true);
    }
    // boot_id increments once per power-up. It disambiguates seq numbers
    // across reboots so the server's UNIQUE(device_id,boot_id,seq) holds.
    bootId_ = p_.getULong("boot_id", 0) + 1;
    p_.putULong("boot_id", bootId_);

    // Balaji V1 freeze remediation (Product Readiness Review P1-1):
    // lifetime restart counter, incremented once per boot right alongside
    // boot_id above -- same namespace, same lifecycle (both reset together
    // on factoryReset() below, consistent with every other operational
    // field here). Distinct from boot_id itself (which also disambiguates
    // telemetry seq numbers and must never be repurposed for anything
    // else) -- this is a plain, single-purpose observability counter.
    p_.putUInt("restart_cnt", p_.getUInt("restart_cnt", 0) + 1);
  }

  // ---- identity ----
  String  deviceId()   { return chipId(); }         // stable, from MAC
  uint32_t bootId()    { return bootId_; }

  // ---- endpoint / auth (the upstream-agnostic bit) ----
  String serverUrl()   { return p_.getString("server_url", DEFAULT_SERVER_URL); }
  String apiKey()      { return p_.getString("api_key",    DEFAULT_API_KEY); }
  void   setServerUrl(const String& v) { p_.putString("server_url", v); }
  void   setApiKey   (const String& v) { p_.putString("api_key",    v); }

  // ---- DM-Phase 6 (§11.2 / ADR-018): Logical Device ID, the manufacturing-
  // serial identity tier. "" (empty string, the NVS default for an absent
  // key) is this field's "unassigned" state -- diagnostics.h maps "" to
  // JSON null (§13 A.3's frozen "logical_device_id": null contract,
  // implemented since DM-Phase 1, populated for real starting here).
  // Write-once enforcement is the CALLER's job (local_api.h's new factory
  // endpoint), not this accessor's -- store.h has never enforced business
  // rules for any other field either (setApiKey/setServerUrl are equally
  // unconditional), staying a pure NVS accessor throughout.
  String logicalDeviceId()                    { return p_.getString("logical_id", ""); }
  void   setLogicalDeviceId(const String& v)  { p_.putString("logical_id", v); }

  // ---- wifi ----
  String wifiSsid()    { return p_.getString("wifi_ssid", DEFAULT_WIFI_SSID); }
  String wifiPass()    { return p_.getString("wifi_pass", DEFAULT_WIFI_PASS); }
  void   setWifi(const String& s, const String& pw) {
    p_.putString("wifi_ssid", s); p_.putString("wifi_pass", pw);
  }

  // ---- calibration config version (K-factor itself is NOT stored here;
  //      it lives server-side. We only cache the version + values we were told
  //      so telemetry can stamp which calibration was in effect.) ----
  uint32_t cfgVer()               { return p_.getUInt("cfg_ver", 0); }
  void     setCfgVer(uint32_t v)  { p_.putUInt("cfg_ver", v); }

  // Cache last-known K/density/Tref so the device can compute a *display-only*
  // flow rate offline. The AUTHORITATIVE litre calc is done server-side from
  // raw pulses — see calc note in telemetry.h.
  float    kFactor()              { return p_.getFloat("kfactor", 0.0f); }
  float    density()              { return p_.getFloat("density", 0.0f); }
  float    tRef()                 { return p_.getFloat("tref",    15.0f); }

  // Wipe all NVS keys in our namespace; next boot re-seeds from config.h.
  // RISK-15: deliberately does NOT clear the security-version floor
  // namespace (p_sec_) -- see begin()'s comment above for why.
  void factoryReset() { p_.clear(); }

  // ---- RISK-15 remediation (OTA anti-downgrade security-version floor) ----
  // The highest FW_SECURITY_VERSION any firmware image has ever been
  // CONFIRMED healthy on this device (see ota.h::confirmHealthyBoot()) --
  // a durable software monotonic gate, not an eFuse-based hardware
  // anti-rollback (burning eFuses is out of scope for this pilot stage;
  // see the design doc for the explicit tradeoff this accepts: a physical
  // NVS-partition erase via esptool could still reset this floor, which an
  // eFuse-based mechanism would not permit -- documented, not hidden).
  uint32_t securityVersion()              { return p_sec_.getUInt("sec_ver", 0); }
  void     setSecurityVersion(uint32_t v) { p_sec_.putUInt("sec_ver", v); }

  // ---- P1 hardening: app-level unhealthy-boot rollback safety net --------
  // Bootloader-level PENDING_VERIFY does not reliably arm on this hardware/
  // toolchain (see ota.h::confirmHealthyBoot()'s own extensive comment and
  // 36_OTA_CONFIRMATION_ROOT_CAUSE_AUDIT_AND_REMEDIATION.md). This tracks,
  // independently of the bootloader, whether the CURRENTLY RUNNING image
  // (identified by its own FW_VERSION/FW_SECURITY_VERSION, not by any
  // bootloader-reported partition state) has ever reached application-level
  // health confirmation before, and how many consecutive boots have failed
  // to reach it. Lives in the SAME namespace as the security floor above
  // (survives factoryReset()) deliberately: this is OTA-image safety state,
  // not ordinary reprovisioning config -- a factory reset (WiFi/server/key)
  // must not silently reset the crash-loop protection for a still-bad image.
  String   lastConfirmedFwVersion()       { return p_sec_.getString("last_ok_ver", ""); }
  uint32_t lastConfirmedSecurityVersion() { return p_sec_.getUInt("last_ok_secver", 0); }
  uint32_t unhealthyBootStreak()          { return p_sec_.getUInt("unhealthy_streak", 0); }
  void     incrementUnhealthyBootStreak() { p_sec_.putUInt("unhealthy_streak", unhealthyBootStreak() + 1); }
  // Called once THIS running image has been proven healthy (see
  // ota.h::confirmHealthyBoot()) -- records it as the new "last known good"
  // image identity and clears the streak, the same idempotent-write shape
  // as clearCrashResetStreak() below for the unrelated ordinary-crash counter.
  void recordHealthyBoot(const String& fwVersion, uint32_t securityVersion) {
    p_sec_.putString("last_ok_ver", fwVersion);
    p_sec_.putUInt("last_ok_secver", securityVersion);
    if (unhealthyBootStreak() != 0) p_sec_.putUInt("unhealthy_streak", 0);
  }
  void     setCalib(float k, float d, float tr) {
    p_.putFloat("kfactor", k); p_.putFloat("density", d); p_.putFloat("tref", tr);
  }

#if MIKI_WIRE_PROFILE
  // ---- Miki Wire profile tunables (validated NVS config) -------------------
  // Unlike the pure accessors above, these setters VALIDATE -- they are the
  // write path for safety-relevant monitor thresholds typed over an
  // unauthenticated serial console, so an out-of-bounds value is refused
  // (returns false) rather than stored. Reads fall back to the compiled
  // defaults (0 = feature inert) on absent/corrupt keys, so bad NVS can
  // never prevent boot or invent a threshold. Compile-time absent from the
  // Balaji flag-less build, like everything MIKI_WIRE_PROFILE.
  uint32_t mikiMaxPulseHz() { return p_.getULong("mw_max_hz", MIKI_MAX_PULSE_HZ_DEFAULT); }
  bool setMikiMaxPulseHz(uint32_t v) {
    if (v > MIKI_MAX_PULSE_HZ_LIMIT) return false;          // 0 (= off) is always valid
    p_.putULong("mw_max_hz", v);
    return true;
  }
  uint32_t mikiSuspectGapS() { return p_.getULong("mw_suspect_s", MIKI_SUSPECT_GAP_S_DEFAULT); }
  bool setMikiSuspectGapS(uint32_t v) {
    if (v != 0 && (v < MIKI_SUSPECT_GAP_S_MIN || v > MIKI_SUSPECT_GAP_S_MAX)) return false;
    p_.putULong("mw_suspect_s", v);
    return true;
  }
#endif

  // ---- Balaji V1 freeze remediation (Product Readiness Review P1-1): -----
  // persistent, NVS-backed diagnostic counters. Lifetime counts since last
  // factory reset (same lifecycle as boot_id/restart_cnt above -- all live
  // in the "covio" namespace and reset together via factoryReset(), which
  // is the existing, unchanged behavior for every other operational field
  // here). Purely additive: no existing field, method, or behavior above
  // is modified by any of this.
  uint32_t restartCount()       { return p_.getUInt("restart_cnt", 0); }
  uint32_t watchdogResetCount() { return p_.getUInt("wdt_cnt", 0); }
  uint32_t brownoutResetCount() { return p_.getUInt("bod_cnt", 0); }
  uint32_t pushFailCount()      { return p_.getUInt("pushfail_cnt", 0); }
  uint32_t wifiReconnectCount() { return p_.getUInt("wifirecon_cnt", 0); }
  uint32_t crashResetStreak()   { return p_.getUInt("crash_streak", 0); }

  void incrementWatchdogResetCount() { p_.putUInt("wdt_cnt", watchdogResetCount() + 1); }
  void incrementBrownoutResetCount() { p_.putUInt("bod_cnt", brownoutResetCount() + 1); }
  void incrementPushFailCount()      { p_.putUInt("pushfail_cnt", pushFailCount() + 1); }
  void incrementWifiReconnectCount() { p_.putUInt("wifirecon_cnt", wifiReconnectCount() + 1); }
  void incrementCrashResetStreak()   { p_.putUInt("crash_streak", crashResetStreak() + 1); }
  // Called once this boot has proven itself healthy for
  // HEALTHY_UPTIME_CLEARS_CRASH_STREAK_MS (config.h) -- a sustained good
  // run is evidence this is not a crash loop, whatever caused past resets.
  // No-op (and no redundant NVS write) if the streak is already 0.
  void clearCrashResetStreak() {
    if (crashResetStreak() != 0) p_.putUInt("crash_streak", 0);
  }

private:
  static String chipId() {
    uint64_t mac = ESP.getEfuseMac();
    char buf[20];
    snprintf(buf, sizeof(buf), "esp32-%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);
    return String(buf);
  }
  Preferences p_;
  Preferences p_sec_;   // RISK-15: separate namespace, survives factoryReset()
  uint32_t bootId_ = 0;
};
