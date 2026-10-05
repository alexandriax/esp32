#include "../firmware/sloth_pet/pet_record.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using sloth::Snapshot;

static bool equal(const Snapshot& a, const Snapshot& b) {
  return a.fullness == b.fullness && a.happiness == b.happiness &&
      a.energy == b.energy && a.sleeping == b.sleeping &&
      a.fullness_seconds == b.fullness_seconds &&
      a.happiness_seconds == b.happiness_seconds &&
      a.energy_seconds == b.energy_seconds && a.active_seconds == b.active_seconds;
}

// Golden fixtures and migration CRCs independently computed with Python zlib.
static const uint8_t kV1[24] = {
  0x53,0x4c,0x54,0x48,0x01,0x00,0x50,0x51,0x52,0x00,0xff,0x01,
  0x83,0x03,0x57,0x02,0x78,0x56,0x34,0x12,0x56,0xe8,0x6e,0x8e
};
static const uint8_t kV2[24] = {
  0x53,0x4c,0x54,0x48,0x02,0x00,0x50,0x51,0x52,0x00,0xff,0x01,
  0x83,0x03,0x57,0x02,0x78,0x56,0x34,0x12,0xa4,0x5c,0xa6,0xa7
};
static const uint8_t kV3[28] = {
  0x53,0x4c,0x54,0x48,0x03,0x00,0x50,0x51,0x52,0x00,0x99,0x00,
  0x2b,0x01,0x57,0x02,0x78,0x56,0x34,0x12,0x00,0xf1,0x53,0x65,
  0x62,0x74,0x34,0xc4
};
static const uint32_t kUtc = 1700000000u;

static Snapshot fixture() {
  Snapshot s = {};
  s.fullness = 80; s.happiness = 81; s.energy = 82;
  s.fullness_seconds = 153; s.happiness_seconds = 299; s.energy_seconds = 599;
  s.active_seconds = 0x12345678u;
  return s;
}

static void golden_and_roundtrip() {
  uint8_t encoded[sloth::kRecordSize];
  const Snapshot expected = fixture();
  sloth::encodeRecord(expected, kUtc, encoded);
  assert(memcmp(encoded, kV3, sizeof(kV3)) == 0);
  Snapshot decoded = {};
  uint32_t utc = 0;
  assert(sloth::decodeRecord(kV3, sizeof(kV3), decoded, utc));
  assert(equal(decoded, expected) && utc == kUtc);
  // Legacy food/joy clocks migrate; awake energy retains its 599-second clock.
  assert(sloth::decodeRecord(kV1, sizeof(kV1), decoded, utc));
  assert(equal(decoded, expected) && utc == 0);
  utc = kUtc;
  assert(sloth::decodeRecord(kV2, sizeof(kV2), decoded, utc));
  assert(equal(decoded, expected) && utc == 0);
  sloth::PetState original;
  original.advance(721);
  original.act(sloth::Action::Nap);
  original.advance(17);
  sloth::encodeRecord(original.snapshot(), kUtc, encoded);
  assert(sloth::decodeRecord(encoded, sizeof(encoded), decoded, utc));
  assert(equal(decoded, original.snapshot()) && utc == kUtc);
  sloth::PetState restored;
  assert(restored.restore(decoded));
  original.advance(1501); restored.advance(1501);
  assert(equal(original.snapshot(), restored.snapshot()));
}

