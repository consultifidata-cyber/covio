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
  void factoryReset() { p_.clear(); }
  void     setCalib(float k, float d, float tr) {
    p_.putFloat("kfactor", k); p_.putFloat("density", d); p_.putFloat("tref", tr);
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
  uint32_t bootId_ = 0;
};
