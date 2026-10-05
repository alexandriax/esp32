#pragma once

#include <stddef.h>
#include <stdint.h>
#include "local_time.h"

namespace sloth {

// Enum values are persisted; append future choices before Count, without
// reordering existing values.
enum class Animal : uint8_t { Sloth = 0, Cat = 1, Frog = 2, SunConure = 3, Count };
enum class Scene : uint8_t { Jungle, Meadow, Night, NYC, Space, Island, UnderSea, Count };

constexpr size_t kPetNameMax = 12;
constexpr uint8_t kShakeSensitivityCount = 5;
struct Settings {
  Animal animal = Animal::Sloth;
  Scene scene = Scene::Jungle;
  uint8_t timeZone = 0;
  bool showSubtitle = true;
  uint8_t screenRotation = 0; // Clockwise quarter turns, shared by every screen.
  uint8_t shakeSensitivity = 2; // Very Low, Low, Normal, High, Very High.
  char name[kPetNameMax + 1] = "Moss";
};

const char* animalName(Animal animal);
const char* sceneName(Scene scene);
const char* shakeSensitivityName(uint8_t sensitivity);
Animal nextAnimal(Animal animal);
Scene nextScene(Scene scene);

// Trim outer ASCII spaces. Accept 1..12 ASCII letters, digits, spaces, hyphens,
// or apostrophes. Failure leaves the existing settings unchanged.
bool setName(Settings& settings, const char* name);
bool validSettings(const Settings& settings);

// Version 3, 28 bytes, independent of the pet-care record and struct padding:
// 0..3 "PETC"; 4 version; 5 animal; 6 scene; 7 zone; 8 subtitle (0/1);
// 9 name length; 10 shake sensitivity; 11 screen rotation (0..3); 12..23 name, zero padded;
// 24..27 little-endian CRC-32/ISO-HDLC over 0..23.
// Versions 1/2 remain readable and migrate rotation to 0. Their reserved byte
// must be zero; v1 also migrates shake sensitivity to Normal (2).
constexpr size_t kSettingsRecordSize = 28;
// Exactly one record is required. Encode/decode leave outputs untouched on
// failure, so callers can initialize Settings defaults before attempting load.
bool encodeSettings(const Settings& settings, uint8_t* out,
                    size_t size = kSettingsRecordSize);
bool decodeSettings(const uint8_t* data, size_t size, Settings& out);

}  // namespace sloth
