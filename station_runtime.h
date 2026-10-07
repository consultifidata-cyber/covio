// station_runtime.h -- the Machine Station on the board (STATION_ENABLE=1).
//
// Part 1 §K, MC-014. The station runs in its OWN FreeRTOS task, because one
// HTTPS transaction may hold the main loop for up to 80 s and a station in the
// loop would be deaf for that long. Counting is not touched: pulses stay in the
// PCNT peripheral and totalizer.h; this task only READS the PCNT counter
// register (safe from any task) to learn whether the machine moved this
// second. It never calls totalizer.total(), whose drain step is not meant to
// be read mid-way from a second task.
//
// THE LOOP STAYS THE ONLY WRITER of the durable queue and of `seq`. The task
// hands its events over through an in-memory FreeRTOS queue; the loop drains
// it (drainInto), builds record_type 2 rows (station_row.h), appends them with
// the next global seq and checkpoints, exactly as it does telemetry.
//
// Config (MC-015, MC-051): fetched by the loop on its network slot from the
// existing config path with ?station=1&have=N, checked against the server's
// sha256 header and applied whole or not at all (station_logic.h); the last
// applied body is kept on flash and restored at boot. A board that has never
// had a valid config is silent and dark.
//
// ⚠ Compiles and is host-tested in parts; NOT bench verified. Light, sounder,
// beeper, reader and the expander's behaviour are BENCH VERIFICATION REQUIRED.
#pragma once

#if STATION_ENABLE

#if SENSOR_MODE == SENSOR_MODE_CT
#error "STATION_ENABLE needs the PCNT pulse path; a CT build synthesizes pulses in software"
#endif

#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <driver/pcnt.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include "card_reader.h"
#include "station_io.h"
#include "station_logic.h"
#include "station_row.h"
#include "station_store.h"

#ifndef PIN_STATION_BEEPER
#define PIN_STATION_BEEPER 46
#endif
#ifndef STATION_CONFIG_POLL_MS
#define STATION_CONFIG_POLL_MS 60000UL
#endif
#define STATION_CONFIG_MAX_BYTES (32 * 1024)
#define STATION_CONFIG_FILE "/station.cfg"
#define STATION_CONFIG_TMP "/station.cfg.tmp"
// Holds every event an 80 s stall of the loop can produce with a card tapped
// every second (two events a tap at most): about 16 KB of RAM.
#define STATION_EVENT_QUEUE_LEN 160
#ifndef STATION_TASK_PERIOD_MS
#define STATION_TASK_PERIOD_MS 20  // reader poll and output refresh
#endif

namespace station {

// One event and the uptime second it happened in. The second travels WITH the
// event: the loop may drain eighty seconds' worth at once after a long HTTPS
// call, and each row must carry its own time.
struct QueuedEvent {
  Event e;
  uint32_t ts;
};

class StationRuntime {
 public:
  // `wallNowS` returns estimated unix seconds or -1 (SyncEngine::estimatedUnixNow).
  typedef long (*WallClock)();

  bool begin(WallClock wall, CardReader* reader) {
    wall_ = wall;
    reader_ = reader;
    live_ = allocConfig();
    scratch_ = allocConfig();
    if (!live_ || !scratch_) {
      Serial.println("[STATION] no memory for the config; station disabled");
      return false;
    }
    logic_ = new StationLogic(live_);
    lock_ = xSemaphoreCreateMutex();
    events_ = xQueueCreate(STATION_EVENT_QUEUE_LEN, sizeof(QueuedEvent));
    prefs_.begin("station", false);
    bool ack = prefs_.getBool("ack", false);
    logic_->boot(ack);
    restoreConfig();
    pinMode(PIN_STATION_BEEPER, OUTPUT);
    digitalWrite(PIN_STATION_BEEPER, LOW);
    governor_.begin(millis());
    outputsOk_ = outputs_.begin();  // all off, then outputs
    if (!outputsOk_) Serial.println("[STATION] output expander not answering (station fault)");
    int16_t c = 0;
    pcnt_get_counter_value(PCNT_UNIT_USED, &c);
    lastCount_ = (uint16_t)c;
    xTaskCreatePinnedToCore(taskEntry, "station", 6144, this, 2, &task_, 1);
    Serial.printf("[STATION] started: reader=%s config=%s\n", reader_->kind(),
                  logic_->hasConfig() ? "restored" : "none (silent until one arrives)");
    return true;
  }

