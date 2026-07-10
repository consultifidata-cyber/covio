// ============================================================================
// ota.h  —  pull-based OTA with version manifest + rollback safety
// ----------------------------------------------------------------------------
// FLOW:
//   1. Poll server_url + PATH_OTA_MANIFEST -> {"version":"1.1.0","url":"..."}
//   2. If manifest.version != FW_VERSION, download that .bin via HTTPUpdate.
//   3. HTTPUpdate writes to the INACTIVE OTA partition and reboots into it.
//   4. On next boot, main.ino calls confirmHealthyBoot() AFTER the device has
//      proven it can run (WiFi up + one successful server contact). That call
//      marks the new image valid. If the new image crashes/hangs BEFORE that,
//      the bootloader rolls back to the previous good partition automatically.
//
// ############################################################################
// # SECURITY — READ BEFORE FIELD DEPLOYMENT                                   #
// # Pull OTA means the device DOWNLOADS AND EXECUTES whatever is at that URL.  #
// # For a device you cannot physically reach, the minimum bar is:             #
// #   1. Serve the manifest AND the .bin over HTTPS with a valid cert, and    #
// #      pin the server CA (setCACert) — NOT setInsecure().                    #
// #      DM-Phase 5 / ADR-005: DONE below (covioIsHttpsUrl() dispatch, both   #
// #      poll() and doUpdate_()) — genuine firmware code, implemented and     #
// #      reviewed in this environment.                                        #
// #   2. Enable Secure Boot + signed app images so a tampered/rogue binary     #
// #      is rejected by the bootloader even if the URL is compromised.         #
// #      NOT DONE here — this is a one-way eFuse burn performed once at the    #
// #      first factory flash via espsecure.py/esptool.py, not firmware        #
// #      source code, and cannot be implemented or verified without real      #
// #      ESP32 hardware (neither of which exists in this environment). See    #
// #      Docs/DM-Phase5-Secure-Boot-Factory-Provisioning.md for the full,     #
// #      not-yet-executed procedure.                                          #
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
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "esp_ota_ops.h"
#include "store.h"
#include "certs.h"

// ---- DM-Phase 1 (local diagnostics, §13 A.4 ota_state enum) --------------
enum OtaState { OTA_STATE_NONE, OTA_STATE_PENDING_VERIFY, OTA_STATE_CONFIRMED, OTA_STATE_FAILED };

class Ota {
public:
  void begin(Store* st) { st_ = st; }

  // DM-Phase 1: getter only. Precedence mirrors this class's own existing
  // fields: a confirmed trial image reports CONFIRMED even though
  // pendingVerify_ is still true (confirmHealthyBoot() never clears it);
  // otherwise a still-on-trial image reports PENDING_VERIFY; otherwise the
  // most recent doUpdate_() attempt's own outcome (failed_) is reported.
  OtaState state() {
    if (confirmed_)     return OTA_STATE_CONFIRMED;
    if (pendingVerify_) return OTA_STATE_PENDING_VERIFY;
    if (failed_)        return OTA_STATE_FAILED;
    return OTA_STATE_NONE;
  }

