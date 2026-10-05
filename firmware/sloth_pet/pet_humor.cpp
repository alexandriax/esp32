#include "pet_humor.h"

#include <string.h>

namespace sloth {
#if __cplusplus < 201703L
constexpr size_t PetHumor::kMaxJokes;
constexpr size_t PetHumor::kRecentCount;
constexpr size_t PetHumor::kRecentFamilies;
constexpr size_t PetHumor::kRecordSize;
constexpr uint16_t PetHumor::kNoJoke;
#endif
namespace {
constexpr size_t kSeenOffset = 32;
constexpr size_t kRecentOffset = kSeenOffset + PetHumor::kMaxJokes / 8;
constexpr size_t kChecksumOffset = PetHumor::kRecordSize - 4;
static_assert(kRecentOffset + PetHumor::kRecentCount * 2 == kChecksumOffset, "history layout");

uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t read32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
      (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void write16(uint8_t* p, uint16_t value) { p[0] = value; p[1] = value >> 8; }
void write32(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t crc32(const uint8_t* p, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= p[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return ~crc;
}

// Positive weights keep every mood available. Freshness tiers are applied first,
// so the current mood cannot confine a long session to one part of the corpus.
unsigned moodWeight(HumorMood mood, const Snapshot& pet) {
  const unsigned food = pet.fullness > 100 ? 100 : pet.fullness;
  const unsigned joy = pet.happiness > 100 ? 100 : pet.happiness;
  const unsigned rest = pet.energy > 100 ? 100 : pet.energy;
  switch (mood) {
    case HumorMood::Neutral: return 16;
    case HumorMood::Hungry: return food <= 30 ? 64 : food < 55 ? 32 : 4;
    case HumorMood::Full: return food >= 80 ? 48 : food >= 60 ? 24 : 4;
    case HumorMood::Bored: return joy <= 30 ? 64 : joy < 55 ? 32 : 4;
    case HumorMood::Happy: return joy >= 80 ? 48 : joy >= 60 ? 24 : 4;
    case HumorMood::Tired: return rest <= 30 ? 64 : rest < 55 ? 32 : 4;
    case HumorMood::Rested: return rest >= 80 ? 48 : rest >= 60 ? 24 : 4;
    case HumorMood::Sleeping: return pet.sleeping ? 96 : 1;
  }
  return 1;
}
}  // namespace

PetHumor::PetHumor() { begin(0x6d2b79f5u); }

void PetHumor::begin(uint32_t hardwareSeed) {
  memset(seen_, 0, sizeof(seen_));
  for (size_t i = 0; i < kRecentCount; ++i) recent_[i] = kNoJoke;
  const size_t count = jokeCount();
  bankCount_ = count <= kMaxJokes ? static_cast<uint16_t>(count) : 0;
  bankVersion_ = humorBankVersion();
  random_ = hardwareSeed ^ bankVersion_ ^ (static_cast<uint32_t>(bankCount_) * 0x9e3779b9u);
  if (!random_) random_ = 0x6d2b79f5u;
  cycle_ = 0;
  seenCount_ = 0;
  current_ = kNoJoke;
  recentCount_ = recentHead_ = 0;
}

bool PetHumor::bankMatches() const {
  return bankCount_ && jokeCount() == bankCount_ && humorBankVersion() == bankVersion_;
}

bool PetHumor::seen(uint16_t id) const { return (seen_[id >> 3] & (1u << (id & 7u))) != 0; }
unsigned PetHumor::historyLimit() const { return bankCount_ < kRecentCount ? bankCount_ : kRecentCount; }

uint16_t PetHumor::recent(unsigned age) const {
  const unsigned limit = historyLimit();
  return recent_[(recentHead_ + limit - 1u - age) % limit];
}

uint32_t PetHumor::randomBelow(uint32_t limit) {
  random_ ^= random_ << 13;
  random_ ^= random_ >> 17;
  random_ ^= random_ << 5;
  // Multiply-high gives a bounded-time draw with <=1/2^32 probability error;
  // unlike modulo it does not magnify low PRNG bits or require a rejection loop.
  return static_cast<uint32_t>((static_cast<uint64_t>(random_) * limit) >> 32);
}

uint16_t PetHumor::next(const Snapshot& pet) {
  if (!bankMatches()) {
    const size_t count = jokeCount();
    if (!count || count > kMaxJokes) return kNoJoke;
    // The embedded corpus is normally immutable for a process lifetime. If a
    // host/test swaps it, old IDs must not address the replacement corpus.
    begin(random_);
  }
  if (seenCount_ == bankCount_) {
    memset(seen_, 0, sizeof(seen_));
    seenCount_ = 0;
    if (cycle_ != UINT32_MAX) ++cycle_;
  }

  uint16_t recentFamilies[kRecentFamilies];
  uint8_t recentTopics[3];
  const unsigned familyCount = recentCount_ < kRecentFamilies ? recentCount_ : kRecentFamilies;
  const unsigned topicCount = recentCount_ < 3 ? recentCount_ : 3;
  for (unsigned i = 0; i < familyCount; ++i) recentFamilies[i] = jokeAt(recent(i)).family;
  for (unsigned i = 0; i < topicCount; ++i) recentTopics[i] = jokeAt(recent(i)).topic;
  const unsigned exclusion = recentCount_ < bankCount_ ? recentCount_ : bankCount_ - 1u;

  uint16_t chosen = kNoJoke;
  int bestTier = -1;
  uint32_t totalWeight = 0;
  for (uint16_t id = 0; id < bankCount_; ++id) {
    if (seen(id)) continue;
    bool justSeen = false;
    // All recent entries from this cycle were already eliminated by seen().
    // Only the first few draws after rollover need a previous-cycle ID check.
    for (unsigned i = seenCount_; i < exclusion; ++i)
      if (recent(i) == id) { justSeen = true; break; }
    if (justSeen) continue;
    const Joke& joke = jokeAt(id);
    bool freshFamily = true;
    for (unsigned i = 0; i < familyCount; ++i)
      if (recentFamilies[i] == joke.family) { freshFamily = false; break; }
    const int tier = (freshFamily ? 2 : 0) + (!topicCount || recentTopics[0] != joke.topic ? 1 : 0);
    if (tier < bestTier) continue;
    if (tier > bestTier) { bestTier = tier; totalWeight = 0; chosen = kNoJoke; }
    unsigned topicOccurrences = 0;
    for (unsigned i = 0; i < topicCount; ++i) if (recentTopics[i] == joke.topic) ++topicOccurrences;
    const unsigned topicWeight = topicOccurrences == 0 ? 16 : topicOccurrences == 1 ? 4 : topicOccurrences == 2 ? 2 : 1;
    const uint32_t weight = moodWeight(joke.mood, pet) * topicWeight;
    totalWeight += weight;
    if (randomBelow(totalWeight) < weight) chosen = id;
  }
  // Valid history always leaves a candidate: the bank is larger than the recent
  // exclusion, and previous-cycle entries age out as new entries are selected.
  if (chosen == kNoJoke) return kNoJoke;
  seen_[chosen >> 3] |= static_cast<uint8_t>(1u << (chosen & 7u));
  ++seenCount_;
  current_ = chosen;
  recent_[recentHead_] = chosen;
  recentHead_ = static_cast<uint8_t>((recentHead_ + 1u) % historyLimit());
  if (recentCount_ < historyLimit()) ++recentCount_;
  return chosen;
}

HumorMood PetHumor::currentMood() const {
  return bankMatches() && current_ < bankCount_ ? jokeAt(current_).mood : HumorMood::Neutral;
}

bool PetHumor::validHistory() const {
  if (!bankMatches() || !random_ || seenCount_ > bankCount_ ||
      recentCount_ > historyLimit() || recentHead_ >= historyLimit()) return false;
  if (recentCount_ < historyLimit() && recentHead_ != recentCount_) return false;
  unsigned bits = 0;
  for (unsigned id = 0; id < kMaxJokes; ++id) {
    if (!seen(static_cast<uint16_t>(id))) continue;
    if (id >= bankCount_) return false;
    ++bits;
  }
  if (bits != seenCount_) return false;
  if (!seenCount_) {
    if (cycle_ || recentCount_ || current_ != kNoJoke) return false;
  } else if (!recentCount_ || current_ >= bankCount_ || current_ != recent(0) || !seen(current_)) return false;
  for (unsigned i = 0; i < kRecentCount; ++i) {
    if (i >= recentCount_ && recentCount_ < historyLimit()) {
      if (recent_[i] != kNoJoke) return false;
    } else if (i >= historyLimit()) {
      if (recent_[i] != kNoJoke) return false;
    } else {
      if (recent_[i] >= bankCount_) return false;
      for (unsigned earlier = 0; earlier < i; ++earlier)
        if (recent_[earlier] == recent_[i]) return false;
    }
  }
  const unsigned currentCycleRecent = seenCount_ < recentCount_ ? seenCount_ : recentCount_;
  for (unsigned age = 0; age < currentCycleRecent; ++age) if (!seen(recent(age))) return false;
  if (!cycle_ && recentCount_ != (seenCount_ < historyLimit() ? seenCount_ : historyLimit())) return false;
  return true;
}

bool PetHumor::encode(uint8_t* out, size_t size) const {
  if (!out || size != kRecordSize || !validHistory()) return false;
  memset(out, 0, size);
  memcpy(out, "MHUM", 4);
  out[4] = 1;
  write16(out + 6, static_cast<uint16_t>(kRecordSize));
  write32(out + 8, bankVersion_);
  write16(out + 12, bankCount_);
  write16(out + 14, seenCount_);
  write32(out + 16, cycle_);
  write32(out + 20, random_);
  write16(out + 24, current_);
  out[26] = recentCount_;
  out[27] = recentHead_;
  memcpy(out + kSeenOffset, seen_, sizeof(seen_));
  for (size_t i = 0; i < kRecentCount; ++i) write16(out + kRecentOffset + i * 2, recent_[i]);
  write32(out + kChecksumOffset, crc32(out, kChecksumOffset));
  return true;
}

bool PetHumor::restore(const uint8_t* record, size_t size) {
  if (!record || size != kRecordSize || memcmp(record, "MHUM", 4) || record[4] != 1 || record[5] ||
      read16(record + 6) != kRecordSize || record[28] || record[29] || record[30] || record[31] ||
      read32(record + kChecksumOffset) != crc32(record, kChecksumOffset)) return false;
  PetHumor decoded;
  decoded.bankVersion_ = read32(record + 8);
  decoded.bankCount_ = read16(record + 12);
  if (!decoded.bankCount_ || decoded.bankCount_ > kMaxJokes) return false;
  decoded.seenCount_ = read16(record + 14);
  decoded.cycle_ = read32(record + 16);
  decoded.random_ = read32(record + 20);
  decoded.current_ = read16(record + 24);
  decoded.recentCount_ = record[26];
  decoded.recentHead_ = record[27];
  memcpy(decoded.seen_, record + kSeenOffset, sizeof(decoded.seen_));
  for (size_t i = 0; i < kRecentCount; ++i) decoded.recent_[i] = read16(record + kRecentOffset + i * 2);
  if (!decoded.validHistory()) return false;
  *this = decoded;
  return true;
}

}  // namespace sloth
