// ============================================================================
// ack_validation.h — Miki Wire hardening (Phase-0 finding F8): ack bounding.
// ----------------------------------------------------------------------------
// Pure, dependency-free logic (no Arduino.h, no ESP-IDF) -- same "pure
// function extracted for testability" pattern this project already uses for
// sensor_stuck.h / ota_version_policy.h / timestamp_parse.h /
// credential_display.h. Host-tested by test/native_cpp/test_ack_validation.cpp.
//
// WHY THIS EXISTS: sync.h previously applied any parsed ack_seq to the queue
// unconditionally. The ack rule ("cumulative: server has all seq <= ack")
// makes an oversized ack catastrophic -- a buggy or malicious response of,
// say, 2^31 would permanently mark every queued-but-untransmitted row as
// delivered (EventQueue::ackThrough is monotonic and its pruning is
// irreversible). A legitimate cumulative ack can never exceed the highest
// seq the device has actually transmitted: the server cannot have resolved
// a record it has never received. Clamping to that bound caps the damage of
// a bad server value at "acks what was genuinely sent" -- plausibly the
// server's intent -- while preserving liveness against a buggy-but-working
// backend (an outright reject would freeze pruning forever). The wire
// contract is untouched: this validates a response, changes no request.
// ============================================================================
#pragma once
#include <stdint.h>

// Returns the ack value that is safe to apply, given the highest seq actually
// transmitted in the batch this response acknowledges. Sets *wasClamped true
// when the server's value exceeded that bound (callers log it loudly).
// Callers must handle parse failure (<0) BEFORE this -- the parsed value here
// is already known non-negative.
inline uint32_t boundAckSeq(uint32_t parsedAck, uint32_t maxSentSeq, bool* wasClamped) {
  if (parsedAck > maxSentSeq) {
    if (wasClamped) *wasClamped = true;
    return maxSentSeq;
  }
  if (wasClamped) *wasClamped = false;
  return parsedAck;
}

// Boot-time seq resume floor (Phase-0 F4's ack-deadlock corollary): if the
// totalizer checkpoint regressed (both slots corrupt -> lastSeq restarts low)
// while the ack record still holds a higher watermark, every new row's seq
// would be filtered as "already acked" by pending() and never transmitted --
// the exact deadlock the manual reset_ack command exists to clear. Resuming
// from whichever is higher keeps seq globally monotonic across checkpoint
// loss with zero effect on a healthy boot (checkpointSeq >= ackedSeq always
// holds when the checkpoint is intact, since seq only ever ratchets forward
// of what the server has acknowledged).
inline uint32_t resumeSeqFloor(uint32_t checkpointSeq, uint32_t ackedSeq) {
  return (checkpointSeq >= ackedSeq) ? checkpointSeq : ackedSeq;
}