  // On boot, if the running image is pending verification, we are in the
  // trial window after an OTA. We defer marking it valid until the app has
  // demonstrably worked (see confirmHealthyBoot()).
  void noteBoot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
      pendingVerify_ = (state == ESP_OTA_IMG_PENDING_VERIFY);
      if (pendingVerify_)
        Serial.println("[OTA] running a NEW image on trial — must confirm health");
    }
  }

  // Call once the device has proven itself healthy this boot
  // (WiFi connected AND at least one successful server ack or config fetch).
  // This cancels the pending rollback and commits the new firmware.
  void confirmHealthyBoot() {
    if (!pendingVerify_ || confirmed_) return;
    esp_ota_mark_app_valid_cancel_rollback();
    confirmed_ = true;
    Serial.println("[OTA] new image confirmed valid — rollback cancelled");
  }

  // Poll manifest; if a different version is offered, attempt the update.
  // DM-Phase 2 (ADR-017 WiFi-authority consolidation): takes the caller's
  // already-computed connectivity state instead of independently querying
  // WiFi.status() itself -- this and sync.h were the two places that used
  // to each poll it separately (Blueprint Task 4/5's documented coupling
  // risk). Sync remains the sole module that ever calls WiFi.status()/
  // WiFi.begin() for connectivity-decision purposes; no new header
  // dependency is introduced here (the caller, covio_firmware.ino, is what
  // wires syncEngine.online() through).
  void poll(bool online) {
    if (!online) return;

    String url = st_->serverUrl() + PATH_OTA_MANIFEST;

    // DM-Phase 5 (ADR-005): same scheme dispatch as sync.h's pushOnce()/pollConfig().
    HTTPClient http;
    WiFiClientSecure secureClient;
    bool began;
    if (covioIsHttpsUrl(url)) {
      secureClient.setCACert(COVIO_PINNED_CA_CERT);
      began = http.begin(secureClient, url);
    } else {
      began = http.begin(url);
    }
    if (!began) return;
    http.addHeader("X-Api-Key", st_->apiKey());
    http.setTimeout(6000);
    int code = http.GET();
    if (code != 200) { http.end(); return; }
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
      // would report ota_state=failed indefinitely, since doUpdate_() -- the
      // only other place failed_ is cleared -- would never run again.
      failed_ = false;
      return;                                     // already current
    }

    Serial.printf("[OTA] update offered: %s (running %s)\n", ver.c_str(), FW_VERSION);
    doUpdate_(bin);
  }

  // DM-Phase 2: promoted from private to public (no behavior/signature
  // change) so wifi_provision.h's /api/v1/config JSON handler can reuse this
  // exact extractor rather than reimplementing it a third time (sync.h
  // already has its own copy for a different response shape) -- see
  // MASTER_GOVERNANCE.md's own note on this project having already paid
  // once for that duplication pattern.
  static String extractStr_(const String& s, const char* key) {
    String pat = "\"" + String(key) + "\"";
    int i = s.indexOf(pat);
    if (i < 0) return "";
    i = s.indexOf(':', i); if (i < 0) return "";
    i = s.indexOf('"', i); if (i < 0) return "";
    int j = s.indexOf('"', i+1); if (j < 0) return "";
    return s.substring(i+1, j);
  }

private:
  void doUpdate_(const String& binUrl) {
    failed_ = false;                              // DM-Phase 1: each new attempt
                                                    // starts fresh -- state()
                                                    // reflects the MOST RECENT
                                                    // attempt only.
    httpUpdate.rebootOnUpdate(true);              // reboot into new image on success
    // DM-Phase 5 (ADR-005): the .bin download itself is the 4th of the "four
    // cloud endpoints" that must move to pinned HTTPS -- same scheme
    // dispatch as poll() above, applied to the binary transfer.
    t_httpUpdate_return ret;
    if (covioIsHttpsUrl(binUrl)) {
      WiFiClientSecure secureClient;
      secureClient.setCACert(COVIO_PINNED_CA_CERT);
      // Pass current version so a well-behaved server can 304 if unchanged.
      ret = httpUpdate.update(secureClient, binUrl, FW_VERSION);
    } else {
      WiFiClient client;                          // bench/dev http:// fallback -- see config.h
      ret = httpUpdate.update(client, binUrl, FW_VERSION);
    }
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        failed_ = true;                            // DM-Phase 1: getter only,
                                                     // no change to behavior below
        Serial.printf("[OTA] FAILED (%d): %s\n",
                      httpUpdate.getLastError(),
                      httpUpdate.getLastErrorString().c_str());
        break;                                    // stay on current image
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("[OTA] server says no update");
        break;
      case HTTP_UPDATE_OK:
        Serial.println("[OTA] OK — rebooting");   // (device reboots)
        break;
    }
  }

  Store* st_ = nullptr;
  bool pendingVerify_ = false;
  bool confirmed_ = false;
  bool failed_ = false;   // DM-Phase 1: outcome of the most recent doUpdate_() attempt
};
