#ifndef SLOTH_PET_PET_TIMELINE_H
#define SLOTH_PET_PET_TIMELINE_H

#include <stdint.h>

namespace sloth {

// One owner of elapsed time. The caller advances PetState with each returned
// interval exactly once, including begin()'s boot interval. During normal use,
// call advance(now) before an action and before capturing a snapshot for saving.
// Save the snapshot and savedUtc() together in one atomic persistence record.
// RTC readings never independently advance the pet during an active session.
class PetTimeline {
 public:
  // PCF85063 supports years 2000..2099. Zero means unknown/untrusted time.
  // These bounds are UTC Unix seconds: inclusive 2000-01-01, exclusive 2100-01-01.
  static constexpr uint32_t kFirstUtc = 946684800u;
  static constexpr uint32_t kEndUtc = 4102444800u;

  PetTimeline() : lastMs_(0), remainderMs_(0), utc_(0), started_(false) {}

  static bool validUtc(uint32_t utc) {
    return utc >= kFirstUtc && utc < kEndUtc;
  }

  // Initialize once after restoring the snapshot. rtcUtc must come from an
  // established clock: a valid-looking date alone cannot establish authority.
  // The board layer passes 0 when RTC validity/authority is unknown (including
  // a legacy save without a timestamp), and host synchronization may establish
  // a new anchor later. Invalid saved timestamps never produce boot catch-up.
  // A valid backward RTC skips the unknowable interval and starts a fresh
  // anchor at rtcUtc. It must not underflow into a huge elapsed interval.
  uint32_t begin(uint32_t monotonicMs, uint32_t savedUtc, uint32_t rtcUtc) {
    lastMs_ = monotonicMs;
    remainderMs_ = 0;
    utc_ = validUtc(rtcUtc) ? rtcUtc : 0;
    started_ = true;
    return validUtc(savedUtc) && utc_ >= savedUtc ? utc_ - savedUtc : 0;
  }

  // Returns whole powered-on seconds since the previous accounted time.
  // Fractions survive repeated calls, actions, saves, and clock anchoring.
  // Unsigned subtraction handles millis() rollover. Call at least once per
  // 2^32 milliseconds (about 49.7 days); longer unobserved spans are ambiguous.
  uint32_t advance(uint32_t monotonicMs) {
    if (!started_) {
      begin(monotonicMs, 0, 0);
      return 0;
    }
    const uint64_t elapsedMs = static_cast<uint64_t>(
        static_cast<uint32_t>(monotonicMs - lastMs_)) + remainderMs_;
    lastMs_ = monotonicMs;
    const uint32_t seconds = static_cast<uint32_t>(elapsedMs / 1000u);
    remainderMs_ = static_cast<uint16_t>(elapsedMs % 1000u);
    if (utc_ != 0) {
      // Do not wrap a timestamp or save a date outside the RTC's year range.
      utc_ = seconds >= kEndUtc - utc_ ? 0 : utc_ + seconds;
    }
    return seconds;
  }

  // Timestamp paired with the current accounted snapshot, not a fresh RTC
  // read. Projecting it from monotonic elapsed time tolerates transient RTC
  // I/O failures without either losing or charging elapsed seconds twice.
  uint32_t savedUtc() const { return utc_; }

  // Relabel the already-accounted snapshot after an authoritative host/RTC
  // synchronization. First advance(now) and apply its result to PetState.
  // This changes neither gameplay nor the pending fraction of a live second;
  // corrections are clock changes, not additional time spent caring for Moss.
  // Invalid UTC revokes the anchor. Coarse RTC seconds bound pairing precision
  // to one second; subsecond time cannot be reconstructed after total power loss.
  void anchorUtc(uint32_t rtcUtc) {
    utc_ = validUtc(rtcUtc) ? rtcUtc : 0;
  }

 private:
  uint32_t lastMs_;
  uint16_t remainderMs_;
  uint32_t utc_;
  bool started_;
};

}  // namespace sloth

#endif  // SLOTH_PET_PET_TIMELINE_H
