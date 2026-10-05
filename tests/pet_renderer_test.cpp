#include "../firmware/sloth_pet/pet_renderer.h"
#include "../firmware/sloth_pet/pet_reaction.h"
#include "../firmware/sloth_pet/pet_ambient.h"
#include "../firmware/sloth_pet/pet_speech.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static void ambientExplorationAndBounds() {
  sloth::Snapshot pet{}; pet.fullness = 78; pet.happiness = 90; pet.energy = 62;
  const sloth::Snapshot original = pet;
  sloth::Settings settings;
  uint16_t baseline[240*240], guarded[240*240+2];
  guarded[0] = 0x1234; guarded[240*240+1] = 0x5678;
  const uint32_t samples[] = {0, 5000, 6000, 9000, 12000, 15999, 16000, 20000,
      24000, 28000, 33999, 34000, 38000, 43999, 44000, 48000, 52000, 56000,
      61999, 62000, 66000, 71999, 72000, 100000, 143999};
  for (unsigned animal = 0; animal < static_cast<unsigned>(sloth::Animal::Count); ++animal) {
    settings.animal = static_cast<sloth::Animal>(animal);
    const uint32_t cycle = sloth::ambientCycleMs(settings.animal);
    for (unsigned scene = 0; scene < static_cast<unsigned>(sloth::Scene::Count); ++scene) {
      settings.scene = static_cast<sloth::Scene>(scene);
      unsigned activities = 0; int left = 0, right = 0;
      bool groomed = false, rested = false, articulated = false;
      sloth::drawPet(baseline, pet, 0, "TEST", 0, 0, sloth::Hud(), settings);
      for (uint32_t phase : samples) {
        const uint32_t ms = static_cast<uint32_t>(static_cast<uint64_t>(phase) * cycle / 72000);
        const auto pose = sloth::ambientPose(settings.animal, settings.scene, ms, false);
        activities |= 1u << static_cast<unsigned>(pose.activity);
        if (pose.x < left) left = pose.x;
        if (pose.x > right) right = pose.x;
        groomed = groomed || pose.groom > 128;
        rested = rested || pose.closed;
        articulated = articulated || pose.step != 0 || pose.wing != 0;
        assert(pose.x >= -26 && pose.x <= 26 && pose.y >= -7 && pose.y <= 5);
        sloth::drawPet(guarded+1, pet, 0, "TEST", ms, 0, sloth::Hud(), settings);
        assert(guarded[0] == 0x1234 && guarded[240*240+1] == 0x5678);
        assert(std::memcmp(baseline, guarded+1, 46*240*sizeof(uint16_t)) == 0);
        assert(std::memcmp(baseline+171*240, guarded+1+171*240,
                           (240-171)*240*sizeof(uint16_t)) == 0);
        if (phase == 20000 || phase == 48000) {
          unsigned changed = 0;
          for (int pixel = 46*240; pixel < 171*240; ++pixel)
            changed += baseline[pixel] != guarded[1+pixel];
          assert(changed > 1500); // Whole character travels, not a particle overlay.
        }
        const auto sleeping = sloth::ambientPose(settings.animal, settings.scene, ms, true);
        assert(sleeping.x == 0 && sleeping.y == 0 && sleeping.step == 0 && sleeping.wing == 0);
        assert(sleeping.groom == 0 && sleeping.headX == 0 && sleeping.headY == 0);
      }
      assert(activities == 15 && left <= -21 && right >= 21);
      assert(groomed && rested && articulated);
      for (uint32_t edge : {0u, cycle-1, cycle, UINT32_MAX}) {
        sloth::drawPet(guarded+1, pet, 0, "TEST", edge, 0, sloth::Hud(), settings);
        assert(std::memcmp(baseline, guarded+1, 46*240*sizeof(uint16_t)) == 0);
        assert(std::memcmp(baseline+171*240, guarded+1+171*240,
                           (240-171)*240*sizeof(uint16_t)) == 0);
      }
    }
    // Smooth position at phase boundaries and when the outing reverses direction.
    for (uint32_t ms = 1; ms < cycle*2; ms += 97) {
      const auto a = sloth::ambientPose(settings.animal, sloth::Scene::Jungle, ms-1, false);
      const auto b = sloth::ambientPose(settings.animal, sloth::Scene::Jungle, ms, false);
      assert(b.x-a.x >= -1 && b.x-a.x <= 1);
    }
  }
  assert(std::memcmp(&pet, &original, sizeof(pet)) == 0);
}

