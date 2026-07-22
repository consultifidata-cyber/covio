// ============================================================================
// wifi_provision.h  —  DM-Phase 2: SoftAP + captive-portal WiFi provisioning
// ----------------------------------------------------------------------------
// Implements ADR-017 (LAN-Local Diagnostics & WiFi-Based Provisioning),
// which supersedes ADR-008's commissioning-mechanism deferral. See
// Docs/Covio_Device_Manager_Live_Readiness_Plan.md §3.1/§3.1a/§5/§13.
//
// ARCHITECTURE (§3.1's diagram):
//   Boot -> station connect attempt (Sync, unchanged) -> bounded timeout
//        -> SoftAP "Covio-Setup-<last4>" (open, time-bounded) + captive
//           portal (this file) -> operator submits WiFi/URL/key -> tested
//           live, then written via Store's EXISTING setWifi/setServerUrl/
//           setApiKey (no new provisioning data model) -> reboot ->
//           station mode connects normally -> local_api.h resumes.
//
// AP mode is a FALLBACK STATE, never a permanent mode (§3.1): it activates
// only on a boot-time station-connect timeout, or an explicit `provision`
// console command, and stops the moment station WiFi is confirmed.
//
// WHY THIS FILE OWNS ITS OWN WebServer (not local_api.h's): a captive
// portal's WebServer and local_api.h's read-only diagnostics WebServer are
// never active at the same time in this design (see covio_firmware.ino) --
// local_api.h is only begin()'d once station WiFi is already confirmed, so
// there is never a port-80 conflict between the two, and this file never
// needs to depend on local_api.h at all.
//
// SECURITY: the SoftAP is open (no password) and unauthenticated by design
// -- physical/RF proximity to the device's own AP is the trust boundary,
// mirroring ADR-005's "physical possession is this architecture's full-trust
// boundary" (§3.1, §10 item 1, ratified in ADR-017). Config writes are only
// ever reachable through THIS server, which only ever runs while AP mode is
// active -- see local_api.h's own /api/v1/config route (added this phase)
// for what a station-mode request to that same path gets instead (403).
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config.h"
#include "store.h"
#include "ota.h"   // reuses Ota::extractStr_ -- see that file's own comment

// DM-Phase 2: one-shot "re-enter AP mode on next boot" signal for the new
// `provision` console command (provision.h). RTC_DATA_ATTR survives
// ESP.restart() (a software reset) but not a power-on reset, which is
// exactly the "without a factory reset" scope §5 specifies -- and adds no
// new NVS key (ADR-017's Compatibility section: "no new NVS keys").
RTC_DATA_ATTR bool g_wifiProvisionForceAp = false;

class WifiProvision {
public:
  // Set by the `provision` console command; consumed exactly once at boot.
  static void requestReprovision() { g_wifiProvisionForceAp = true; }
  static bool consumeReprovisionRequest() {
    bool v = g_wifiProvisionForceAp;
    g_wifiProvisionForceAp = false;
    return v;
  }

  // ---- SoftAP fallback logic ------------------------------------------------
  void begin(Store* st) {
    st_ = st;

    String ssid = apSsid_();
    // WIFI_AP_STA (not WIFI_AP): keeps whatever STA connection attempt Sync
    // already kicked off in setup() alive in the background, so a device
    // forced into AP mode via `provision` while its existing credentials
    // are still actually valid can still reconnect on its own (see
    // service()'s own check below) without operator action.
    WiFi.mode(WIFI_AP_STA);
    // Validation-pass fix: check and log failures here, matching this
    // codebase's own established "fail loud" convention (the SD-init
    // failure above, and local_api.h's own MDNS.begin() check) -- a silent
    // failure here would leave the device reporting "AP mode active" while
    // being completely unreachable, with no diagnostic trail.
    if (!WiFi.softAP(ssid.c_str())) {   // no password: open, time-bounded AP (§3.1/§10 item 1/ADR-017)
      Serial.println("[PROV-AP] WARNING: WiFi.softAP() reported failure -- AP may be unreachable");
    }

    if (!dnsServer_.start(53, "*", WiFi.softAPIP())) {   // captive portal: redirect all DNS -> our IP
      Serial.println("[PROV-AP] WARNING: DNS server failed to start -- captive-portal auto-redirect "
                      "will not work (manual browse to the AP's IP may still work)");
    }

    server_.on("/", HTTP_GET, [this]() { handleForm_(""); });
    server_.on("/", HTTP_POST, [this]() { handleSubmit_(); });
    server_.on("/api/v1/config", HTTP_POST, [this]() { handleConfigApi_(); });
    server_.onNotFound([this]() { handleForm_(""); });   // captive-portal catch-all
    server_.begin();

    active_ = true;
    Serial.printf("[PROV-AP] SoftAP started: %s (open)\n", ssid.c_str());
  }