static void legacy_migration_and_validation() {
  // CRCs cover old fields before migration; expected fractions are literal values.
  const struct MigrationCase {
    uint8_t version, sleeping;
    uint16_t food, joy, energy;
    bool valid;
    uint16_t new_food, new_joy, new_energy;
    uint32_t crc;
  } cases[] = {
    {1,1,511,899,0,true,153,299,0,0x24ab5cb9u},
    {1,1,511,899,9,true,153,299,0,0x03a40d71u},
    {1,1,511,899,10,true,153,299,1,0x85307fdfu},
    {1,1,511,899,19,true,153,299,1,0xa1e92c8cu},
    {1,1,511,899,20,true,153,299,2,0xbcec1c34u},
    {1,1,511,899,29,true,153,299,2,0x9be34dfcu},
    {1,1,511,899,30,false,0,0,0,0x1d773f52u},
    {2,1,511,899,2,true,153,299,2,0x40ab4940u},
    {2,1,511,899,3,false,0,0,0,0x8bf79ae5u},
    {2,0,0,899,599,true,0,299,599,0x519a9ff3u},
    {2,0,3,899,599,true,0,299,599,0xbaad24f0u},
    {2,0,4,899,599,true,1,299,599,0x58713f89u},
    {2,0,599,899,599,true,179,299,599,0x24a42a44u},
    {2,0,600,899,599,false,0,0,0,0xd5af71c9u},
    {2,0,65535,899,599,false,0,0,0,0xf99244acu},
    {2,0,511,0,599,true,153,0,599,0x4bfac400u},
    {2,0,511,2,599,true,153,0,599,0x09dfc37du},
    {2,0,511,3,599,true,153,1,599,0xc575c3e3u},
    {2,0,511,899,599,true,153,299,599,0xa7a65ca4u},
    {2,0,511,900,599,false,0,0,0,0xad6355bdu},
    {2,0,511,65535,599,false,0,0,0,0xff8788d6u},
    {2,0,511,899,600,false,0,0,0,0x56f0ee71u},
    {2,2,511,899,0,false,0,0,0,0x94818e4au}
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    const MigrationCase& c = cases[i];
    uint8_t legacy[sloth::kLegacyRecordSize];
    memcpy(legacy, kV1, sizeof(legacy));
    legacy[4] = c.version; legacy[9] = c.sleeping;
    const uint16_t clocks[] = {c.food, c.joy, c.energy};
    for (size_t clock = 0; clock < 3; ++clock) {
      legacy[10 + clock * 2] = static_cast<uint8_t>(clocks[clock]);
      legacy[11 + clock * 2] = static_cast<uint8_t>(clocks[clock] >> 8);
    }
    for (size_t byte = 0; byte < 4; ++byte)
      legacy[20 + byte] = static_cast<uint8_t>(c.crc >> (byte * 8));
    Snapshot decoded = fixture(), expected = fixture();
    uint32_t utc = kUtc;
    assert(sloth::decodeRecord(legacy, sizeof(legacy), decoded, utc) == c.valid);
    if (c.valid) {
      expected.sleeping = c.sleeping;
      expected.fullness_seconds = c.new_food;
      expected.happiness_seconds = c.new_joy;
      expected.energy_seconds = c.new_energy;
      assert(utc == 0 && equal(decoded, expected));
      // Saving migrated state must not scale the clocks a second time.
      uint8_t upgraded[sloth::kRecordSize];
      sloth::encodeRecord(decoded, kUtc, upgraded);
      assert(upgraded[4] == 3);
      assert(sloth::decodeRecord(upgraded, sizeof(upgraded), decoded, utc));
    }
    assert(equal(decoded, expected) && utc == kUtc);
  }
  const uint8_t migrated_sleep[28] = {
    0x53,0x4c,0x54,0x48,0x03,0x00,0x50,0x51,0x52,0x01,0x99,0x00,
    0x2b,0x01,0x02,0x00,0x78,0x56,0x34,0x12,0x00,0x00,0x00,0x00,
    0xa7,0x48,0x4c,0x14
  };
  Snapshot s = fixture(); s.sleeping = 1; s.energy_seconds = 2;
  uint8_t encoded[sloth::kRecordSize];
  sloth::encodeRecord(s, 0, encoded);
  assert(memcmp(encoded, migrated_sleep, sizeof(encoded)) == 0);
  uint32_t utc = kUtc;
  assert(sloth::decodeRecord(migrated_sleep, sizeof(migrated_sleep), s, utc));
  assert(utc == 0);
  sloth::PetState pet;
  assert(pet.restore(s)); pet.advance(1);
  assert(pet.snapshot().energy == 83 && pet.snapshot().happiness == 80);
  assert(pet.snapshot().energy_seconds == 0 && pet.snapshot().happiness_seconds == 0);
}

