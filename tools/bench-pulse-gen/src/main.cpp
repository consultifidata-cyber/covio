// bench-pulse-gen -- an exact, counted pulse source for the Machine Station bench.
//
// Runs on any ESP32 DevKit (NOT the station board). GPIO 25 drives the LED side
// of an optocoupler module (PC817 class); the opto's transistor closes the
// station board's DI1 to 0 V, exactly as the proximity sensor does on Wire
// Line 1. The count is decided by this program, never by a person pressing a
// switch, so "input pulses" in the bench sheet is a fact, not an estimate
// (bench rows B02, B03, B24, B35-B37).
//
// Serial, 115200 baud, one command per line:
//   run <hz> <count>      exactly <count> pulses at <hz> (1..2000), then stop
//   wire <minutes>        a realistic Wire Line 1 day: 6 Hz runs of 10-40 min,
//                         stops of 1-10 min, for <minutes>; prints each change
//   stop                  stop now (the count so far is printed)
//   total                 pulses emitted since boot or the last `zero`
//   zero                  set the total to 0
// Every line it prints starts with "PG " and ends with the running total, so a
// bench log is the evidence.
#include <Arduino.h>
#include <esp_timer.h>

static const int PIN_OUT = 25;

static esp_timer_handle_t g_timer = nullptr;
static volatile uint64_t g_total = 0;      // rising edges emitted since zero
static volatile uint32_t g_left = 0;       // pulses still to emit in this run
static volatile bool g_level = false;
static volatile bool g_running = false;

static void IRAM_ATTR onTick(void*) {
  if (!g_running) return;
  if (!g_level) {
    if (g_left == 0) {
      g_running = false;
      return;
    }
    g_level = true;
    digitalWrite(PIN_OUT, HIGH);
    g_total = g_total + 1;
    g_left = g_left - 1;
  } else {
    g_level = false;
    digitalWrite(PIN_OUT, LOW);
  }
}

static void startRun(uint32_t hz, uint32_t count) {
  esp_timer_stop(g_timer);
  g_level = false;
  digitalWrite(PIN_OUT, LOW);
  g_left = count;
  g_running = count > 0;
  // Two ticks a pulse (high half, low half): 50 % duty.
  uint64_t halfUs = 500000ULL / hz;
  if (g_running) esp_timer_start_periodic(g_timer, halfUs);
}

static void stopRun() {
  g_running = false;
  esp_timer_stop(g_timer);
  digitalWrite(PIN_OUT, LOW);
  g_level = false;
}

static void waitDone(uint32_t hz, uint32_t count, uint32_t t0) {
  while (g_running) {
    if (Serial.available()) {
      String l = Serial.readStringUntil('\n');
      l.trim();
      if (l == "stop") {
        stopRun();
        break;
      }
    }
    delay(5);
  }
  esp_timer_stop(g_timer);
  Serial.printf("PG DONE hz=%u asked=%u emitted=%u ms=%u total=%llu\n", hz, count,
                count - g_left, millis() - t0, (unsigned long long)g_total);
}

static void wireDay(uint32_t minutes) {
  uint32_t endMs = millis() + minutes * 60000UL;
  randomSeed(esp_random());
  while ((int32_t)(endMs - millis()) > 0) {
    uint32_t runS = 600 + random(1800);  // 10-40 min
    uint32_t stopS = 60 + random(540);   // 1-10 min
    Serial.printf("PG RUN 6Hz for %us total=%llu\n", runS, (unsigned long long)g_total);
    uint32_t t0 = millis();
    startRun(6, runS * 6);
    waitDone(6, runS * 6, t0);
    if (!((int32_t)(endMs - millis()) > 0)) break;
    Serial.printf("PG STOP for %us total=%llu\n", stopS, (unsigned long long)g_total);
    uint32_t s0 = millis();
    while (millis() - s0 < stopS * 1000UL) {
      if (Serial.available() && Serial.readStringUntil('\n').indexOf("stop") >= 0) {
        Serial.printf("PG END total=%llu\n", (unsigned long long)g_total);
        return;
      }
      delay(20);
    }
  }
  Serial.printf("PG END total=%llu\n", (unsigned long long)g_total);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_OUT, OUTPUT);
  digitalWrite(PIN_OUT, LOW);
  esp_timer_create_args_t args = {};
  args.callback = &onTick;
  args.name = "pulse";
  esp_timer_create(&args, &g_timer);
  Serial.println("PG ready: run <hz> <count> | wire <minutes> | stop | total | zero");
}

void loop() {
  if (!Serial.available()) {
    delay(5);
    return;
  }
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.startsWith("run ")) {
    int sp = line.indexOf(' ', 4);
    uint32_t hz = (uint32_t)line.substring(4, sp).toInt();
    uint32_t count = (uint32_t)line.substring(sp + 1).toInt();
    if (hz < 1 || hz > 2000 || count == 0) {
      Serial.println("PG REFUSED: run <1..2000 Hz> <count > 0>");
      return;
    }
    Serial.printf("PG START hz=%u count=%u total=%llu\n", hz, count, (unsigned long long)g_total);
    uint32_t t0 = millis();
    startRun(hz, count);
    waitDone(hz, count, t0);
  } else if (line.startsWith("wire ")) {
    wireDay((uint32_t)line.substring(5).toInt());
  } else if (line == "total") {
    Serial.printf("PG TOTAL total=%llu\n", (unsigned long long)g_total);
  } else if (line == "zero") {
    g_total = 0;
    Serial.println("PG ZERO total=0");
  } else if (line == "stop") {
    stopRun();
    Serial.printf("PG STOPPED total=%llu\n", (unsigned long long)g_total);
  } else if (line.length()) {
    Serial.println("PG ? run <hz> <count> | wire <minutes> | stop | total | zero");
  }
}
