#pragma once

#include <stdint.h>

namespace sloth {

enum { kPetReactionCount = 12, kPetReactionDurationMs = 4000 };

// A separate shuffle bag keeps gestures fresh without consuming joke-selection
// randomness or writing another record every time the pet speaks.
class PetReaction {
 public:
  void begin(uint32_t seed) {
    random_ = seed ? seed : 0x61c88647u;
    cursor_ = kPetReactionCount;
    previous_ = -1;
    clear();
  }

  int start(uint32_t now) {
    if (cursor_ == kPetReactionCount) refill();
    previous_ = bag_[cursor_++];
    since_ = now;
    playing_ = true;
    return previous_;
  }

  void clear() { playing_ = false; }
  int active(uint32_t now) const {
    return playing_ && now - since_ < kPetReactionDurationMs ? previous_ : -1;
  }
  uint32_t elapsed(uint32_t now) const { return playing_ ? now - since_ : 0; }

 private:
  uint32_t random_ = 0x61c88647u, since_ = 0;
  uint8_t bag_[kPetReactionCount] = {}, cursor_ = kPetReactionCount;
  int previous_ = -1;
  bool playing_ = false;

  uint32_t nextRandom() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_;
  }
  void refill() {
    for (unsigned i = 0; i < kPetReactionCount; ++i) bag_[i] = i;
    for (unsigned i = kPetReactionCount - 1; i; --i) {
      const unsigned j = nextRandom() % (i + 1);
      const uint8_t swap = bag_[i]; bag_[i] = bag_[j]; bag_[j] = swap;
    }
    if (bag_[0] == previous_) {
      const unsigned j = 1 + nextRandom() % (kPetReactionCount - 1);
      const uint8_t swap = bag_[0]; bag_[0] = bag_[j]; bag_[j] = swap;
    }
    cursor_ = 0;
  }
};

}  // namespace sloth
