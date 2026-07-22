// ============================================================================
// fake_totalizer.h — trivial IQueueOffsetCheckpoint for native host tests
// ----------------------------------------------------------------------------
// RISK-04 phase-2 remediation. A real Totalizer depends on the ESP32 PCNT
// hardware peripheral and cannot be constructed on a host machine. This is
// the entire real Totalizer surface EventQueue ever touches (see
// queue_offset_checkpoint.h) -- a plain in-memory implementation, not a
// simulation of Totalizer's own checkpoint/CRC logic (which is unrelated to
// what this harness tests).
// ============================================================================
#pragma once
#include "../../queue_offset_checkpoint.h"

class FakeQueueOffsetCheckpoint : public IQueueOffsetCheckpoint {
public:
  void setQueueOffset(uint32_t segment, uint32_t offset) override {
    segment_ = segment;
    offset_ = offset;
    setCount++;
  }
  uint32_t queueOffsetSegment() const override { return segment_; }
  uint32_t queueOffsetOffset()  const override { return offset_; }

  int setCount = 0;   // test introspection: how many times the checkpoint advanced

private:
  uint32_t segment_ = 0;
  uint32_t offset_ = 0;
};
