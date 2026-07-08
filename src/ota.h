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
// #   2. Enable Secure Boot + signed app images so a tampered/rogue binary     #
// #      is rejected by the bootloader even if the URL is compromised.         #
// # This file ships with an HTTP path so your BENCH stub works. Do NOT deploy  #
// # to site over plain HTTP. The TODO markers show exactly where TLS goes.     #
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
#include "esp_ota_ops.h"
#include "store.h"

// #include <WiFiClientSecure.h>   // <-- TODO(field): use this instead of WiFiClient

class Ota {
public:
  void begin(Store* st) { st_ = st; }

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
  void poll() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = st_->serverUrl() + PATH_OTA_MANIFEST;
    if (!http.begin(url)) return;                 // TODO(field): begin(secureClient,url)
    http.addHeader("X-Api-Key", st_->apiKey());
    http.setTimeout(6000);
    int code = http.GET();
    if (code != 200) { http.end(); return; }
    String body = http.getString();
    http.end();

    String ver = extractStr_(body, "version");
    String bin = extractStr_(body, "url");
    if (ver.length() == 0 || bin.length() == 0) return;
    if (ver == FW_VERSION) return;                // already current

    Serial.printf("[OTA] update offered: %s (running %s)\n", ver.c_str(), FW_VERSION);
    doUpdate_(bin);
  }

private:
  void doUpdate_(const String& binUrl) {
    WiFiClient client;                            // TODO(field): WiFiClientSecure + setCACert
    httpUpdate.rebootOnUpdate(true);              // reboot into new image on success
    // Pass current version so a well-behaved server can 304 if unchanged.
    t_httpUpdate_return ret = httpUpdate.update(client, binUrl, FW_VERSION);
    switch (ret) {
      case HTTP_UPDATE_FAILED:
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

  static String extractStr_(const String& s, const char* key) {
    String pat = "\"" + String(key) + "\"";
    int i = s.indexOf(pat);
    if (i < 0) return "";
    i = s.indexOf(':', i); if (i < 0) return "";
    i = s.indexOf('"', i); if (i < 0) return "";
    int j = s.indexOf('"', i+1); if (j < 0) return "";
    return s.substring(i+1, j);
  }

  Store* st_ = nullptr;
  bool pendingVerify_ = false;
  bool confirmed_ = false;
};
