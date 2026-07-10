// ============================================================================
// local_api.h  —  DM-Phase 1: read-only local HTTP API + mDNS advertisement
// ----------------------------------------------------------------------------
// WebServer.h (synchronous, built into arduino-esp32), not ESPAsyncWebServer
// -- matches this project's existing single-threaded, cooperative-scheduler
// design (§3.1's own stated reasoning). service() below is a two-line
// addition to loop(), consistent with how provision.service() and
// syncEngine.wifiService() already work.
//
// Every route here is read-only and unauthenticated by design (§3.1): it
// never returns a raw secret (api_key, wifi_pass) -- only masked status via
// diagnostics.h. Config *writes* (POST /api/v1/config) are AP-mode only
// (ADR-017/018, DM-Phase 2): this server registers that same path too, but
// its handler (handleConfigForbidden_, below) unconditionally rejects with
// 403 -- the actual write logic lives entirely in wifi_provision.h's own,
// separate WebServer instance, which is never active at the same time as
// this one (see covio_firmware.ino).
//
// This module owns the two genuinely-new periodic diagnostic samplers that
// don't belong to any single existing module (pulse_frequency_hz,
// rssi_history_dbm) -- diagnostics.h stays a pure builder over
// already-known values (its own header comment).
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include "config.h"
#include "store.h"
#include "totalizer.h"
#include "queue.h"
#include "sync.h"
#include "ota.h"
#include "diagnostics.h"

class LocalApi {
public:
  // Approved DM-Phase 1 global (MASTER_GOVERNANCE.md §2: "no new globals
  // beyond what an approved phase calls for" -- this phase is that approval).
  void begin(Store* st, Totalizer* tot, EventQueue* q, Sync* sync, Ota* ota) {
    st_ = st; tot_ = tot; q_ = q; sync_ = sync; ota_ = ota;

    server_.on("/api/v1/info",    HTTP_GET, [this]() { handleInfo_(); });
    server_.on("/api/v1/status",  HTTP_GET, [this]() { handleStatus_(); });
    server_.on("/api/v1/health",  HTTP_GET, [this]() { handleHealth_(); });
    server_.on("/api/v1/metrics", HTTP_GET, [this]() { handleMetrics_(); });
    server_.on("/api/v1/logs",    HTTP_GET, [this]() { handleLogsNotImplemented_(); });
    server_.on("/api/v1/config",  HTTP_POST, [this]() { handleConfigForbidden_(); });
#if FACTORY_TEST_BUILD
    // DM-Phase 6: see handleFactoryProvision_()'s own comment for why this
    // route is compile-time gated rather than a runtime mode check.
    server_.on("/api/v1/factory/provision", HTTP_POST, [this]() { handleFactoryProvision_(); });
#endif
    server_.on("/",               HTTP_GET, [this]() { handleHtml_(); });
    server_.onNotFound([this]() { handleNotFound_(); });

    // WebServer::begin() returns void on this core -- there is no failure
    // signal to branch on; binding a listening socket before the STA
    // interface has an IP is harmless (it simply isn't reachable yet).
    // mDNS (below, in service()) genuinely needs a connected interface, so
    // it is deferred; this does not need to be.
    server_.begin();
    Serial.println("[LOCALAPI] HTTP server started on :80");
  }

  // Call once per loop(). server_.handleClient() runs every call so a
  // pending request is always serviced promptly; mDNS-start and the
  // periodic diagnostic samplers are throttled to ~1/s internally so this
  // never adds meaningful cost to the main loop's telemetry cadence.
  void service() {
    server_.handleClient();

    uint32_t now = millis();
    if (now - lastSampleMs_ >= 1000) {
      lastSampleMs_ = now;
      if (!mdnsStarted_ && WiFi.status() == WL_CONNECTED) startMdns_();
      sampleRssi_();
      samplePulseFreq_();
    }
  }

private:
  // ---- route handlers ------------------------------------------------------
  void handleInfo_() {
    server_.send(200, "application/json", Diagnostics::buildInfoJson(*st_));
  }

  void handleStatus_() {
    server_.send(200, "application/json",
                 Diagnostics::buildStatusJson(*st_, *tot_, *q_, *sync_, *ota_, sdPresent_()));
  }

  void handleHealth_() {
    server_.send(200, "application/json",
                 Diagnostics::buildHealthJson(*q_, *sync_, *ota_, sdPresent_()));
  }

