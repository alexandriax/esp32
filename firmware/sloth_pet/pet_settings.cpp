#include "pet_settings.h"

#include <string.h>

namespace sloth {
namespace {
bool allowedNameChar(char value) {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
         (value >= '0' && value <= '9') || value == ' ' || value == '-' || value == '\'';
}

size_t validNameLength(const char* name) {
  size_t size = 0;
  while (size <= kPetNameMax && name[size]) {
    if (!allowedNameChar(name[size])) return 0;
    ++size;
  }
  if (!size || size > kPetNameMax || name[0] == ' ' || name[size - 1] == ' ') return 0;
  return size;
}

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
  }
  return crc ^ UINT32_MAX;
}

uint32_t read32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}
}  // namespace

const char* animalName(Animal animal) {
  switch (animal) {
    case Animal::Sloth: return "Sloth";
    case Animal::Cat: return "Cat";
    case Animal::Frog: return "Frog";
    case Animal::SunConure: return "SUN CONURE";
    default: return "Unknown";
  }
}

const char* sceneName(Scene scene) {
  switch (scene) {
    case Scene::Jungle: return "Jungle";
    case Scene::Meadow: return "Meadow";
    case Scene::Night: return "Night";
    case Scene::NYC: return "NYC";
    case Scene::Space: return "Space";
    case Scene::Island: return "Island";
    case Scene::UnderSea: return "Under the Sea";
    default: return "Unknown";
  }
}

const char* shakeSensitivityName(uint8_t sensitivity) {
  static const char* const labels[] = {"Very Low", "Low", "Normal", "High", "Very High"};
  return sensitivity < kShakeSensitivityCount ? labels[sensitivity] : "Unknown";
}

Animal nextAnimal(Animal animal) {
  const unsigned value = static_cast<unsigned>(animal);
  return value < static_cast<unsigned>(Animal::Count)
      ? static_cast<Animal>((value + 1) % static_cast<unsigned>(Animal::Count)) : Animal::Sloth;
}

Scene nextScene(Scene scene) {
  const unsigned value = static_cast<unsigned>(scene);
  return value < static_cast<unsigned>(Scene::Count)
      ? static_cast<Scene>((value + 1) % static_cast<unsigned>(Scene::Count)) : Scene::Jungle;
}

bool setName(Settings& settings, const char* name) {
  if (!name) return false;
  while (*name == ' ') ++name;
  size_t size = strlen(name);
  while (size && name[size - 1] == ' ') --size;
  if (!size || size > kPetNameMax) return false;
  char validated[kPetNameMax + 1] = {};
  for (size_t i = 0; i < size; ++i) {
    if (!allowedNameChar(name[i])) return false;
    validated[i] = name[i];
  }
  memcpy(settings.name, validated, sizeof(validated));
  return true;
}

bool validSettings(const Settings& settings) {
  return static_cast<unsigned>(settings.animal) < static_cast<unsigned>(Animal::Count) &&
         static_cast<unsigned>(settings.scene) < static_cast<unsigned>(Scene::Count) &&
         settings.timeZone < zoneCount() && settings.screenRotation < 4 && settings.shakeSensitivity < kShakeSensitivityCount &&
         validNameLength(settings.name) != 0;
}

bool encodeSettings(const Settings& settings, uint8_t* out, size_t size) {
  if (!out || size != kSettingsRecordSize || !validSettings(settings)) return false;
  uint8_t bytes[kSettingsRecordSize] = {};
  bytes[0] = 'P'; bytes[1] = 'E'; bytes[2] = 'T'; bytes[3] = 'C';
  bytes[4] = 3;
  bytes[5] = static_cast<uint8_t>(settings.animal);
  bytes[6] = static_cast<uint8_t>(settings.scene);
  bytes[7] = settings.timeZone;
  bytes[8] = settings.showSubtitle ? 1 : 0;
  bytes[9] = static_cast<uint8_t>(validNameLength(settings.name));
  bytes[10] = settings.shakeSensitivity;
  bytes[11] = settings.screenRotation;
  memcpy(bytes + 12, settings.name, bytes[9]);
  const uint32_t crc = crc32(bytes, 24);
  for (unsigned byte = 0; byte < 4; ++byte) bytes[24 + byte] = static_cast<uint8_t>(crc >> (byte * 8));
  memcpy(out, bytes, sizeof(bytes));
  return true;
}

bool decodeSettings(const uint8_t* data, size_t size, Settings& out) {
  if (!data || size != kSettingsRecordSize || data[0] != 'P' || data[1] != 'E' ||
      data[2] != 'T' || data[3] != 'C' || (data[4] < 1 || data[4] > 3) || data[8] > 1 ||
      data[9] < 1 || data[9] > kPetNameMax || (data[4] < 3 ? data[11] != 0 : data[11] > 3) ||
      (data[4] == 1 ? data[10] != 0 : data[10] >= kShakeSensitivityCount) ||
      read32(data + 24) != crc32(data, 24)) return false;
  for (unsigned i = data[9]; i < kPetNameMax; ++i) if (data[12 + i]) return false;
  Settings decoded;
  decoded.animal = static_cast<Animal>(data[5]);
  decoded.scene = static_cast<Scene>(data[6]);
  decoded.timeZone = data[7];
  decoded.showSubtitle = data[8] != 0;
  decoded.shakeSensitivity = data[4] == 1 ? 2 : data[10];
  decoded.screenRotation = data[4] < 3 ? 0 : data[11];
  memset(decoded.name, 0, sizeof(decoded.name));
  memcpy(decoded.name, data + 12, data[9]);
  if (!validSettings(decoded) || validNameLength(decoded.name) != data[9]) return false;
  out = decoded;
  return true;
}

}  // namespace sloth
