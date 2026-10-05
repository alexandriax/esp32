#include "../firmware/sloth_pet/pet_settings.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// Independent Python zlib fixtures for legacy/current Settings records.
const uint8_t golden[] = {
  0x50,0x45,0x54,0x43,0x01,0x00,0x00,0x00,0x01,0x04,0x00,0x00,
  0x4D,0x6F,0x73,0x73,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x94,0xF2,0xB7,0x2C,
};
const uint8_t goldenV2[] = {
  0x50,0x45,0x54,0x43,0x02,0x00,0x00,0x00,0x01,0x04,0x02,0x00,
  0x4D,0x6F,0x73,0x73,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0xF3,0x7B,0x70,0x62,
};
const uint8_t goldenV3[] = {0x50,0x45,0x54,0x43,0x03,0x00,0x00,0x00,0x01,0x04,0x02,0x00,0x4d,0x6f,0x73,0x73,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xb5,0x40,0x17,0x07};
const uint8_t goldenConure[] = {
  0x50,0x45,0x54,0x43,0x02,0x03,0x00,0x00,0x01,0x04,0x02,0x00,
  0x4D,0x6F,0x73,0x73,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x5A,0xFD,0x26,0xC1,
};
const uint8_t legacyCustomized[] = {
  0x50,0x45,0x54,0x43,0x01,0x02,0x06,0x0D,0x00,0x08,0x00,0x00,
  0x4F,0x27,0x4D,0x6F,0x73,0x73,0x2D,0x37,0x00,0x00,0x00,0x00,
  0xE7,0x98,0x4E,0x44,
};

bool same(const sloth::Settings& a, const sloth::Settings& b) {
  return a.animal == b.animal && a.scene == b.scene && a.timeZone == b.timeZone &&
         a.screenRotation == b.screenRotation && a.showSubtitle == b.showSubtitle && a.shakeSensitivity == b.shakeSensitivity &&
         std::strcmp(a.name, b.name) == 0;
}

void rejected(const uint8_t* record, size_t size) {
  sloth::Settings output;
  output.animal = sloth::Animal::Cat;
  output.scene = sloth::Scene::Night;
  output.timeZone = 11;
  output.showSubtitle = false;
  output.shakeSensitivity = 4;
  assert(sloth::setName(output, "Keep Me"));
  const auto original = output;
  assert(!sloth::decodeSettings(record, size, output) && same(output, original));
}