  void handleMetrics_() {
    int16_t snapshot[RSSI_HISTORY_LEN];
    int n = snapshotRssiHistory_(snapshot);
    server_.send(200, "application/json",
                 Diagnostics::buildMetricsJson(*q_, *sync_, sdPresent_(),
                                                havePulseFreqHz_, lastPulseFreqHz_,
                                                snapshot, n));
  }

  // GET / -- plain HTML mirror of /api/v1/status, for a factory tech with
  // only a browser and no app (§8 DM-Phase 1 step 5). Built directly from
  // the same getters buildStatusJson() uses -- not by parsing that JSON
  // body back apart -- so the same no-secrets guarantee applies identically.
  void handleHtml_() {
    bool sdOk = sdPresent_();
    String health = Diagnostics::healthStateOnly(*q_, *sync_, *ota_, sdOk);

    String h;
    h.reserve(1024);
    h  = "<html><head><title>Covio Device</title><meta charset=\"utf-8\">";
    h += "<style>body{font-family:system-ui;margin:2rem;max-width:640px}";
    h += "table{border-collapse:collapse;width:100%}";
    h += "td,th{border:1px solid #ccc;padding:.4rem .6rem;text-align:left}";
    h += "</style></head><body>";
    h += "<h2>Covio Device — " + htmlEscape_(st_->deviceId()) + "</h2>";
    h += "<p><b>Health:</b> " + htmlEscape_(health) + "</p>";
    h += "<table>";
    h += htmlRow_("Firmware", FW_VERSION);
    h += htmlRow_("Uptime (ms)", String(millis()));
    h += htmlRow_("WiFi", String(sync_->online() ? "connected to " : "disconnected from ")
                            + htmlEscape_(st_->wifiSsid())
                            + " (" + String((int)WiFi.RSSI()) + " dBm)");
    h += htmlRow_("Server URL", htmlEscape_(st_->serverUrl()));
    h += htmlRow_("API key", Diagnostics::apiKeyStatus_(*st_));
    h += htmlRow_("SD card", sdOk ? "ok" : "absent");
    h += htmlRow_("Queue backlog", String(q_->pendingCount()));
    h += htmlRow_("Total pulses", String((unsigned long long)tot_->total()));
    h += htmlRow_("Last sync",
                   sync_->haveSync() ? (String(millis() - sync_->lastSyncMs()) + " ms ago")
                                      : String("never"));
    h += htmlRow_("OTA state", Diagnostics::otaStateStr_(ota_->state()));
    h += "</table></body></html>";
    server_.send(200, "text/html", h);
  }

  // §13 A.3/A.5: honest 501 until ADR-012's log ring buffer exists -- never
  // fabricate log lines.
  void handleLogsNotImplemented_() {
    server_.send(501, "application/json",
                 "{\"error\":{\"code\":\"NOT_IMPLEMENTED\",\"message\":"
                 "\"Log ring buffer requires ADR-012\"}}");
  }

  void handleNotFound_() {
    server_.send(404, "application/json",
                 "{\"error\":{\"code\":\"NOT_FOUND\",\"message\":\"Unknown path\"}}");
  }

