// ============================================================================
// sync.h  —  the reusable Covio device sync engine
// ----------------------------------------------------------------------------
// Responsibilities (device-side half of the shared contract):
//   - keep WiFi up (reconnect with backoff+jitter)
//   - POST batches of queued records to server_url + PATH_PUSH
//   - parse {ack_seq} from the JSON response and prune the queue to it
//   - poll server_url + PATH_CONFIG for the current K-factor/version
//
// UPSTREAM-AGNOSTIC: every request goes to whatever store.serverUrl() returns.
// This class has no idea whether that's an LCS or the cloud. Swapping targets
// is a provisioning change (edit NVS), never a code change.
//
// ACK RULE: the queue is pruned ONLY when the response body parses to a valid
// ack_seq. A bare HTTP 200 with no/garbled body prunes nothing — records stay
// queued and get retried. This is what guarantees no silent data loss.
//
// ADR-001 note: pushOnce() forwards whatever Telemetry::toJson() produces
// as-is. It has no schema_version/record_type awareness of its own — the
// per-record envelope fields are stamped by Telemetry::build()/toJson() and
// interpreted by the receiver. Acceptance/rejection of a given schema_version
// is entirely a receiver-side decision (server/server.py); the device never
// negotiates or inspects its own schema version at push time.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "store.h"
#include "queue.h"
#include "telemetry.h"
#include "certs.h"
#include "timestamp_parse.h"   // overflow-safe int64 parsing (server_time_ms remediation)

class Sync {
public:
  void begin(Store* st, EventQueue* q) { st_ = st; q_ = q; }

