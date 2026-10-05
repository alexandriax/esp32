#pragma once

#include <stddef.h>
#include <stdint.h>
#include "pet_state.h"

namespace sloth {

enum class HumorMood : uint8_t { Neutral, Hungry, Full, Bored, Happy, Tired, Rested, Sleeping };
struct Joke {
  const char* text;
  uint16_t family;  // Shared premise, including paraphrases; not merely a topic.
  uint8_t topic;
  HumorMood mood;
};

// Implemented by the immutable generated corpus. Changing any text or metadata
// must change humorBankVersion(), so old IDs cannot silently change meanings.
size_t jokeCount();
const Joke& jokeAt(size_t index);
uint32_t humorBankVersion();

// Allocation-free, complete-cycle selection. Freshness takes precedence over
// mood: never repeat a line within a cycle or within the last 64 selections;
// prefer a premise absent from the last 24, then a topic unlike the last one.
// Mood and the last three topics weight the remaining choices. For tiny test
// banks of <=64 items, the recent-ID exclusion is necessarily count-1 instead.
class PetHumor {
 public:
  static constexpr size_t kMaxJokes = 4096;
  static constexpr size_t kRecentCount = 64;
  static constexpr size_t kRecentFamilies = 24;
  static constexpr size_t kRecordSize = 676;
  static constexpr uint16_t kNoJoke = UINT16_MAX;

  PetHumor();
  void begin(uint32_t hardwareSeed);
  uint16_t next(const Snapshot& pet);
  uint16_t currentId() const { return current_; }
  uint16_t seenCount() const { return seenCount_; }
  uint32_t cycle() const { return cycle_; }  // Completed cycles, saturating.
  HumorMood currentMood() const;

  // Exact-size, versioned, explicit little-endian encoding, including seen map,
  // random state and recent IDs. CRC-32 detects corruption. Rejection is atomic;
  // callers can keep the newly seeded state after a missing/incompatible record.
  bool encode(uint8_t* out, size_t size = kRecordSize) const;
  bool restore(const uint8_t* record, size_t size);

 private:
  uint8_t seen_[kMaxJokes / 8];
  uint16_t recent_[kRecentCount];
  uint32_t random_, cycle_, bankVersion_;
  uint16_t bankCount_, seenCount_, current_;
  uint8_t recentCount_, recentHead_;

  bool bankMatches() const;
  bool seen(uint16_t id) const;
  uint16_t recent(unsigned age) const;  // age 0 is the latest selection.
  unsigned historyLimit() const;
  uint32_t randomBelow(uint32_t limit);
  bool validHistory() const;
};

}  // namespace sloth