  // DM-Phase 2 (§13 A.2/A.5): /api/v1/config is AP-mode only. This server
  // only ever runs during normal station-mode operation -- wifi_provision.h
  // owns a SEPARATE WebServer instance for the actual AP-mode config-write
  // handling, and is never active at the same time as this one (see
  // covio_firmware.ino). Reaching this handler therefore always means the
  // device is NOT in AP mode, so it unconditionally rejects.
  void handleConfigForbidden_() {
    server_.send(403, "application/json",
                 "{\"error\":{\"code\":\"CONFIG_WRITE_FORBIDDEN_NOT_IN_AP_MODE\",\"message\":"
                 "\"Config writes are only accepted while the device is in provisioning mode\"}}");
  }

#if FACTORY_TEST_BUILD
  // DM-Phase 6 (§11.2/§11.7, ADR-008/ADR-018): the factory workflow's
  // local-API write path. ADR-018 states its own preference explicitly:
  // "the desktop app's factory workflow... sets it directly via the local
  // API rather than requiring a console command at all." This reuses THIS
  // server (not a second WebServer/port) rather than wifi_provision.h's
  // AP-mode one, because §11.7's factory sequence associates the device to
  // the factory-floor network in normal STATION mode -- /api/v1/config's
  // existing AP-mode-only gate (handleConfigForbidden_() above) cannot be
  // reused here; it would unconditionally reject every factory-floor
  // request exactly as it does today for any station-mode caller.
  //
  // Compile-time gating via FACTORY_TEST_BUILD (config.h) is the ENTIRE
  // security boundary, deliberately with no additional runtime mode check
  // layered on top: ADR-008 already requires "the factory-test image must
  // never be the image that ships to a customer" as its own build/release
  // control, so this route is compiled OUT of every image that could ever
  // reach a customer site -- a stronger guarantee than any runtime check
  // could add on top of a build that might still ship.
  void handleFactoryProvision_() {
    String body = server_.arg("plain");
    String newLogicalId = Ota::extractStr_(body, "logical_device_id");
    String newApiKey    = Ota::extractStr_(body, "api_key");

    if (newLogicalId.length() == 0 && newApiKey.length() == 0) {
      server_.send(400, "application/json",
                   "{\"error\":{\"code\":\"FACTORY_PROVISION_EMPTY\",\"message\":"
                   "\"at least one of logical_device_id or api_key is required\"}}");
      return;
    }

    // Audit fix (production-readiness audit, Identity lifecycle /
    // Provisioning workflow finding): logical_device_id and api_key are two
    // independent fields bundled into one request purely for the SOP's own
    // convenience ("Generate Logical Device ID + unique API key... write to
    // NVS" as one step) -- a write-once conflict on ONE must never silently
    // discard an otherwise-valid write to the OTHER, matching this
    // codebase's own established per-item-independent processing
    // convention (server.py's push() never lets one malformed record block
    // the rest of a batch). Both are now attempted regardless of the
    // other's outcome, and the response reports both outcomes.
    bool idConflict = false;
    if (newLogicalId.length() > 0) {
      String existing = st_->logicalDeviceId();
      // Write-once (§11.2/ADR-018: "manufacturing serial ... permanent per
      // board"). Re-submitting the SAME value is an idempotent success (a
      // retried request after a dropped response must not be treated as an
      // error); a DIFFERENT value once one is already assigned is rejected,
      // never silently overwritten.
      if (existing.length() > 0 && existing != newLogicalId) {
        idConflict = true;
      } else {
        st_->setLogicalDeviceId(newLogicalId);
      }
    }

    if (newApiKey.length() > 0) {
      // Freely overwritable -- matches the console's existing `set key`
      // command's own unrestricted semantics (ADR-008), unlike the
      // write-once Logical Device ID above. Applied independently of
      // idConflict above.
      st_->setApiKey(newApiKey);
    }

    String lid = st_->logicalDeviceId();
    if (idConflict) {
      // Standard error envelope (§13 A.5 convention, reused by every other
      // route in this file) -- api_key_status is included ALONGSIDE the
      // error, not instead of it, so a caller can tell the api_key write
      // (if one was submitted) still succeeded even though the overall
      // response is a 409.
      String resp = "{\"error\":{\"code\":\"LOGICAL_ID_ALREADY_ASSIGNED\",\"message\":"
                    "\"this device already has a different logical_device_id\"},"
                    "\"logical_device_id\":\"" + lid + "\","
                    "\"api_key_status\":\"" + Diagnostics::apiKeyStatus_(*st_) + "\"}";
      server_.send(409, "application/json", resp);
      return;
    }

    String resp = "{\"logical_device_id\":";
    resp += lid.length() ? ("\"" + lid + "\"") : "null";
    resp += ",\"api_key_status\":\"" + Diagnostics::apiKeyStatus_(*st_) + "\"}";
    server_.send(200, "application/json", resp);
  }
#endif

  // ---- HTML page helpers ----------------------------------------------------
  String htmlRow_(const String& label, const String& value) {
    return "<tr><td>" + label + "</td><td>" + value + "</td></tr>";
  }