  // ---- called by the loop -----------------------------------------------------

  // The latest lifetime total, for alert events only (informational).
  void setTotal(uint64_t total) { __atomic_store_n(&total_, total, __ATOMIC_RELAXED); }

  // The existing monitors' verdict (MIKI_WIRE_PROFILE: implausible rate or a
  // suspect idle), from the loop's telemetry tick. Suspect: no sound, fault light.
  void setInputSuspect(bool suspect) { __atomic_store_n(&suspect_, suspect, __ATOMIC_RELAXED); }

  // The durable queue's unacknowledged rows (measurement and station alike),
  // for Station Health: a backlog that grows is the network, not the station.
  void setBacklog(uint32_t rows) { __atomic_store_n(&backlog_, rows, __ATOMIC_RELAXED); }

  // Move events into the durable queue. `append` builds and stores one row
  // with the next seq and returns false when the queue refused it; it is the
  // loop's own code path (covio_firmware.ino).
  template <typename AppendFn>
  int drainInto(AppendFn append) {
    int n = 0;
    QueuedEvent q;
    while (xQueueReceive(events_, &q, 0) == pdTRUE) {
      StationPayload p;
      if (!encodeEvent(q.e, q.ts, &p)) {
        dropped_++;
        continue;
      }
      if (!append(p, q.ts)) lost_++;  // the durable queue refused it (flash full)
      n++;
    }
    bool ack = logic_->acknowledged();
    if (ack != persistedAck_) {  // flash writes stay in the loop
      prefs_.putBool("ack", ack);
      persistedAck_ = ack;
    }
    return n;
  }

  // Loop pass: the independent failsafe. A task that has stopped refreshing
  // the outputs gets them switched off here.
  void failsafe() {
    OutputState s = governor_.failsafe(millis());
    if (s.bits() == 0 && lastBits_ != 0) {
      if (xSemaphoreTake(lock_, 0) == pdTRUE) {
        outputs_.allOff();
        lastBits_ = 0;
        xSemaphoreGive(lock_);
      }
    }
  }

  // The loop's network slot: fetch and apply a config. Returns true when it
  // used the network.
  bool pollConfig(const String& serverUrl, const String& apiKey, const char* path) {
    uint32_t now = millis();
    if (lastPollMs_ != 0 && now - lastPollMs_ < STATION_CONFIG_POLL_MS) return false;
    lastPollMs_ = now;
    String url = serverUrl + path + "?station=1";
    if (logic_->hasConfig()) url += "&have=" + String(logic_->config().version);
    HTTPClient http;
    WiFiClientSecure secure;
    bool began;
    if (url.startsWith("https://")) {
      secure.setCACert(COVIO_PINNED_CA_CERT);
      secure.setHandshakeTimeout(HTTPS_HANDSHAKE_TIMEOUT_S);
      began = http.begin(secure, url);
    } else {
      began = http.begin(url);
    }
    if (!began) return true;
    const char* keep[] = {"x-station-config-version", "x-station-config-sha256"};
    http.collectHeaders(keep, 2);
    http.addHeader("X-Api-Key", apiKey);
    http.setConnectTimeout(HTTPS_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTPS_IO_TIMEOUT_MS);
    int code = http.GET();
    if (code == 200) {
      int declared = http.getSize();  // -1 when the reply carries no length
      uint32_t version = claimed(http);
      if (declared > STATION_CONFIG_MAX_BYTES) {
        reject(version, "toolarge");
      } else {
        // Never more than the cap into RAM, whatever the server says: a reply
        // with no length (chunked) could otherwise fill the heap. The buffer
        // lives only for this download (no PSRAM in this build).
        char* buf = static_cast<char*>(malloc(STATION_CONFIG_MAX_BYTES + 1));
        if (!buf) {
          reject(version, "memory");
        } else {
          BoundedSink sink(buf, STATION_CONFIG_MAX_BYTES);
          http.writeToStream(&sink);
          size_t got = sink.length();
          if (sink.overflowed() || !configLengthOk(declared, got, STATION_CONFIG_MAX_BYTES)) {
            reject(version, "toolarge");
          } else {
            buf[got] = 0;
            if (strncmp(buf, "UNCHANGED", 9) != 0)
              applyDownloaded(buf, got, http.header("x-station-config-sha256"), version);
          }
          free(buf);
        }
      }
    }
    http.end();
    return true;
  }

