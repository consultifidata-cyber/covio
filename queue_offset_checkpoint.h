// ============================================================================
// queue_offset_checkpoint.h — RISK-04 phase-2 remediation
// ----------------------------------------------------------------------------
// Deliberately dependency-free (no Arduino.h, no LittleFS.h, no ESP-IDF
// headers): this is the ONLY seam EventQueue (queue.h) needs into Totalizer
// (totalizer.h), extracted so a native host test can supply a fake
// implementation without pulling in the PCNT hardware peripheral or any
// other ESP32-only dependency Totalizer itself has.
// ============================================================================
#pragma once
#include <stdint.h>

class IQueueOffsetCheckpoint {
public:
  virtual ~IQueueOffsetCheckpoint() {}
  virtual void     setQueueOffset(uint32_t segment, uint32_t offset) = 0;
  virtual uint32_t queueOffsetSegment() const = 0;
  virtual uint32_t queueOffsetOffset()  const = 0;
};