  // ---- WiFi ----
  void wifiConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(st_->wifiSsid().c_str(), st_->wifiPass().c_str());
    Serial.printf("[NET] connecting to %s\n", st_->wifiSsid().c_str());
    // DM-Phase 5 (ADR-005): "state the limitation to operators, don't hide
    // it" -- a device still configured with a plain http:// server_url is
    // running without transport encryption or CA pinning. Fine for bench
    // iteration (this is exactly what DEFAULT_SERVER_URL is for); must be
    // https:// before any field deployment. Logged once per boot here,
    // not on every reconnect attempt.
    if (!covioIsHttpsUrl(st_->serverUrl())) {
      Serial.println("[SECURITY] server_url is http:// (unencrypted, unpinned) -- "
                      "bench/dev only. Must be https:// before field deployment (ADR-005).");
    }
  }

  // DM-Phase 2 (ADR-017 WiFi-authority consolidation): while true, this
  // module's own reconnect logic below is a no-op -- set by
  // covio_firmware.ino exactly while wifi_provision.h owns WiFi connection
  // decisions (AP-fallback active), so the two never independently call
  // WiFi.begin() at the same time with different credentials.
  void setWifiAuthorityPaused(bool paused) { wifiAuthorityPaused_ = paused; }

  // Non-blocking WiFi keepalive with exponential backoff + jitter.
  void wifiService() {
    if (wifiAuthorityPaused_) return;
    if (WiFi.status() == WL_CONNECTED) { backoff_ = WIFI_RETRY_MS; return; }
    uint32_t now = millis();
    if (now - lastWifiTry_ < backoff_) return;
    lastWifiTry_ = now;
    Serial.println("[NET] reconnecting...");
    // Balaji V1 freeze remediation (Product Readiness Review P1-1):
    // persistent counter -- this branch only runs when WiFi was NOT already
    // connected and backoff has elapsed, i.e. exactly a genuine reconnect
    // attempt, never the initial wifiConnect() call at boot (a separate
    // method, uncounted).
    st_->incrementWifiReconnectCount();
    WiFi.disconnect();
    WiFi.begin(st_->wifiSsid().c_str(), st_->wifiPass().c_str());
    backoff_ = min<uint32_t>(backoff_ * 2, 120000UL);      // cap 2 min
    backoff_ += (esp_random() % 1000);                     // jitter
  }

  bool online() { return WiFi.status() == WL_CONNECTED; }

  // ---- DM-Phase 1 (local diagnostics, §13 A.3 /api/v1/status) ----
  // Getters only -- these retain values pushOnce()/pollConfig() already
  // compute but previously only logged. No change to push/ack/poll decision
  // logic. Named "Sync", not "Ack": §13 A.3 defines last_sync_ms_ago as
  // null only if "no successful push/config-poll yet this boot" -- i.e. it
  // tracks the more recent of EITHER a confirmed push ack OR a 200 from
  // pollConfig(), not push-acks alone (validation-pass correction).
  bool     haveSync()         { return haveSync_; }
  uint32_t lastSyncMs()       { return lastSyncMs_; }
  bool     havePush()         { return lastPushHttpCode_ != -1; }
  int      lastPushHttpCode() { return lastPushHttpCode_; }
  uint32_t lastPushRttMs()    { return lastPushRttMs_; }   // valid whenever havePush() is true

  // ---- Push queued records; prune on cumulative ack_seq ----
  // Returns true if at least one record was acked this call.
  bool pushOnce() {
    if (!online()) return false;
    QRow batch[PUSH_BATCH_MAX];
    int n = q_->pending(batch, PUSH_BATCH_MAX);
    if (n == 0) return false;

    String body = Telemetry::toJson(*st_, batch, n);
    String url = st_->serverUrl() + PATH_PUSH;

    // DM-Phase 5 (ADR-005): https:// uses a pinned-CA WiFiClientSecure --
    // never setInsecure() (permanently forbidden by ADR-005). http:// keeps
    // using the plain WiFiClient path exactly as before, for the bench stub.
    HTTPClient http;
    WiFiClientSecure secureClient;
    bool began;
    if (covioIsHttpsUrl(url)) {
      secureClient.setCACert(COVIO_PINNED_CA_CERT);
      began = http.begin(secureClient, url);
    } else {
      began = http.begin(url);
    }
    if (!began) { Serial.println("[SYNC] begin failed"); return false; }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Api-Key", st_->apiKey());
    http.setTimeout(8000);

    uint32_t rttStart = millis();           // DM-Phase 1: §13 A.3 network_rtt_ms
    int code = http.POST(body);
    lastPushRttMs_ = millis() - rttStart;   // measured regardless of the code returned
    lastPushHttpCode_ = code;              // DM-Phase 1: getter only, no behavior change
    if (code != 200) {
      Serial.printf("[SYNC] push HTTP %d — keeping queue\n", code);
      http.end();
      // Balaji V1 freeze remediation (Product Readiness Review P1-1):
      // persistent counter for any push attempt that did not result in an
      // ack -- covers connection failures (which is what a DNS failure or
      // TLS handshake failure surfaces as through this HTTP client
      // abstraction; the two are not separately distinguishable at this
      // layer, so this is deliberately one honest combined counter rather
      // than a fabricated DNS-vs-TLS split) as well as server-side error
      // codes (4xx/5xx).
      st_->incrementPushFailCount();
      return false;                       // NOT an ack; retry later
    }
    String resp = http.getString();
    http.end();

    // Parse ack_seq WITHOUT a JSON lib (tiny, dependency-free).
    long ackSeq = extractLong_(resp, "ack_seq");
    if (ackSeq < 0) {
      Serial.println("[SYNC] 200 but no ack_seq — keeping queue");
      st_->incrementPushFailCount();      // same counter -- this is still not an ack
      return false;                       // bare 200 is not an ack (Invariant 6)
    }
    q_->ackThrough((uint32_t)ackSeq, st_->bootId());
    lastSyncMs_ = millis();                // DM-Phase 1: getter only, no behavior change
    haveSync_   = true;

    // RISK-16 remediation (OTA manifest authenticity): this device has no
    // RTC/NTP (RISK-11, unchanged, pre-existing gap) -- the only wall-clock
    // signal it ever receives at all is server_time_ms, already returned by
    // every successful push (server.py's push() response). Captured here,
    // purely as a best-effort estimate for manifest expiry checks (ota.h) --
    // NOT used for anything telemetry-timestamp-related (QRow.ts remains
    // device-uptime-seconds, unchanged, exactly as SCHEMA_REGISTRY.md
    // documents). If this device has never successfully pushed yet,
    // haveServerTime() is false and ota.h treats manifest expiry as
    // unverifiable (fails closed -- see ota.h's own comment).
    // Remediation (34_OTA_SECURITY_HARDWARE_SUITE_RESULT.md): server_time_ms
    // is a 13-digit Unix MILLISECOND timestamp -- extractLong_()'s 32-bit
    // `long` accumulator overflows on any real-world value, permanently
    // wrapping negative and failing closed on the OTA time-source gate.
    // extractInt64_() below is overflow-CHECKED (see timestamp_parse.h),
    // never silently wraps -- a value that would overflow int64_t is
    // treated exactly like "field absent/malformed" (fails closed, same
    // as before this fix), never accepted with a corrupted value.
    int64_t serverTimeMs = 0;
    if (extractInt64_(resp, "server_time_ms", &serverTimeMs) && serverTimeMs >= 0) {
      // Stored as epoch SECONDS (unchanged field type/width -- a 32-bit
      // signed `long` holds any epoch-seconds value until year 2038,
      // comfortably outside this remediation's scope: only the
      // MILLISECOND parsing step above overflowed, not this division's
      // result).
      serverUnixS_ = (long)(serverTimeMs / 1000);
      serverTimeCapturedAtMs_ = millis();
      haveServerTime_ = true;
    }

    Serial.printf("[SYNC] acked_seq=%ld (sent %d)\n", ackSeq, n);
    return true;
  }

  // RISK-16 remediation: getters only, no behavior change to push/ack logic.
  bool haveServerTime() { return haveServerTime_; }
  // Best-effort estimate of the current unix time (seconds), extrapolated
  // from the last server_time_ms this device actually received plus
  // elapsed device uptime since then. Accuracy degrades with time since
  // the last successful push (no drift correction beyond that -- this
  // device has no independent clock source at all to cross-check against).
  long estimatedUnixNow() {
    if (!haveServerTime_) return -1;
    return serverUnixS_ + (long)((millis() - serverTimeCapturedAtMs_) / 1000);
  }

  // ---- Poll K-factor / calibration config ----
  void pollConfig() {
    if (!online()) return;
    String url = st_->serverUrl() + PATH_CONFIG;

    // DM-Phase 5 (ADR-005): same scheme dispatch as pushOnce() above.
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
    if (code == 200) {
      // DM-Phase 1: a 200 here is itself a successful sync event (§13 A.3's
      // "no successful push/config-poll" null condition) regardless of
      // whether the K-factor version actually changed below.
      lastSyncMs_ = millis();
      haveSync_   = true;
      String r = http.getString();
      long ver = extractLong_(r, "version");
      float k  = extractFloat_(r, "K_factor");
      float d  = extractFloat_(r, "density");
      float tr = extractFloat_(r, "T_ref");
      if (ver >= 0 && (uint32_t)ver != st_->cfgVer()) {
        if (k > 0) st_->setCalib(k, d, tr);
        st_->setCfgVer((uint32_t)ver);
        Serial.printf("[SYNC] new calibration v%ld  K=%.4f\n", ver, k);
      }
    }
    http.end();
  }