  // ---- console (provision.h) ------------------------------------------------

  void printStatus(Print& out) {
    out.printf("station: config=%s v%lu mode=%d cards=%d windows=%d\n",
               logic_->hasConfig() ? "applied" : "none",
               logic_->hasConfig() ? (unsigned long)logic_->config().version : 0UL,
               logic_->hasConfig() ? logic_->config().mode : 0,
               logic_->hasConfig() ? logic_->config().cardCount : 0,
               logic_->hasConfig() ? logic_->config().windowCount : 0);
    out.printf("station: armed=%d acknowledged=%d light=%u sounding=%d reader=%s(%s) outputs=%s\n",
               logic_->armed(), logic_->acknowledged(), (unsigned)lastLight_, lastSounding_,
               reader_->kind(), reader_->up() ? "up" : "down", outputsOk_ ? "ok" : "FAULT");
    out.printf("station: events queued=%u dropped=%lu lost_to_full_queue=%lu governor_cut=%d "
               "sound_ms_window=%lu\n",
               (unsigned)uxQueueMessagesWaiting(events_), (unsigned long)dropped_,
               (unsigned long)lost_, governor_.faultLatched(),
               (unsigned long)governor_.windowSoundMs());
    // Bench measurements (S6 §8, §20): measured here, never estimated.
    out.printf("station: bench parse_us=%lu persist_ms=%lu tick_us_max=%lu tap_ms_last=%lu "
               "tap_ms_max=%lu config_bytes=%u heap_free=%u psram_free=%u\n",
               (unsigned long)parseUs_, (unsigned long)persistMs_, (unsigned long)tickUsMax_,
               (unsigned long)tapMsLast_, (unsigned long)tapMsMax_, (unsigned)sizeof(Config),
               (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  }

  // `station test`: a bounded self-check of the outputs -- each light 1 s,
  // the sounder 1 s, the beeper once. BENCH: watch every channel.
  void requestSelfTest() { selfTest_ = true; }

  // For the envelope:
  // {"cfg":N,"reader":0|1,"state":"...","dropped":N,"backlog":N,"suspect":0|1}.
  String envelopeJson() {
    String s = "{\"cfg\":";
    s += logic_->hasConfig() ? String(logic_->config().version) : String(0);
    s += ",\"reader\":";
    s += reader_->up() ? "1" : "0";
    s += ",\"state\":\"";
    s += !outputsOk_ ? "output_fault" : !logic_->hasConfig() ? "no_config" : "ok";
    // Health (S6 §12): events lost before they reached the durable queue since
    // boot, the queue's unacknowledged rows, and the input monitors' verdict.
    s += "\",\"dropped\":";
    s += String((unsigned long)(dropped_ + lost_));
    s += ",\"backlog\":";
    s += String((unsigned long)__atomic_load_n(&backlog_, __ATOMIC_RELAXED));
    s += ",\"suspect\":";
    s += __atomic_load_n(&suspect_, __ATOMIC_RELAXED) ? "1" : "0";
    s += "}";
    return s;
  }

 private:
  static void* allocBytes(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(n);
    return p;
  }
  static Config* allocConfig() { return static_cast<Config*>(allocBytes(sizeof(Config))); }

  // A Stream that keeps at most `cap` bytes and remembers that more came.
  class BoundedSink : public Stream {
   public:
    BoundedSink(char* buf, size_t cap) : buf_(buf), cap_(cap) {}
    size_t write(uint8_t c) override {
      if (n_ < cap_) buf_[n_++] = (char)c;
      else over_ = true;
      return 1;
    }
    size_t write(const uint8_t* b, size_t n) override {
      for (size_t i = 0; i < n; i++) write(b[i]);
      return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t length() const { return n_; }
    bool overflowed() const { return over_; }

   private:
    char* buf_;
    size_t cap_;
    size_t n_ = 0;
    bool over_ = false;
  };

  static void sha256HexOf(const uint8_t* data, size_t len, char out[65]) {
    uint8_t digest[32];
    mbedtls_sha256(data, len, digest, 0);
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", digest[i]);
  }

  static uint32_t claimed(HTTPClient& http) {
    String v = http.header("x-station-config-version");
    return (uint32_t)v.toInt();
  }

  void reject(uint32_t version, const char* why) {
    Event e;
    memset(&e, 0, sizeof(e));
    e.event = EV_CONFIG_REJECTED;
    e.hasCfg = true;
    e.cfg = version;
    e.hasErr = true;
    snprintf(e.err, sizeof(e.err), "%s", why);
    enqueue(e, uptimeS());
  }

  void applyDownloaded(const char* body, size_t len, const String& sha, uint32_t version) {
    if (sha.length() == 64) {
      char hex[65];
      sha256HexOf((const uint8_t*)body, len, hex);
      if (!sha.equalsIgnoreCase(hex)) {
        reject(version, "checksum");
        return;
      }
    }
    Event e;
    uint32_t t0 = micros();
    xSemaphoreTake(lock_, portMAX_DELAY);
    e = logic_->applyConfig(body, scratch_);
    xSemaphoreGive(lock_);
    parseUs_ = micros() - t0;
    enqueue(e, uptimeS());
    if (e.event == EV_CONFIG_APPLIED) {
      // Last known good, on flash, whole or not at all (station_store.h).
      uint32_t p0 = millis();
      if (!store_.save(body, len))
        Serial.println("[STATION] config not saved (flash write failed); the old copy stays");
      persistMs_ = millis() - p0;
    }
  }

  // At boot: the last known good, checked twice -- the file's own checksum
  // (station_store.h), then the same whole-config validation as a download.
  // Nothing usable: silent and dark until the server sends one.
  void restoreConfig() {
    char* buf = static_cast<char*>(malloc(STATION_CONFIG_MAX_BYTES + 1));
    if (!buf) {
      Serial.println("[STATION] no memory to restore the config; silent until one arrives");
      return;
    }
    bool fromTmp = false;
    long n = store_.load(buf, STATION_CONFIG_MAX_BYTES + 1, &fromTmp);
    if (n < 0) {
      Serial.println("[STATION] no stored config; silent until one arrives");
    } else {
      Event e = logic_->applyConfig(buf, scratch_);
      if (e.event != EV_CONFIG_APPLIED)
        Serial.printf("[STATION] stored config refused (%s); silent until one arrives\n", e.err);
      else if (fromTmp)
        Serial.println("[STATION] restored the copy an interrupted save left");
    }
    free(buf);
  }

  static uint32_t uptimeS() { return millis() / 1000; }

  void enqueue(const Event& e, uint32_t ts) {
    QueuedEvent q;
    q.e = e;
    q.ts = ts;
    if (xQueueSend(events_, &q, 0) != pdTRUE) dropped_++;
  }

  static void taskEntry(void* self) { static_cast<StationRuntime*>(self)->run(); }

  void run() {
    TickClock clock;
    clock.begin(millis() / 1000);
    char pendingTap[17] = {0};
    for (;;) {
      uint32_t now = millis();
      char uid[17];
      if (reader_->poll(uid)) {
        memcpy(pendingTap, uid, sizeof(uid));
        tapAtMs_ = now;
      }
      uint32_t second;
      if (clock.due(now / 1000, pendingTap[0] != 0, &second)) {
        uint32_t t0 = micros();
        tickOnce(second, pendingTap);
        uint32_t took = micros() - t0;
        if (took > tickUsMax_) tickUsMax_ = took;
        pendingTap[0] = 0;
      }
      driveOutputs(millis());
      vTaskDelay(pdMS_TO_TICKS(STATION_TASK_PERIOD_MS));
    }
  }

  void tickOnce(uint32_t second, const char* tap) {
    int16_t c = 0;
    pcnt_get_counter_value(PCNT_UNIT_USED, &c);
    uint16_t now = (uint16_t)c;
    // A value lower than the last is the loop's drain (cleared at >= 30000):
    // count only what arrived since the clear, never a phantom jump.
    uint32_t pulses = now >= lastCount_ ? (uint32_t)(now - lastCount_) : (uint32_t)now;
    lastCount_ = now;
    long wall = wall_ ? wall_() : -1;
    TickInput in;
    in.uptimeS = second;
    in.hasWall = wall >= 0;
    in.wallMs = (int64_t)wall * 1000;
    in.pulses = pulses;
    in.total = __atomic_load_n(&total_, __ATOMIC_RELAXED);
    in.tap = tap;
    in.readerUp = reader_->up();
    in.inputSuspect = __atomic_load_n(&suspect_, __ATOMIC_RELAXED);
    in.outputFault = !outputsOk_;
    TickOutput out;
    xSemaphoreTake(lock_, portMAX_DELAY);
    logic_->tick(in, &out);
    xSemaphoreGive(lock_);
    lastTickS_ = second;
    for (int i = 0; i < out.eventCount; i++) enqueue(out.events[i], second);
    lastLight_ = out.light;
    lastSounding_ = out.sounding;
    if (out.beep != B_NONE) {
      uint32_t at = millis();
      startBeep(out.beep, at);
      if (tap && tap[0]) {  // a card's own feedback: tap read -> beep started
        tapMsLast_ = at - tapAtMs_;
        if (tapMsLast_ > tapMsMax_) tapMsMax_ = tapMsLast_;
      }
    }
  }

  void driveOutputs(uint32_t now) {
    Light light = lastLight_;
    bool sounding = lastSounding_;
    if (selfTest_) {
      selfTest_ = false;
      selfTestUntil_ = now + 4000;
      startBeep(B_ACCEPTED, now);
    }
    if (selfTestUntil_ && (int32_t)(selfTestUntil_ - now) > 0) {
      uint32_t phase = (selfTestUntil_ - now) / 1000;  // 3,2,1,0
      light = phase == 3 ? L_GREEN : phase == 2 ? L_AMBER : phase == 1 ? L_RED_BLINK : L_OFF;
      sounding = phase == 0;
    } else {
      selfTestUntil_ = 0;
    }
    OutputState s = governor_.refresh(light, sounding, now);
    xSemaphoreTake(lock_, portMAX_DELAY);
    outputsOk_ = outputs_.apply(s);
    xSemaphoreGive(lock_);
    lastBits_ = s.bits();
    serviceBeep(now);
  }

  // Onboard beeper: accepted one short, person two short, refused one long.
  void startBeep(Beep b, uint32_t now) {
    beepPattern_ = b;
    beepStart_ = now;
  }
  void serviceBeep(uint32_t now) {
    if (beepPattern_ == B_NONE) return;
    uint32_t t = now - beepStart_;
    bool on = false;
    if (beepPattern_ == B_ACCEPTED) on = t < 100;
    else if (beepPattern_ == B_PERSON) on = t < 100 || (t >= 200 && t < 300);
    else if (beepPattern_ == B_REFUSED) on = t < 500;
    digitalWrite(PIN_STATION_BEEPER, on ? HIGH : LOW);
    if (t >= 500) {
      beepPattern_ = B_NONE;
      digitalWrite(PIN_STATION_BEEPER, LOW);
    }
  }

  WallClock wall_ = nullptr;
  CardReader* reader_ = nullptr;
  Config* live_ = nullptr;
  Config* scratch_ = nullptr;
  StationLogic* logic_ = nullptr;
  ConfigStore<fs::LittleFSFS> store_{LittleFS, sha256HexOf, STATION_CONFIG_FILE,
                                     STATION_CONFIG_TMP};
  SemaphoreHandle_t lock_ = nullptr;
  QueueHandle_t events_ = nullptr;
  TaskHandle_t task_ = nullptr;
  Preferences prefs_;
  bool persistedAck_ = false;
  OutputGovernor governor_;
  Pca9554Outputs outputs_;
  volatile bool outputsOk_ = false;
  volatile uint8_t lastBits_ = 0;
  uint16_t lastCount_ = 0;
  uint64_t total_ = 0;
  bool suspect_ = false;
  volatile Light lastLight_ = L_OFF;
  volatile bool lastSounding_ = false;
  volatile uint32_t lastTickS_ = 0;
  volatile uint32_t dropped_ = 0;
  uint32_t lastPollMs_ = 0;
  volatile bool selfTest_ = false;
  uint32_t selfTestUntil_ = 0;
  Beep beepPattern_ = B_NONE;
  uint32_t beepStart_ = 0;
  uint32_t backlog_ = 0;
  uint32_t lost_ = 0;  // station rows the durable queue refused (flash full)
  // Bench measurements (printStatus).
  uint32_t parseUs_ = 0;
  uint32_t persistMs_ = 0;
  uint32_t tickUsMax_ = 0;
  uint32_t tapAtMs_ = 0;
  uint32_t tapMsLast_ = 0;
  uint32_t tapMsMax_ = 0;
};

}  // namespace station

#endif  // STATION_ENABLE