static void roamingTouchTargets() {
  sloth::Snapshot state{};
  sloth::Settings settings;
  for (unsigned animal = 0; animal < static_cast<unsigned>(sloth::Animal::Count); ++animal) {
    settings.animal = static_cast<sloth::Animal>(animal);
    for (unsigned scene = 0; scene < static_cast<unsigned>(sloth::Scene::Count); ++scene) {
      settings.scene = static_cast<sloth::Scene>(scene);
      const uint32_t cycle = sloth::ambientCycleMs(settings.animal);
      const uint32_t phases[] = {12000, 20000, 28000, 38000, 48000, 56000, 68000, 100000};
      for (uint32_t phase : phases) {
        const uint32_t ms = static_cast<uint32_t>(static_cast<uint64_t>(phase) * cycle / 72000);
        const auto ambient = sloth::ambientPose(settings.animal, settings.scene, ms, false);
        const int center = 120 + ambient.x;
        // Face, both outer feet/wings, and the long tail follow the animal.
        assert(sloth::hitTestPet(center, 77 + ambient.y, state, ms, settings));
        assert(sloth::hitTestPet(center-40, 125 + ambient.y, state, ms, settings));
        assert(sloth::hitTestPet(center+40, 125 + ambient.y, state, ms, settings));
        if (settings.animal == sloth::Animal::SunConure)
          assert(sloth::hitTestPet(center, 161 + ambient.y, state, ms, settings));
        // The pointing hand can extend beyond the body's ordinary target.
        assert(sloth::hitTestPet(center+76, 87, state, ms, settings, 0, 9, 900));
        assert(!sloth::hitTestPet(center, 45, state, ms, settings));
        assert(!sloth::hitTestPet(center, 171, state, ms, settings));
        assert(!sloth::hitTestPet(22, 184, state, ms, settings));
        assert(!sloth::hitTestPet(214, 210, state, ms, settings));
        assert(!sloth::hitTestPet(INT32_MIN, INT32_MAX, state, ms, settings));
        assert(!sloth::hitTestPet(INT32_MAX, INT32_MIN, state, ms, settings));
      }
      state.sleeping = 1;
      assert(sloth::hitTestPet(120, 80, state, cycle/3, settings));
      assert(!sloth::hitTestPet(40, 80, state, cycle/3, settings));
      state.sleeping = 0;
    }
  }
  // Regression: these visible locations lay outside the former fixed ellipse.
  settings.animal = sloth::Animal::Sloth; settings.scene = sloth::Scene::Space;
  assert(!sloth::hitTestPetSpeech(54, 85, nullptr));
  assert(sloth::hitTestPet(54, 85, state, 20000, settings));
  settings.animal = sloth::Animal::SunConure;
  assert(!sloth::hitTestPetSpeech(120, 161, nullptr));
  assert(sloth::hitTestPet(120, 161, state, 1000, settings));
  // Bubble target stays in screen coordinates while the animal moves away.
  const char* speech = "I AM EXPLORING THIS VERY SMALL WORLD.";
  const auto bubble = sloth::speechLayout(speech);
  assert(sloth::hitTestPetSpeech(bubble.x+8, bubble.y+8, speech));
  assert(sloth::hitTestPetSpeech(bubble.x+bubble.width-9, bubble.y+8, speech));
}