  // wifi_ssid/server_url are NVS-configurable (console `set wifi`/`set url`)
  // and could otherwise contain characters that break this page's own
  // markup -- escaped for correctness, not just as a hardening measure.
  String htmlEscape_(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
      char c = in[i];
      switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        default:   out += c;        break;
      }
    }
    return out;
  }

  // ---- SD presence (§5: "reusing existing SD.begin()/SD.exists()") --------
  bool sdPresent_() {
    // Cheap, non-disruptive: SD.exists() does not remount. /queue is
    // guaranteed to exist once SD.begin() has ever succeeded this boot
    // (queue.h::begin() creates it) -- present/absent only, per §5's scope
    // note; the full degradation state machine is ADR-004's job.
    return SD.exists(SD_QUEUE_DIR);
  }

  // ---- mDNS (§13 A.6) -------------------------------------------------------
  String mdnsInstanceName_() {
    // covio-<last6hexofmac>. store.deviceId() is already "esp32-<hex mac>"
    // (store.h's chipId()) -- reusing its last 6 hex characters avoids
    // re-deriving the MAC or touching store.h at all.
    String hwid = st_->deviceId();
    return "covio-" + hwid.substring(hwid.length() - 6);
  }

  void startMdns_() {
    String hostname = mdnsInstanceName_();
    if (!MDNS.begin(hostname.c_str())) {
      Serial.println("[LOCALAPI] mDNS begin FAILED -- will retry (~1/s)");
      return;   // mdnsStarted_ stays false; retried by service()'s own throttle
    }
    MDNS.addService("covio", "tcp", 80);
    MDNS.addServiceTxt("covio", "tcp", "hardware_id", st_->deviceId());
    MDNS.addServiceTxt("covio", "tcp", "fw", FW_VERSION);
    MDNS.addServiceTxt("covio", "tcp", "model", DEVICE_MODEL);
    // DM-Phase 6: broadcasts the real value once assigned; "" (store.h's
    // unassigned sentinel) keeps the TXT record present-but-empty exactly as
    // it was pre-DM-Phase-6 for any device never factory-provisioned.
    MDNS.addServiceTxt("covio", "tcp", "logical_device_id", st_->logicalDeviceId());
    mdnsStarted_ = true;
    Serial.printf("[LOCALAPI] mDNS started: %s.local\n", hostname.c_str());
  }

  // ---- periodic diagnostic samplers (§3.1a /api/v1/metrics) ----------------
  static const int RSSI_HISTORY_LEN = 10;   // §13 A.3: "last 10 samples"

  void sampleRssi_() {
    if (WiFi.status() != WL_CONNECTED) return;   // nothing meaningful to sample
    rssiHistory_[rssiNext_] = (int16_t)WiFi.RSSI();
    rssiNext_ = (rssiNext_ + 1) % RSSI_HISTORY_LEN;
    if (rssiCount_ < RSSI_HISTORY_LEN) rssiCount_++;
  }

  // Returns an oldest-to-newest ordered snapshot into `out` (caller-sized
  // RSSI_HISTORY_LEN), correct regardless of ring wraparound state.
  int snapshotRssiHistory_(int16_t* out) {
    int n = rssiCount_;
    int start = (rssiNext_ - n + RSSI_HISTORY_LEN) % RSSI_HISTORY_LEN;
    for (int i = 0; i < n; i++) out[i] = rssiHistory_[(start + i) % RSSI_HISTORY_LEN];
    return n;
  }

  void samplePulseFreq_() {
    uint64_t total = tot_->total();
    uint32_t now = millis();
    if (havePulseSample_) {
      uint32_t elapsedMs = now - prevPulseSampleMs_;
      if (elapsedMs > 0) {
        // totalizer.total() is monotonic non-decreasing within a boot
        // (base_ + accumulated_ + live PCNT count, never reset) -- this
        // >= guard is defensive only, matching this codebase's existing
        // "never trust an impossible number" convention (see queue.h).
        uint64_t delta = (total >= prevPulseTotal_) ? (total - prevPulseTotal_) : 0;
        lastPulseFreqHz_ = (float)delta / ((float)elapsedMs / 1000.0f);
        havePulseFreqHz_ = true;
      }
    }
    prevPulseTotal_ = total;
    prevPulseSampleMs_ = now;
    havePulseSample_ = true;
  }

  WebServer server_{80};
  Store* st_ = nullptr;
  Totalizer* tot_ = nullptr;
  EventQueue* q_ = nullptr;
  Sync* sync_ = nullptr;
  Ota* ota_ = nullptr;

  bool mdnsStarted_ = false;
  uint32_t lastSampleMs_ = 0;

  int16_t rssiHistory_[RSSI_HISTORY_LEN] = {0};
  int rssiCount_ = 0;
  int rssiNext_ = 0;

  bool havePulseSample_ = false;
  uint64_t prevPulseTotal_ = 0;
  uint32_t prevPulseSampleMs_ = 0;
  bool havePulseFreqHz_ = false;
  float lastPulseFreqHz_ = 0.0f;
};