private:
  // Minimal JSON scalar extractors. Good enough for the flat, trusted
  // server contract; not a general parser.
  static long extractLong_(const String& s, const char* key) {
    String pat = "\"" + String(key) + "\"";
    int i = s.indexOf(pat);
    if (i < 0) return -1;
    i = s.indexOf(':', i);
    if (i < 0) return -1;
    i++;
    while (i < (int)s.length() && (s[i] == ' ' || s[i] == '"')) i++;
    long v = 0; bool any = false;
    while (i < (int)s.length() && (isdigit(s[i]))) { v = v*10 + (s[i]-'0'); i++; any = true; }
    return any ? v : -1;
  }
  // Remediation for the OTA time-source overflow defect: same field-
  // location logic as extractLong_() above (find "key", skip to the
  // value, same first-match-wins policy for a duplicate key -- this
  // parser has never attempted duplicate-key disambiguation, and this
  // fix does not add that), but hands the located digit range to
  // parseNonNegativeInt64Checked() (timestamp_parse.h) for the actual
  // overflow-checked accumulation, instead of extractLong_()'s unchecked
  // 32-bit `long` loop. Returns false (leaves *out untouched) for a
  // missing field, malformed/non-digit content, an empty value, or a
  // value that would overflow int64_t -- every one of those is a fail-
  // closed rejection at the call site, identical in effect to
  // extractLong_()'s existing "-1 = absent/malformed" contract.
  static bool extractInt64_(const String& s, const char* key, int64_t* out) {
    String pat = "\"" + String(key) + "\"";
    int i = s.indexOf(pat);
    if (i < 0) return false;
    i = s.indexOf(':', i);
    if (i < 0) return false;
    i++;
    while (i < (int)s.length() && (s[i] == ' ' || s[i] == '"')) i++;
    int start = i;
    while (i < (int)s.length() && isdigit(s[i])) i++;
    return parseNonNegativeInt64Checked(s.c_str(), start, i, out);
  }
  static float extractFloat_(const String& s, const char* key) {
    String pat = "\"" + String(key) + "\"";
    int i = s.indexOf(pat);
    if (i < 0) return -1;
    i = s.indexOf(':', i);
    if (i < 0) return -1;
    return s.substring(i+1).toFloat();
  }

  Store* st_ = nullptr;
  EventQueue* q_ = nullptr;
  uint32_t lastWifiTry_ = 0;
  uint32_t backoff_ = WIFI_RETRY_MS;
  bool     wifiAuthorityPaused_ = false;   // DM-Phase 2: see setWifiAuthorityPaused()

  // ---- DM-Phase 1 (local diagnostics) ----
  bool     haveSync_ = false;         // millis() 0 is a valid timestamp, so a
                                       // separate flag distinguishes "never
                                       // synced" from "synced at boot".
  uint32_t lastSyncMs_ = 0;           // last successful push-ack OR config-poll 200
  int      lastPushHttpCode_ = -1;    // -1 = no push attempted yet this boot
  uint32_t lastPushRttMs_ = 0;        // DM-Phase 1: valid iff lastPushHttpCode_ != -1

  // RISK-16 remediation (OTA manifest authenticity -- expiry checks)
  bool     haveServerTime_ = false;
  long     serverUnixS_ = 0;
  uint32_t serverTimeCapturedAtMs_ = 0;
};