int main() {
  static_assert(static_cast<int>(sloth::Animal::Sloth) == 0, "Persisted sloth ID");
  static_assert(static_cast<int>(sloth::Animal::Cat) == 1, "Persisted cat ID");
  static_assert(static_cast<int>(sloth::Animal::Frog) == 2, "Persisted frog ID");
  static_assert(static_cast<int>(sloth::Animal::SunConure) == 3, "Appended conure ID");
  static_assert(static_cast<int>(sloth::Animal::Count) == 4, "Four species");
  sloth::Settings conure;
  assert(sloth::decodeSettings(goldenConure, sizeof(goldenConure), conure));
  assert(conure.animal == sloth::Animal::SunConure);
  assert(std::strcmp(sloth::animalName(conure.animal), "SUN CONURE") == 0);
  uint8_t conureRecord[sloth::kSettingsRecordSize];
  assert(sloth::encodeSettings(conure, conureRecord));
  assert(conureRecord[4] == 3 && conureRecord[5] == 3 && conureRecord[11] == 0);
  sloth::Settings restoredConure;assert(sloth::decodeSettings(conureRecord,sizeof(conureRecord),restoredConure) && same(conure,restoredConure));
  sloth::Settings settings;
  assert(sloth::validSettings(settings));
  assert(settings.shakeSensitivity == 2 && sloth::kShakeSensitivityCount == 5);
  uint8_t record[sloth::kSettingsRecordSize];
  assert(sloth::encodeSettings(settings, record));
  assert(sizeof(goldenV3) == sizeof(record) && std::memcmp(record, goldenV3, sizeof(record)) == 0);
  sloth::Settings loaded;
  loaded.showSubtitle = false;
  loaded.shakeSensitivity = 4;
  assert(sloth::decodeSettings(golden, sizeof(golden), loaded) && same(settings, loaded));
  loaded.shakeSensitivity = 0;
  assert(sloth::decodeSettings(goldenV2, sizeof(goldenV2), loaded) && same(settings, loaded));
  assert(sloth::decodeSettings(legacyCustomized, sizeof(legacyCustomized), loaded));
  assert(loaded.animal == sloth::Animal::Frog && loaded.scene == sloth::Scene::UnderSea);
  assert(loaded.timeZone == 13 && !loaded.showSubtitle && loaded.shakeSensitivity == 2);
  assert(std::strcmp(loaded.name, "O'Moss-7") == 0);
  const auto migrated = loaded;
  assert(sloth::encodeSettings(loaded, record));
  assert(record[4] == 3 && record[10] == 2 && record[11] == 0);
  assert(sloth::decodeSettings(record, sizeof(record), loaded) && same(loaded, migrated));

  assert(sloth::setName(settings, "  O'Moss-7  ") && std::strcmp(settings.name, "O'Moss-7") == 0);
  const char* invalid[] = {"", " ", "\tMoss", "Moss\n", "Moss_", "Moss!", "M\xC3\xB6ss", "1234567890123"};
  for (const char* name : invalid) {
    const auto before = settings;
    assert(!sloth::setName(settings, name) && same(settings, before));
  }
  assert(!sloth::setName(settings, nullptr));
  assert(sloth::setName(settings, "123456789012"));
  assert(settings.name[12] == 0);
  assert(sloth::setName(settings, settings.name)); // Aliased input is safe.

  for (unsigned animal = 0; animal < static_cast<unsigned>(sloth::Animal::Count); ++animal) {
    for (unsigned scene = 0; scene < static_cast<unsigned>(sloth::Scene::Count); ++scene) {
      for (uint8_t zone = 0; zone < sloth::zoneCount(); ++zone) {
        settings.animal = static_cast<sloth::Animal>(animal);
        settings.scene = static_cast<sloth::Scene>(scene);
        settings.timeZone = zone;
        settings.showSubtitle = zone % 2;
        settings.screenRotation = zone % 4;
        settings.shakeSensitivity = zone % sloth::kShakeSensitivityCount;
        assert(sloth::encodeSettings(settings, record));
        assert(sloth::decodeSettings(record, sizeof(record), loaded) && same(settings, loaded));
      }
    }
  }

  rejected(nullptr, sizeof(golden));
  for (size_t size = 0; size < sizeof(golden); ++size) rejected(golden, size);
  uint8_t extended[sizeof(golden) + 1]{};
  std::memcpy(extended, golden, sizeof(golden));
  rejected(extended, sizeof(extended));
  const uint8_t* corruptionSources[] = {golden, goldenV2, goldenV3};
  for (const uint8_t* source : corruptionSources) {
    for (size_t byte = 0; byte < sizeof(golden); ++byte) {
      for (unsigned bit = 0; bit < 8; ++bit) {
        std::memcpy(record, source, sizeof(record));
        record[byte] ^= 1u << bit;
        rejected(record, sizeof(record));
      }
    }
  }
  // Semantically invalid records with independently computed, VALID CRCs.
  const struct { unsigned at; uint8_t value; uint32_t crc; } invalidFields[] = {
    {0,88,0xFEBACE2Au}, {4,4,0x073D228Bu}, {5,4,0xC5DFFF76u},
    {6,7,0xCD99F8F0u}, {7,14,0x6D930A02u}, {8,2,0x057F4666u},
    {9,0,0xA6CB97F7u}, {9,13,0x5AC5E5FBu}, {10,1,0xB1B813E2u},
    {11,1,0xF1212B11u}, {12,95,0xCC9481F7u}, {12,32,0x9A4A9F72u},
    {15,32,0x67688F27u}, {14,0,0x31C9CC0Fu}, {16,1,0xE01DF20Au},
  };
  for (const auto& bad : invalidFields) {
    std::memcpy(record, golden, sizeof(record));
    record[bad.at] = bad.value;
    for (unsigned byte = 0; byte < 4; ++byte) record[24 + byte] = bad.crc >> (byte * 8);
    rejected(record, sizeof(record));
  }
  const struct { unsigned at; uint8_t value; uint32_t crc; } invalidV2Fields[] = {
    {4,4,0xE653E626u}, {10,5,0x07BDD133u}, {10,255,0xBC96570Fu},
    {11,1,0xBFE6A276u}, {5,4,0x8B187611u}, {6,7,0x835E7197u}, {7,14,0x23548365u},
  };
  for (const auto& bad : invalidV2Fields) {
    std::memcpy(record, goldenV2, sizeof(record));
    record[bad.at] = bad.value;
    for (unsigned byte = 0; byte < 4; ++byte) record[24 + byte] = bad.crc >> (byte * 8);
    rejected(record, sizeof(record));
  }

  for (uint8_t invalidRotation : {uint8_t(4), uint8_t(255)}) {
    std::memcpy(record,goldenV3,sizeof(record));record[11]=invalidRotation;
    uint32_t crc=UINT32_MAX;for(unsigned i=0;i<24;++i){crc^=record[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xEDB88320u:0);}
    crc^=UINT32_MAX;for(unsigned b=0;b<4;++b)record[24+b]=crc>>(b*8);rejected(record,sizeof(record));
  }
  settings=sloth::Settings{};settings.screenRotation=4;assert(!sloth::validSettings(settings));

  // Invalid in-memory values must not partially overwrite the caller's blob.
  settings = sloth::Settings{};
  settings.animal = sloth::Animal::Count;
  std::memset(record, 0xA5, sizeof(record));
  assert(!sloth::encodeSettings(settings, record));
  for (uint8_t byte : record) assert(byte == 0xA5);
  settings = sloth::Settings{};
  settings.shakeSensitivity = sloth::kShakeSensitivityCount;
  assert(!sloth::validSettings(settings) && !sloth::encodeSettings(settings, record));
  for (uint8_t byte : record) assert(byte == 0xA5);
  settings = sloth::Settings{};
  std::memset(settings.name, 'X', sizeof(settings.name));
  assert(!sloth::validSettings(settings) && !sloth::encodeSettings(settings, record));
  settings = sloth::Settings{};
  assert(!sloth::encodeSettings(settings, nullptr));
  assert(!sloth::encodeSettings(settings, record, sizeof(record) - 1));
  assert(sloth::nextAnimal(sloth::Animal::Frog) == sloth::Animal::SunConure);
  assert(sloth::nextAnimal(sloth::Animal::SunConure) == sloth::Animal::Sloth);
  assert(sloth::nextAnimal(sloth::Animal::Count) == sloth::Animal::Sloth);
  assert(sloth::nextScene(sloth::Scene::UnderSea) == sloth::Scene::Jungle);
  assert(sloth::nextScene(sloth::Scene::Count) == sloth::Scene::Jungle);
  assert(std::strcmp(sloth::animalName(sloth::Animal::Cat), "Cat") == 0);
  assert(std::strcmp(sloth::sceneName(sloth::Scene::Meadow), "Meadow") == 0);
  const char* sensitivityLabels[] = {"Very Low", "Low", "Normal", "High", "Very High"};
  for (uint8_t level = 0; level < sloth::kShakeSensitivityCount; ++level)
    assert(std::strcmp(sloth::shakeSensitivityName(level), sensitivityLabels[level]) == 0);
  assert(std::strcmp(sloth::shakeSensitivityName(5), "Unknown") == 0);
  std::puts("Settings: v1 migration, sensitivity, names, CRC fixtures, corruption and atomic fallback passed");
}
