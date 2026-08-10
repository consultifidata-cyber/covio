// ============================================================================
// provision.h  —  serial provisioning console
// ----------------------------------------------------------------------------
// WHY THIS EXISTS: NVS defaults from config.h are seeded ONCE (first boot).
// After that, reflashing with new #defines changes NOTHING. This console is
// how you change server_url / api_key / WiFi on a running device with no
// reflash — plug in USB, open Serial Monitor @115200, type commands.
//
// Commands (newline-terminated):
//   help                      list commands
//   show                      print identity + current NVS config
//   set url <http(s)://...>   change server endpoint      (reboot to apply)
//   set key <apikey>          change API key
//   set wifi <ssid> <pass>    change WiFi (no spaces in ssid — limitation)
//   reboot                    restart now
//   factory                   wipe NVS (re-seeds from config.h defaults) + reboot
//   provision                 re-enter AP-mode setup on next boot (DM-Phase 2,
//                             NOT a factory reset -- existing wifi/server/key
//                             values and all queue/totalizer state are untouched)
//   reset_ack                 Commissioning reset: fast-forwards the queue's
//                             ack cursor to the current write position (see
//                             queue.h::resetAckToCurrentPosition). Touches
//                             ONLY the queue's AckRec -- NVS (server_url/
//                             api_key/wifi/calibration) and the Totalizer's
//                             own checkpoint (total/seq) are untouched. Never
//                             automatic -- explicit operator command only.
//   recover_queue             Queue storage recovery: deletes a single
//                             exhausted, never-rotated segment file and
//                             resets ONLY the write-position cursors (see
//                             queue.h::recoverQueueStorage). Does not touch
//                             acked_seq, NVS, or totalizer.total. Explicit
//                             operator command only.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "mbedtls/sha256.h"
#include "credential_display.h"
#include "store.h"
#include "wifi_provision.h"
#include "queue.h"
#include "totalizer.h"

class Provision {
public:
  // EventQueue*/Totalizer* default nullptr so any pre-existing single-arg
  // begin(&store) call site still compiles -- reset_ack simply reports
  // "unavailable" if not wired, rather than being silently absent.
  void begin(Store* st, EventQueue* q = nullptr, Totalizer* tot = nullptr) {
    st_ = st; q_ = q; tot_ = tot;
  }

  void service() {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        if (buf_.length()) handle_(buf_);
        buf_ = "";
      } else if (buf_.length() < 220) buf_ += c;
    }
  }