  bool active() { return active_; }

  // Call once per loop() while active(). Returns true exactly once, the
  // moment station WiFi becomes confirmed while AP mode was active --
  // covio_firmware.ino uses this to hand WiFi authority back to Sync and
  // start local_api.h. Handles both the common path (the captive portal's
  // own successful config write reboots the device before this is ever
  // reached again) and the rarer edge case (already-valid credentials
  // reconnect on their own while AP mode was forced by the `provision`
  // command) uniformly, satisfying "AP stops broadcasting once station WiFi
  // is confirmed" (§8 acceptance criteria) either way.
  bool service() {
    if (!active_) return false;
    dnsServer_.processNextRequest();
    server_.handleClient();
    if (WiFi.status() == WL_CONNECTED) {
      stop_();
      return true;
    }
    return false;
  }

private:
  // ---- safe exit from provisioning mode -------------------------------------
  void stop_() {
    server_.stop();
    dnsServer_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    active_ = false;
    Serial.println("[PROV-AP] station confirmed -- AP stopped");
  }

  String apSsid_() {
    // "Covio-Setup-<last4>" per §3.1's diagram exactly.
    String hwid = st_->deviceId();
    return "Covio-Setup-" + hwid.substring(hwid.length() - 4);
  }

  // ---- captive portal (DNS + WebServer) --------------------------------------
  void handleForm_(const String& error) {
    String h;
    h.reserve(1280);
    h  = "<html><head><title>Covio Setup</title><meta charset=\"utf-8\">";
    h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    h += "<style>body{font-family:system-ui;margin:2rem;max-width:420px}";
    h += "label{display:block;margin-top:1rem;font-weight:600}";
    h += "input{width:100%;padding:.5rem;box-sizing:border-box;margin-top:.25rem}";
    h += "button{margin-top:1.5rem;padding:.6rem 1.2rem}";
    h += ".err{background:#fee;border:1px solid #c33;padding:.6rem;border-radius:4px;color:#900}";
    h += "</style></head><body>";
    h += "<h2>Covio Device Setup</h2>";
    if (error.length()) h += "<div class=\"err\">" + htmlEscape_(error) + "</div>";
    h += "<form method=\"POST\" action=\"/\">";
    h += "<label>WiFi network name</label>";
    h += "<input name=\"wifi_ssid\" required value=\"" + htmlEscape_(st_->wifiSsid()) + "\">";
    h += "<label>WiFi password</label>";
    h += "<input name=\"wifi_pass\" type=\"password\" required>";
    h += "<label>Server URL</label>";
    h += "<input name=\"server_url\" required value=\"" + htmlEscape_(st_->serverUrl()) + "\">";
    h += "<label>API key</label>";
    h += "<input name=\"api_key\" type=\"password\" required>";
    h += "<button type=\"submit\">Save &amp; Connect</button>";
    h += "</form>";
    h += "<p><small>WiFi password and API key are never pre-filled or displayed -- "
         "re-enter them each time, even if unchanged.</small></p>";
    h += "</body></html>";
    server_.send(200, "text/html", h);
  }

  String successPageHtml_() {
    return "<html><head><title>Covio Setup</title><meta charset=\"utf-8\"></head>"
           "<body style=\"font-family:system-ui;margin:2rem\">"
           "<h2>Saved</h2><p>Rebooting and connecting to your WiFi network now. "
           "This setup network will disappear shortly.</p></body></html>";
  }