int main() {
  ambientExplorationAndBounds();
  roamingTouchTargets();
  for (int y = -1; y <= 240; ++y) {
    for (int x = -1; x <= 240; ++x) {
      int expected = -1;
      int matchingTargets = 0;
      if (y >= 190 && y < 229) {
        for (int a = 0; a < 4; ++a) {
          const int right = a == 3 ? 231 : 73 + 62 * a;
          if (x >= 11 + 62 * a && x < right) {
            expected = a;
            ++matchingTargets;
          }
        }
      }
      assert(matchingTargets <= 1);
      assert(sloth::hitTestAction(x, y) == expected);
    }
  }
  assert(sloth::hitTestAction(INT32_MIN, INT32_MAX) == -1);
  assert(sloth::hitTestAction(INT32_MAX, INT32_MIN) == -1);
  assert(sloth::hitTestAction(332 / 2, 441 / 2) == 2);
  // All four controls share the same row, with disjoint expanded targets.
  assert(sloth::hitTestAction(428 / 2, 451 / 2) == 3);
  assert(sloth::hitTestAction(214, 228) == 3);
  assert(sloth::hitTestAction(214, 229) == -1);
  assert(sloth::hitTestAction(72, 198) == 0);
  assert(sloth::hitTestAction(73, 198) == 1);
  assert(sloth::hitTestAction(134, 198) == 1);
  assert(sloth::hitTestAction(135, 198) == 2);
  assert(sloth::hitTestAction(196, 198) == 2);
  assert(sloth::hitTestAction(197, 198) == 3);
  sloth::Snapshot pet = {};
  pet.fullness = 78; pet.happiness = 90; pet.energy = 62;
  uint16_t guarded[240 * 240 + 2];
  uint16_t idle[240 * 240];
  guarded[0] = 0x1234; guarded[240 * 240 + 1] = 0x5678;
  sloth::drawPet(NULL, pet, 0, NULL, 0, 3);
  sloth::drawPet(idle, pet, 1, "TEST", 0, 0);
  for (unsigned t = 0; t < 840; t += 70) {
    sloth::drawPet(guarded + 1, pet, 1, "TEST", t, 3);
    assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0x5678);
    // No dance pixels may touch meters, status text, controls, or footer.
    assert(std::memcmp(guarded + 1 + 173 * 240, idle + 173 * 240,
                       (240 - 173) * 240 * sizeof(uint16_t)) == 0);
    assert(std::memcmp(guarded + 1, idle, 173 * 240 * sizeof(uint16_t)) != 0);
  }
  for (unsigned mode = 0; mode <= 3; ++mode) {
    for (unsigned t = 0; t < 6000; t += 171) {
      sloth::drawPet(guarded + 1, pet, mode, "TEST", t, mode);
      assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0x5678);
      // Every animation preserves the clock, battery meter, and title region.
      assert(std::memcmp(guarded + 1, idle, 46 * 240 * sizeof(uint16_t)) == 0);
    }
  }
  pet.sleeping = 1;
  sloth::drawPet(idle, pet, 2, "REST +1 EVERY 3 SEC", 0, 0);
  pet.energy = 63;
  sloth::drawPet(guarded + 1, pet, 2, "REST +1 EVERY 3 SEC", 0, 0);
  assert(std::memcmp(guarded + 1 + 173 * 240, idle + 173 * 240,
                     7 * 240 * sizeof(uint16_t)) != 0);
  pet.fullness = pet.happiness = pet.energy = 255;
  sloth::drawPet(guarded + 1, pet, UINT32_MAX, NULL, UINT32_MAX, 3);
  assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0x5678);

  sloth::Hud hud;
  hud.timeText = "12:59 PM";
  hud.zoneText = "EST";
  hud.batteryPresent = true;
  hud.batteryPercent = 100;
  hud.charging = false;
  hud.externalPower = false;
  sloth::drawPet(idle, pet, 0, "TEST", 0, 0);
  sloth::drawPet(guarded + 1, pet, 0, "TEST", 0, 0, hud);
  unsigned hudChanges = 0;
  for (int y = 0; y < 240; ++y) {
    for (int x = 0; x < 240; ++x) {
      if (idle[y * 240 + x] != guarded[1 + y * 240 + x]) {
        ++hudChanges;
        assert(y >= 14 && y <= 24);
        assert((x >= 14 && x <= 61) || (x >= 171 && x <= 224));
      }
    }
  }
  assert(hudChanges > 0);
  // Clamp bad high readings and cap labels to keep hardware data in its HUD.
  std::memcpy(idle, guarded + 1, sizeof(idle));
  hud.batteryPercent = INT32_MAX;
  hud.timeText = "12:59 PM EXTRA TEXT SHOULD NOT REACH MOSS";
  hud.zoneText = "EST EXTRA TEXT";
  sloth::drawPet(guarded + 1, pet, 0, "TEST", 0, 0, hud);
  assert(std::memcmp(idle, guarded + 1, sizeof(idle)) == 0);
  hud.batteryPercent = 0;
  sloth::drawPet(guarded + 1, pet, 0, "TEST", 0, 0, hud);
  unsigned changedFill = 0;
  for (int y = 17; y < 22; ++y)
    for (int x = 174; x < 191; ++x)
      if (idle[y * 240 + x] != guarded[1 + y * 240 + x]) ++changedFill;
  assert(changedFill == 17 * 5);

  // Power connectivity, rather than the charge-direction flag, controls the bolt.
  sloth::drawPet(idle, pet, 0, "TEST", 0, 0, hud);
  hud.charging = true;
  hud.zoneText = "EDT";
  sloth::drawPet(guarded + 1, pet, 0, "TEST", 0, 0, hud);
  assert(std::memcmp(idle, guarded + 1, sizeof(idle)) == 0);
  hud.charging = false;
  hud.externalPower = true;
  sloth::drawPet(guarded + 1, pet, 0, "TEST", 0, 0, hud);
  unsigned boltChanges = 0;
  for (int y = 0; y < 240; ++y) {
    for (int x = 0; x < 240; ++x) {
      if (idle[y * 240 + x] != guarded[1 + y * 240 + x]) {
        ++boltChanges;
        assert(x >= 178 && x <= 185 && y >= 15 && y <= 24);
      }
    }
  }
  assert(boltChanges > 0);

  // Missing readings show placeholders; null time strings use safe defaults.
  hud = sloth::Hud();
  sloth::drawPet(idle, pet, 0, "TEST", 0, 0, hud);
  hud.timeText = hud.zoneText = NULL;
  hud.batteryPercent = INT32_MIN;
  sloth::drawPet(guarded + 1, pet, 0, "TEST", 0, 0, hud);
  assert(std::memcmp(idle, guarded + 1, sizeof(idle)) == 0);
  // USB with no battery still shows the internal bolt and an unknown percentage.
  hud.externalPower = true;
  sloth::drawPet(guarded + 1, pet, 0, "TEST", 0, 0, hud);
  assert(std::memcmp(idle, guarded + 1, 25 * 240 * sizeof(uint16_t)) != 0);
  assert(std::memcmp(idle + 25 * 240, guarded + 1 + 25 * 240,
                     (240 - 25) * 240 * sizeof(uint16_t)) == 0);
  assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0x5678);

  // Every species and scene keeps its motion within the pet area; the care state
  // is immutable, and neither title/HUD nor meters/controls move during a dance.
  pet = sloth::Snapshot();
  pet.fullness = 78; pet.happiness = 90; pet.energy = 62;
  const sloth::Snapshot originalPet = pet;
  sloth::Settings settings;
  for (unsigned scene = 0; scene < static_cast<unsigned>(sloth::Scene::Count); ++scene) {
    settings.scene = static_cast<sloth::Scene>(scene);
    for (unsigned animal = 0; animal < static_cast<unsigned>(sloth::Animal::Count); ++animal) {
      settings.animal = static_cast<sloth::Animal>(animal);
      sloth::drawPet(idle, pet, 3, "TEST", 0, 0, hud, settings);
      for (unsigned t = 0; t < 840; t += 70) {
        sloth::drawPet(guarded + 1, pet, 3, "TEST", t, 3, hud, settings);
        assert(std::memcmp(idle, guarded + 1, 46 * 240 * sizeof(uint16_t)) == 0);
        assert(std::memcmp(idle + 173 * 240, guarded + 1 + 173 * 240,
                           (240 - 173) * 240 * sizeof(uint16_t)) == 0);
        assert(std::memcmp(idle + 46 * 240, guarded + 1 + 46 * 240,
                           (173 - 46) * 240 * sizeof(uint16_t)) != 0);
        assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0x5678);
      }
      for (int mode = 1; mode <= 2; ++mode) {
        sloth::drawPet(guarded + 1, pet, 3, "TEST", 0, mode, hud, settings);
        assert(std::memcmp(idle + 46 * 240, guarded + 1 + 46 * 240,
                           (173 - 46) * 240 * sizeof(uint16_t)) != 0);
      }
      pet.sleeping = 1;
      sloth::drawPet(guarded + 1, pet, 3, "TEST", 0, 0, hud, settings);
      assert(std::memcmp(idle + 46 * 240, guarded + 1 + 46 * 240,
                         (173 - 46) * 240 * sizeof(uint16_t)) != 0);
      pet.sleeping = 0;
    }
  }
  assert(std::memcmp(&pet, &originalPet, sizeof(pet)) == 0);
  settings = sloth::Settings();
  sloth::drawPet(idle, pet, 3, "TEST", 0, 0, hud, settings);
  std::memcpy(settings.name, "ABCDEFGHIJKL", 13);
  sloth::drawPet(guarded + 1, pet, 3, "TEST", 0, 0, hud, settings);
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 240; ++x)
      if (idle[y * 240 + x] != guarded[1 + y * 240 + x])
        assert(x >= 84 && x <= 155 && y >= 13 && y <= 26);
  std::memcpy(idle, guarded + 1, sizeof(idle));
  settings.showSubtitle = false;
  sloth::drawPet(guarded + 1, pet, 3, "TEST", 0, 0, hud, settings);
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 240; ++x)
      if (idle[y * 240 + x] != guarded[1 + y * 240 + x])
        assert(y >= 29 && y <= 35);
  settings = sloth::Settings();
  sloth::drawPet(idle, pet, 99, "TEST", 0, 0, hud, settings);
  sloth::drawPet(guarded + 1, pet, 3, "TEST", 0, 0, hud, settings);
  unsigned gearChanges = 0;
  for (int y = 0; y < 240; ++y) {
    for (int x = 0; x < 240; ++x) {
      if (idle[y * 240 + x] != guarded[1 + y * 240 + x]) {
        ++gearChanges;
        assert(x >= 200 && x < 228 && y >= 198 && y < 221);
      }
      // A second footer row, including the former SETTINGS text, is absent.
      if (y >= 224) assert(guarded[1 + y * 240 + x] == guarded[1 + y * 240]);
    }
  }
  assert(gearChanges > 0);
  uint32_t scenes[static_cast<unsigned>(sloth::Scene::Count)] = {};
  for (unsigned scene = 0; scene < static_cast<unsigned>(sloth::Scene::Count); ++scene) {
    settings.scene = static_cast<sloth::Scene>(scene);
    sloth::drawPet(idle, pet, 3, "TEST", 0, 0, hud, settings);
    uint32_t fingerprint = 2166136261u;
    for (int y = 43; y < 149; ++y)
      for (int x = 12; x < 229; ++x)
        fingerprint = (fingerprint ^ idle[y * 240 + x]) * 16777619u;
    scenes[scene] = fingerprint;
    for (unsigned previous = 0; previous < scene; ++previous)
      assert(scenes[scene] != scenes[previous]);
  }
  // Every reaction changes the full-size character itself, remains within the
  // pet region, and settles back to the exact ordinary idle frame after 4s.
  // A fixed ambient clock separates gesture movement from ordinary breathing.
  const uint32_t reactionTimes[] = {0, 100, 750, 900, 1700, 2700, 3700, 3999};
  uint16_t fullPose[240 * 240];
  settings = sloth::Settings();
  for (unsigned animal = 0; animal < static_cast<unsigned>(sloth::Animal::Count); ++animal) {
    settings.animal = static_cast<sloth::Animal>(animal);
    sloth::drawPet(idle, pet, 0, "LIFE IN THE SLOW LANE", 1000, 0, hud, settings);
    uint32_t poses[sloth::kPetReactionCount] = {};
    for (int reaction = 0; reaction < sloth::kPetReactionCount; ++reaction) {
      sloth::drawPet(fullPose, pet, 0, "LIFE IN THE SLOW LANE", 1000, 0,
                     hud, settings, nullptr, reaction, 900);
      uint32_t fingerprint = 2166136261u;
      unsigned fullChanges = 0;
      for (int y = 46; y <= 170; ++y) for (int x = 55; x <= 202; ++x) {
        const int pixel = y * 240 + x;
        fingerprint = (fingerprint ^ fullPose[pixel]) * 16777619u;
        if (fullPose[pixel] != idle[pixel]) ++fullChanges;
      }
      assert(fullChanges > 100); // Visible anatomy changes, not only particles.
      poses[reaction] = fingerprint;
      for (int previous = 0; previous < reaction; ++previous)
        assert(poses[previous] != fingerprint);
      for (uint32_t elapsed : reactionTimes) {
        sloth::drawPet(guarded + 1, pet, 0, "LIFE IN THE SLOW LANE", 1000, 0,
                       hud, settings, nullptr, reaction, elapsed);
        assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0x5678);
        assert(std::memcmp(idle, guarded + 1, 46 * 240 * sizeof(uint16_t)) == 0);
        assert(std::memcmp(idle + 171 * 240, guarded + 1 + 171 * 240,
                           (240 - 171) * 240 * sizeof(uint16_t)) == 0);
        if (elapsed == 100)
          assert(std::memcmp(fullPose, guarded + 1, sizeof(fullPose)) != 0);
        if (elapsed == 3999) {
          unsigned settledChanges = 0;
          for (int pixel = 46 * 240; pixel < 171 * 240; ++pixel)
            if (idle[pixel] != guarded[1 + pixel]) ++settledChanges;
          assert(settledChanges < fullChanges);
        }
      }
      const uint32_t finishedTimes[] = {sloth::kPetReactionDurationMs, UINT32_MAX};
      for (uint32_t elapsed : finishedTimes) {
        sloth::drawPet(guarded + 1, pet, 0, "LIFE IN THE SLOW LANE", 1000, 0,
                       hud, settings, nullptr, reaction, elapsed);
        assert(std::memcmp(idle, guarded + 1, sizeof(idle)) == 0);
      }
      pet.sleeping = 1;
      sloth::drawPet(fullPose, pet, 2, "REST +1 EVERY 3 SEC", 1000, 0, hud, settings);
      sloth::drawPet(guarded + 1, pet, 2, "REST +1 EVERY 3 SEC", 1000, 0,
                     hud, settings, nullptr, reaction, 900);
      assert(std::memcmp(fullPose, guarded + 1, sizeof(fullPose)) == 0);
      pet.sleeping = 0;
    }
    const int invalidReactions[] = {INT32_MIN, -1, sloth::kPetReactionCount, INT32_MAX};
    for (int invalid : invalidReactions) {
      sloth::drawPet(guarded + 1, pet, 0, "LIFE IN THE SLOW LANE", 1000, 0,
                     hud, settings, nullptr, invalid, 900);
      assert(std::memcmp(idle, guarded + 1, sizeof(idle)) == 0);
    }
  }
  assert(std::memcmp(&pet, &originalPet, sizeof(pet)) == 0);

  // Status stays truthful and visible above the pet even while it speaks.
  sloth::drawPet(idle, pet, 0, "LIFE IN THE SLOW LANE", 1000, 0, hud, settings, "HELLO!");
  sloth::drawPet(guarded + 1, pet, 0, "REST +1 EVERY 3 SEC", 1000, 0, hud, settings, "HELLO!");
  unsigned statusChanges = 0;
  for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x) {
    if (idle[y * 240 + x] != guarded[1 + y * 240 + x]) {
      ++statusChanges;
      assert(y >= 39 && y <= 45);
    }
  }
  assert(statusChanges > 0);
  std::puts("renderer checks passed: four species, full idle outings, bounded motion, full-size speech, 12 reactions, sleep/settling, status, controls and HUD");
}
