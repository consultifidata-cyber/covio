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
#include "store.h"
#include "queue.h"
#include "telemetry.h"

class Sync {
public:
  void begin(Store* st, EventQueue* q) { st_ = st; q_ = q; }

  // ---- WiFi ----
  void wifiConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(st_->wifiSsid().c_str(), st_->wifiPass().c_str());
    Serial.printf("[NET] connecting to %s\n", st_->wifiSsid().c_str());
  }

  // Non-blocking WiFi keepalive with exponential backoff + jitter.
  void wifiService() {
    if (WiFi.status() == WL_CONNECTED) { backoff_ = WIFI_RETRY_MS; return; }
    uint32_t now = millis();
    if (now - lastWifiTry_ < backoff_) return;
    lastWifiTry_ = now;
    Serial.println("[NET] reconnecting...");
    WiFi.disconnect();
    WiFi.begin(st_->wifiSsid().c_str(), st_->wifiPass().c_str());
    backoff_ = min<uint32_t>(backoff_ * 2, 120000UL);      // cap 2 min
    backoff_ += (esp_random() % 1000);                     // jitter
  }

  bool online() { return WiFi.status() == WL_CONNECTED; }

  // ---- Push queued records; prune on cumulative ack_seq ----
  // Returns true if at least one record was acked this call.
  bool pushOnce() {
    if (!online()) return false;
    QRow batch[PUSH_BATCH_MAX];
    int n = q_->pending(batch, PUSH_BATCH_MAX);
    if (n == 0) return false;

    String body = Telemetry::toJson(*st_, batch, n);
    HTTPClient http;
    String url = st_->serverUrl() + PATH_PUSH;
    if (!http.begin(url)) { Serial.println("[SYNC] begin failed"); return false; }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Api-Key", st_->apiKey());
    http.setTimeout(8000);

    int code = http.POST(body);
    if (code != 200) {
      Serial.printf("[SYNC] push HTTP %d — keeping queue\n", code);
      http.end();
      return false;                       // NOT an ack; retry later
    }
    String resp = http.getString();
    http.end();

    // Parse ack_seq WITHOUT a JSON lib (tiny, dependency-free).
    long ackSeq = extractLong_(resp, "ack_seq");
    if (ackSeq < 0) {
      Serial.println("[SYNC] 200 but no ack_seq — keeping queue");
      return false;                       // bare 200 is not an ack (Invariant 6)
    }
    q_->ackThrough((uint32_t)ackSeq, st_->bootId());
    Serial.printf("[SYNC] acked_seq=%ld (sent %d)\n", ackSeq, n);
    return true;
  }

  // ---- Poll K-factor / calibration config ----
  void pollConfig() {
    if (!online()) return;
    HTTPClient http;
    String url = st_->serverUrl() + PATH_CONFIG;
    if (!http.begin(url)) return;
    http.addHeader("X-Api-Key", st_->apiKey());
    http.setTimeout(6000);
    int code = http.GET();
    if (code == 200) {
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
};