  String htmlEscape_(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
      char c = in[i];
      switch (c) {
        case '&': out += "&amp;";  break;
        case '<': out += "&lt;";   break;
        case '>': out += "&gt;";   break;
        case '"': out += "&quot;"; break;
        default:  out += c;        break;
      }
    }
    return out;
  }

  // ---- configuration write flow (existing Store setters only) ---------------
  // Shared by both entry points below. Does NOT test WiFi or write to NVS --
  // callers decide those steps separately, since the JSON path's documented
  // contract (§13 A.3) does not call for a live test but the HTML portal's
  // UX requirement does (§8 test case 2).
  bool validateFields_(const String& ssid, const String& pass, const String& url,
                        const String& key, String& errCode, String& errMsg) {
    if (ssid.length() == 0 || pass.length() == 0 || url.length() == 0 || key.length() == 0) {
      errCode = "CONFIG_MISSING_FIELD";
      errMsg  = "wifi_ssid, wifi_pass, server_url, and api_key are all required together";
      return false;
    }
    if (!(url.startsWith("http://") || url.startsWith("https://"))) {
      errCode = "CONFIG_INVALID_URL";
      errMsg  = "server_url must start with http:// or https://";
      return false;
    }
    return true;
  }

  // Reuses Store's existing NVS setters verbatim -- no new provisioning data
  // model (ADR-017's Architecture Decision, ADR-008's own named Future
  // Extension).
  void writeConfig_(const String& ssid, const String& pass, const String& url, const String& key) {
    st_->setWifi(ssid, pass);
    st_->setServerUrl(url);
    st_->setApiKey(key);
  }

  // HTML-portal-only: live-tests the submitted credentials before writing
  // anything, so a wrong password produces a visible error in the portal
  // itself rather than a write-then-reboot-then-silently-fall-back-to-AP-
  // again loop (§8 test case 2). Safe to call WiFi.begin() here: Sync's own
  // reconnect logic is paused for the whole time AP mode is active
  // (ADR-017 WiFi-authority consolidation), so there is no race.
  bool testWifiCredentials_(const String& ssid, const String& pass) {
    // Validation-pass fix: matches sync.h's own established reconnect
    // pattern (disconnect before begin()) -- this path is exactly the one
    // most likely to be called repeatedly (an operator retrying after a
    // typo), and skipping the disconnect was an inconsistency with it.
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < AP_TEST_CONNECT_TIMEOUT_MS) {
      dnsServer_.processNextRequest();   // keep the portal at least somewhat responsive
      delay(50);
    }
    return WiFi.status() == WL_CONNECTED;
  }

  // POST / -- the human-facing captive-portal form.
  void handleSubmit_() {
    String ssid = server_.arg("wifi_ssid");
    String pass = server_.arg("wifi_pass");
    String url  = server_.arg("server_url");
    String key  = server_.arg("api_key");

    String errCode, errMsg;
    if (!validateFields_(ssid, pass, url, key, errCode, errMsg)) {
      handleForm_(errMsg);
      return;
    }
    if (!testWifiCredentials_(ssid, pass)) {
      handleForm_("Could not connect to \"" + ssid +
                  "\" -- check the WiFi network name and password and try again.");
      return;
    }
    writeConfig_(ssid, pass, url, key);
    server_.send(200, "text/html", successPageHtml_());
    delay(500);   // let the HTTP response flush before the reboot cuts the connection
    ESP.restart();
  }

  // POST /api/v1/config -- the frozen JSON contract (§13 A.3). Field/URL
  // validation only, exactly matching §13's documented error codes; no live
  // WiFi test (not part of this endpoint's documented behavior -- a future
  // desktop-app client (DM-Phase 3) is expected to poll device status after
  // a config push rather than have the device block the HTTP response on a
  // live test).
  void handleConfigApi_() {
    String body = server_.arg("plain");
    String ssid = Ota::extractStr_(body, "wifi_ssid");
    String pass = Ota::extractStr_(body, "wifi_pass");
    String url  = Ota::extractStr_(body, "server_url");
    String key  = Ota::extractStr_(body, "api_key");

    String errCode, errMsg;
    if (!validateFields_(ssid, pass, url, key, errCode, errMsg)) {
      server_.send(400, "application/json",
        "{\"error\":{\"code\":\"" + errCode + "\",\"message\":\"" + errMsg + "\"}}");
      return;
    }
    writeConfig_(ssid, pass, url, key);
    server_.send(200, "application/json", "{\"success\":true}");
    delay(500);
    ESP.restart();
  }

  Store* st_ = nullptr;
  WebServer server_{80};
  DNSServer dnsServer_;
  bool active_ = false;
};
