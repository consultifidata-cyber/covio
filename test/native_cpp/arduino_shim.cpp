#include "arduino_shim.h"

SerialShim Serial;

static uint32_t g_fakeMillis = 0;

void nativeTestSetMillis(uint32_t ms) { g_fakeMillis = ms; }
uint32_t millis() { return g_fakeMillis; }
