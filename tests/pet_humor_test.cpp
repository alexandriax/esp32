#include "../firmware/sloth_pet/pet_humor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace {
sloth::Joke bank[sloth::PetHumor::kMaxJokes];
size_t bankSize = 2048;
uint32_t bankVersion = 0x31d495e7;

void fillBank(size_t size = 2048) {
  bankSize = size;
  for (size_t i = 0; i < sloth::PetHumor::kMaxJokes; ++i) {
    bank[i].text = "Fixture text; IDs distinguish unique test lines.";
    bank[i].family = static_cast<uint16_t>(i / 4);
    bank[i].topic = static_cast<uint8_t>((i / 8) % 16);
    bank[i].mood = static_cast<sloth::HumorMood>(i % 8);
  }
}

sloth::Snapshot neutralPet() {
  sloth::Snapshot pet = {};
  pet.fullness = pet.happiness = pet.energy = 65;
  return pet;
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

void updateChecksum(uint8_t* record) {
  const size_t end = sloth::PetHumor::kRecordSize - 4;
  const uint32_t crc = crc32(record, end);
  for (unsigned i = 0; i < 4; ++i) record[end + i] = static_cast<uint8_t>(crc >> (i * 8));
}

void assertUnchanged(const sloth::PetHumor& humor, const uint8_t* saved) {
  uint8_t after[sloth::PetHumor::kRecordSize];
  assert(humor.encode(after));
  assert(memcmp(after, saved, sizeof(after)) == 0);
}

void completeCycles(size_t size, unsigned cycles) {
  fillBank(size);
  sloth::PetHumor humor;
  humor.begin(0x76543210);
  sloth::Snapshot pet = neutralPet();
  bool seen[sloth::PetHumor::kMaxJokes] = {};
  uint16_t recent[64] = {};
  unsigned recentCount = 0, head = 0;
  unsigned firstTopics[16] = {}, firstQuarters[4] = {};
  for (unsigned draw = 0; draw < size * cycles; ++draw) {
    if (draw % size == 0) memset(seen, 0, sizeof(seen));
    // Care changes throughout a cycle cannot invalidate its global coverage.
    pet.fullness = static_cast<uint8_t>((draw * 17u) % 101u);
    pet.happiness = static_cast<uint8_t>((draw * 29u) % 101u);
    pet.energy = static_cast<uint8_t>((draw * 43u) % 101u);
    pet.sleeping = draw % 19u == 0;
    const uint16_t id = humor.next(pet);
    assert(id < size && !seen[id]);
    for (unsigned i = 0; i < recentCount; ++i) assert(id != recent[i]);
    assert(humor.currentId() == id && humor.currentMood() == bank[id].mood);
    assert(humor.seenCount() == draw % size + 1 && humor.cycle() == draw / size);
    if (draw < 256) { ++firstTopics[bank[id].topic]; ++firstQuarters[id * 4u / size]; }

    // A premise repeats inside 24 only when all eligible fresh premises have
    // been consumed. This checks the fallback against independently tracked IDs.
    bool repeatedFamily = false;
    for (unsigned age = 0; age < recentCount && age < 24; ++age)
      if (bank[recent[(head + 63 - age) % 64]].family == bank[id].family) repeatedFamily = true;
    if (repeatedFamily) {
      for (unsigned candidate = 0; candidate < size; ++candidate) {
        if (seen[candidate]) continue;
        bool excluded = false, fresh = true;
        for (unsigned age = 0; age < recentCount; ++age) {
          const uint16_t old = recent[(head + 63 - age) % 64];
          if (old == candidate) excluded = true;
          if (age < 24 && bank[old].family == bank[candidate].family) fresh = false;
        }
        assert(excluded || !fresh);
      }
    }
    seen[id] = true;
    recent[head] = id;
    head = (head + 1) % 64;
    if (recentCount < 64) ++recentCount;
    if (draw % size == size - 1) for (unsigned i = 0; i < size; ++i) assert(seen[i]);
  }
  for (unsigned topic = 0; topic < 16; ++topic) assert(firstTopics[topic] >= 5);
  for (unsigned quarter = 0; quarter < 4; ++quarter) assert(firstQuarters[quarter] >= 35);
}

void stateBias() {
  fillBank();
  sloth::Snapshot baseline = neutralPet();
  for (unsigned requested = 1; requested < 8; ++requested) {
    sloth::Snapshot context = baseline;
    if (requested == static_cast<unsigned>(sloth::HumorMood::Hungry)) context.fullness = 10;
    if (requested == static_cast<unsigned>(sloth::HumorMood::Full)) context.fullness = 95;
    if (requested == static_cast<unsigned>(sloth::HumorMood::Bored)) context.happiness = 10;
    if (requested == static_cast<unsigned>(sloth::HumorMood::Happy)) context.happiness = 95;
    if (requested == static_cast<unsigned>(sloth::HumorMood::Tired)) context.energy = 10;
    if (requested == static_cast<unsigned>(sloth::HumorMood::Rested)) context.energy = 95;
    if (requested == static_cast<unsigned>(sloth::HumorMood::Sleeping)) context.sleeping = 1;
    unsigned baselineMatches = 0, contextMatches = 0, counts[8] = {};
    for (uint32_t seed = 1; seed <= 12; ++seed) {
      sloth::PetHumor control, weighted;
      control.begin(seed * 11235813u); weighted.begin(seed * 11235813u);
      for (unsigned draw = 0; draw < 96; ++draw) {
        const auto a = bank[control.next(baseline)].mood;
        const auto b = bank[weighted.next(context)].mood;
        baselineMatches += static_cast<unsigned>(a) == requested;
        contextMatches += static_cast<unsigned>(b) == requested;
        ++counts[static_cast<unsigned>(b)];
      }
    }
    // Expect a strong practical preference, not an exclusive mood playlist.
    assert(contextMatches * 10 > baselineMatches * 13);
    for (unsigned mood = 0; mood < 8; ++mood) assert(counts[mood] > 0);
    printf("humor mood %u: baseline=%u context=%u /1152\n", requested, baselineMatches, contextMatches);
  }
}

void persistence() {
  fillBank();
  sloth::Snapshot pet = neutralPet();
  sloth::PetHumor live, rebooted;
  live.begin(0x18273645);
  uint8_t record[sloth::PetHumor::kRecordSize], pristine[sloth::PetHumor::kRecordSize];
  assert(live.encode(record));
  assert(rebooted.restore(record, sizeof(record)));
  assert(rebooted.currentId() == sloth::PetHumor::kNoJoke && rebooted.seenCount() == 0);
  for (unsigned draw = 0; draw < bankSize + 130; ++draw) {
    if (draw < 70 || draw % 37 == 0 || draw >= bankSize - 1) {
      assert(live.encode(record));
      rebooted.begin(draw + 3);  // A new boot seed must not override saved history.
      assert(rebooted.restore(record, sizeof(record)));
      assertUnchanged(rebooted, record);
    }
    pet.fullness = static_cast<uint8_t>(draw % 101);
    assert(live.next(pet) == rebooted.next(pet));
  }
  assert(live.encode(pristine));
  // Corruption anywhere, truncation, extension, null, version and bank changes
  // leave the currently working selector entirely unchanged.
  for (size_t offset = 0; offset < sizeof(record); ++offset) {
    memcpy(record, pristine, sizeof(record)); record[offset] ^= 0x80;
    assert(!live.restore(record, sizeof(record)));
    assertUnchanged(live, pristine);
  }
  assert(!live.restore(nullptr, sizeof(record)));
  assert(!live.restore(pristine, sizeof(pristine) - 1));
  assert(!live.restore(pristine, sizeof(pristine) + 1));
  assert(!live.encode(nullptr));
  memset(record, 0x5a, sizeof(record));
  assert(!live.encode(record, sizeof(record) - 1));
  for (uint8_t byte : record) assert(byte == 0x5a);

  // Even a recomputed checksum cannot make malformed state structurally valid.
  const unsigned invalidOffsets[] = {4, 5, 6, 8, 12, 14, 24, 26, 27, 28, 32};
  for (unsigned offset : invalidOffsets) {
    memcpy(record, pristine, sizeof(record)); record[offset] = 0xff;
    updateChecksum(record);
    assert(!live.restore(record, sizeof(record)));
    assertUnchanged(live, pristine);
  }
  memcpy(record, pristine, sizeof(record)); memset(record + 20, 0, 4); updateChecksum(record);
  assert(!live.restore(record, sizeof(record)));
  // Duplicate recent IDs, bits beyond corpus size and a wrong current ID fail.
  memcpy(record, pristine, sizeof(record)); memcpy(record + 546, record + 544, 2); updateChecksum(record);
  assert(!live.restore(record, sizeof(record)));
  memcpy(record, pristine, sizeof(record)); record[32 + bankSize / 8] |= 1; updateChecksum(record);
  assert(!live.restore(record, sizeof(record)));
  assertUnchanged(live, pristine);
  sloth::PetHumor changedBank;
  ++bankVersion;
  assert(!rebooted.restore(pristine, sizeof(pristine)));
  assert(changedBank.next(pet) < bankSize);  // A valid replacement starts fresh.
  assert(changedBank.seenCount() == 1 && changedBank.cycle() == 0);
  --bankVersion;
  assertUnchanged(live, pristine);
  assertUnchanged(rebooted, pristine);
}

void edgeCases() {
  const sloth::Snapshot pet = neutralPet();
  for (size_t count = 1; count <= 65; ++count) {
    fillBank(count);
    sloth::PetHumor humor;
    uint8_t record[sloth::PetHumor::kRecordSize];
    uint16_t prior = sloth::PetHumor::kNoJoke;
    for (unsigned draw = 0; draw < count * 3; ++draw) {
      const uint16_t id = humor.next(pet);
      assert(id < count && (count == 1 || id != prior));
      prior = id;
      assert(humor.encode(record));
      sloth::PetHumor saved; assert(saved.restore(record, sizeof(record)));
      assertUnchanged(saved, record);
    }
  }
  const size_t invalidCounts[] = {0, sloth::PetHumor::kMaxJokes + 1};
  for (size_t count : invalidCounts) {
    fillBank(count);
    sloth::PetHumor humor;
    uint8_t record[sloth::PetHumor::kRecordSize];
    assert(humor.next(pet) == sloth::PetHumor::kNoJoke);
    assert(!humor.encode(record));
  }
  fillBank(65);
  sloth::PetHumor saturated;
  saturated.begin(bankVersion ^ (65u * 0x9e3779b9u));  // A zero mixed seed is repaired.
  uint8_t record[sloth::PetHumor::kRecordSize];
  for (unsigned i = 0; i < 65; ++i) assert(saturated.next(pet) < 65);
  assert(saturated.encode(record));
  memset(record + 16, 0xff, 4); updateChecksum(record);
  assert(saturated.restore(record, sizeof(record)));
  assert(saturated.next(pet) < 65 && saturated.cycle() == UINT32_MAX && saturated.seenCount() == 1);
  fillBank();
  // A corpus with a single premise and topic still progresses through all lines.
  for (size_t i = 0; i < bankSize; ++i) { bank[i].family = 7; bank[i].topic = 2; }
  sloth::PetHumor fallback;
  bool seen[sloth::PetHumor::kMaxJokes] = {};
  for (size_t i = 0; i < bankSize; ++i) {
    const uint16_t id = fallback.next(pet);
    assert(id < bankSize && !seen[id]); seen[id] = true;
  }
  fillBank();
}
}  // namespace

namespace sloth {
size_t jokeCount() { return bankSize; }
const Joke& jokeAt(size_t index) { assert(index < bankSize && index < PetHumor::kMaxJokes); return bank[index]; }
uint32_t humorBankVersion() { return bankVersion; }
}  // namespace sloth

int main() {
  static_assert(sloth::PetHumor::kRecordSize < 1024, "persisted humor history must fit under 1KB");
  static_assert(sizeof(sloth::PetHumor) < 1024, "selector must fit under 1KB without heap allocation");
  completeCycles(2048, 2);
  completeCycles(4096, 1);
  stateBias();
  persistence();
  edgeCases();
  puts("humor checks passed: full cycles, no repeats, last-64 rollover, recent-premise preference, broad range/topics, all meter biases, record continuity/corruption/version checks, tiny/empty/oversized banks");
}