static void corruption_and_lengths() {
  const uint8_t* records[] = {kV1, kV2, kV3};
  const size_t sizes[] = {sizeof(kV1), sizeof(kV2), sizeof(kV3)};
  for (size_t record = 0; record < 3; ++record) {
    const Snapshot before = fixture(); Snapshot out = before;
    uint32_t utc = kUtc;
    for (size_t i = 0; i < sizes[record]; ++i) {
      for (unsigned bit = 0; bit < 8; ++bit) {
        uint8_t damaged[sloth::kRecordSize];
        memcpy(damaged, records[record], sizes[record]);
        damaged[i] ^= static_cast<uint8_t>(1u << bit);
        assert(!sloth::decodeRecord(damaged, sizes[record], out, utc));
        assert(equal(out, before) && utc == kUtc);
      }
    }
    for (size_t size = 0; size < sizes[record]; ++size) {
      assert(!sloth::decodeRecord(records[record], size, out, utc));
      assert(equal(out, before) && utc == kUtc);
    }
    uint8_t extended[sloth::kRecordSize + 1] = {};
    memcpy(extended, records[record], sizes[record]);
    assert(!sloth::decodeRecord(extended, sizes[record] + 1, out, utc));
    assert(!sloth::decodeRecord(NULL, sizes[record], out, utc));
    assert(equal(out, before) && utc == kUtc);
  }
  sloth::encodeRecord(fixture(), kUtc, NULL);
}

static void invalid_state_versions_and_timestamps() {
  const uint8_t future[28] = { // Unknown version, valid CRC.
    0x53,0x4c,0x54,0x48,0x04,0x00,0x50,0x51,0x52,0x00,0x99,0x00,
    0x2b,0x01,0x57,0x02,0x78,0x56,0x34,0x12,0x00,0xf1,0x53,0x65,
    0xf1,0xd2,0x70,0x25
  };
  const Snapshot before = fixture(); Snapshot out = before;
  uint32_t utc = kUtc;
  assert(!sloth::decodeRecord(future, sizeof(future), out, utc));
  assert(equal(out, before) && utc == kUtc);
  uint8_t encoded[sloth::kRecordSize];
  for (int field = 0; field < 8; ++field) {
    Snapshot invalid = before;
    switch (field) {
      case 0: invalid.fullness = 101; break;
      case 1: invalid.happiness = 255; break;
      case 2: invalid.energy = 101; break;
      case 3: invalid.sleeping = 2; break;
      case 4: invalid.fullness_seconds = 180; break;
      case 5: invalid.happiness_seconds = 300; break;
      case 6: invalid.sleeping = 1; invalid.energy_seconds = 3; break;
      case 7: invalid.energy_seconds = 600; break;
    }
    sloth::encodeRecord(invalid, kUtc, encoded);
    assert(!sloth::decodeRecord(encoded, sizeof(encoded), out, utc));
    assert(equal(out, before) && utc == kUtc);
  }
  const uint32_t valid_times[] = {0u,946684800u,1700000000u,4102444799u};
  for (size_t i = 0; i < sizeof(valid_times) / sizeof(valid_times[0]); ++i) {
    sloth::encodeRecord(before, valid_times[i], encoded);
    assert(sloth::decodeRecord(encoded, sizeof(encoded), out, utc));
    assert(equal(out, before) && utc == valid_times[i]);
  }
  const uint32_t invalid_times[] = {1u,946684799u,4102444800u,UINT32_MAX};
  utc = kUtc;
  for (size_t i = 0; i < sizeof(invalid_times) / sizeof(invalid_times[0]); ++i) {
    sloth::encodeRecord(before, invalid_times[i], encoded);
    assert(!sloth::decodeRecord(encoded, sizeof(encoded), out, utc));
    assert(equal(out, before) && utc == kUtc);
  }
}

int main() {
  static_assert(sloth::kRecordSize == 28, "Current save format size changed");
  static_assert(sloth::kLegacyRecordSize == 24, "Legacy save format size changed");
  golden_and_roundtrip();
  legacy_migration_and_validation();
  corruption_and_lengths();
  invalid_state_versions_and_timestamps();
  puts("pet_record: all tests passed");
}