private:
  void handle_(String line) {
    line.trim();
    if (line == "help") {
      Serial.println("cmds: show | set url <u> | set key <k> | set wifi <ssid> <pass> | reboot | factory | provision | reset_ack | recover_queue");
    } else if (line == "show") {
      Serial.printf("device_id : %s\n", st_->deviceId().c_str());
      Serial.printf("fw        : %s\n", FW_VERSION);
      Serial.printf("boot_id   : %u\n", st_->bootId());
      Serial.printf("server_url: %s\n", st_->serverUrl().c_str());
      Serial.printf("api_key   : %s\n", apiKeyDisplay_().c_str());
      Serial.printf("wifi_ssid : %s\n", st_->wifiSsid().c_str());
      Serial.printf("calib     : v%u  K=%.4f  density=%.3f  Tref=%.1f\n",
                    st_->cfgVer(), st_->kFactor(), st_->density(), st_->tRef());
      // Balaji V1 freeze remediation (Product Readiness Review P1-1):
      // persistent diagnostic counters, readable without any network/local-
      // API access -- same values as /api/v1/metrics's restart_count/
      // watchdog_reset_count/brownout_reset_count/wifi_reconnect_count/
      // push_fail_count/crash_reset_streak fields (diagnostics.h).
      Serial.printf("diag      : restarts=%u watchdog=%u brownout=%u "
                    "wifi_reconnects=%u push_fails=%u crash_streak=%u\n",
                    st_->restartCount(), st_->watchdogResetCount(), st_->brownoutResetCount(),
                    st_->wifiReconnectCount(), st_->pushFailCount(), st_->crashResetStreak());
    } else if (line.startsWith("set url ")) {
      st_->setServerUrl(line.substring(8));
      Serial.println("[PROV] server_url saved. 'reboot' to apply cleanly.");
    } else if (line.startsWith("set key ")) {
      st_->setApiKey(line.substring(8));
      Serial.println("[PROV] api_key saved.");
    } else if (line.startsWith("set wifi ")) {
      String rest = line.substring(9);
      int sp = rest.indexOf(' ');
      if (sp > 0) {
        st_->setWifi(rest.substring(0, sp), rest.substring(sp + 1));
        Serial.println("[PROV] wifi saved. 'reboot' to apply.");
      } else Serial.println("[PROV] usage: set wifi <ssid> <pass>");
    } else if (line == "reboot") {
      Serial.println("[PROV] rebooting..."); delay(200); ESP.restart();
    } else if (line == "factory") {
      Serial.println("[PROV] factory reset: wiping NVS, rebooting...");
      st_->factoryReset(); delay(200); ESP.restart();
    } else if (line == "provision") {
      // DM-Phase 2: re-enter AP-mode setup on next boot, without a factory
      // reset -- wifi/server/key values and all SD-persisted queue/totalizer
      // state are completely untouched by this (they live on the SD card,
      // never reachable by anything a WiFi-mode change does).
      Serial.println("[PROV] will re-enter AP-mode setup on next boot. Rebooting...");
      WifiProvision::requestReprovision();
      delay(200);
      ESP.restart();
    } else if (line == "reset_ack") {
      // MW-001 commissioning reset -- see queue.h::resetAckToCurrentPosition.
      // Touches ONLY the queue's AckRec (acked_seq/cursor_segment/
      // cursor_offset). NVS and the Totalizer's own checkpoint (total/seq)
      // are untouched -- explicit operator command, never automatic.
      if (!q_ || !tot_) {
        Serial.println("[PROV] reset_ack unavailable (queue/totalizer not wired this build)");
      } else {
        uint32_t currentSeq = tot_->lastSeq();
        q_->resetAckToCurrentPosition(currentSeq);
        Serial.printf("[PROV] ack cursor reset to current position: seq=%u. 'reboot' to apply cleanly.\n",
                      currentSeq);
      }
    } else if (line == "recover_queue") {
      // MW-001 queue storage recovery -- see queue.h::recoverQueueStorage.
      // Deletes ONLY /queue/seg_000000.bin (proven via live `ls` to be the
      // sole, exhausted segment). Resets ONLY Totalizer.q_segment/q_offset
      // and the queue's cursor_segment/cursor_offset. Does NOT touch
      // acked_seq, NVS, or totalizer.total. Explicit operator command only.
      if (!q_) {
        Serial.println("[PROV] recover_queue unavailable (queue not wired this build)");
      } else {
        q_->recoverQueueStorage();
        Serial.println("[PROV] queue storage recovered: seg_000000.bin removed, write cursor reset to 0/0.");
      }
    } else {
      Serial.println("[PROV] unknown. type: help");
    }
  }

  // Plant-pilot activation remediation (credential-exposure closure):
  // the raw api_key was previously printed verbatim by "show" -- readable
  // by anyone with physical USB access, with no authentication at all
  // (the enterprise re-audit's own finding). Reports only a status
  // classification (same three-way logic as diagnostics.h's
  // apiKeyStatus_(), duplicated here as a tiny private helper rather than
  // pulling that header's much heavier include chain into this file just
  // for one string) plus a short, IRREVERSIBLE SHA-256-derived
  // fingerprint -- enough to confirm "did the key actually change"
  // between two reads, never enough to reconstruct the original value.
  // wifi_pass was never printed by this command in the first place (only
  // wifi_ssid, which is not treated as a secret anywhere else in this
  // codebase either) -- unchanged, not touched by this fix.
  String apiKeyDisplay_() {
    String key = st_->apiKey();
    CredentialDisplayStatus st = classifyCredential(key.c_str(), DEFAULT_API_KEY);
    String status = credentialDisplayStatusStr(st);

    uint8_t hash[32];
    mbedtls_sha256((const unsigned char*)key.c_str(), key.length(), hash, 0);
    char fp[13];
    snprintf(fp, sizeof(fp), "%02x%02x%02x%02x%02x%02x",
             hash[0], hash[1], hash[2], hash[3], hash[4], hash[5]);
    return status + " (fingerprint=" + String(fp) + ")";
  }

  Store* st_ = nullptr;
  EventQueue* q_ = nullptr;
  Totalizer* tot_ = nullptr;
  String buf_;
};
