#include "../firmware/sloth_pet/pet_state.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <type_traits>

using sloth::Action;
using sloth::PetState;
using sloth::Response;
using sloth::Snapshot;

static bool equal(const Snapshot& a, const Snapshot& b) {
  return a.fullness == b.fullness && a.happiness == b.happiness &&
      a.energy == b.energy && a.sleeping == b.sleeping &&
      a.fullness_seconds == b.fullness_seconds &&
      a.happiness_seconds == b.happiness_seconds &&
      a.energy_seconds == b.energy_seconds &&
      a.active_seconds == b.active_seconds;
}

static void needs_and_boundaries() {
  PetState pet;
  Snapshot s = pet.snapshot();
  assert(s.fullness == 80 && s.happiness == 80 && s.energy == 75);
  assert(!s.sleeping && s.active_seconds == 0);
  pet.advance(0);
  assert(equal(s, pet.snapshot()));
  pet.advance(179);
  assert(pet.snapshot().fullness == 80 && pet.snapshot().energy == 75);
  pet.advance(1);
  assert(pet.snapshot().fullness == 79 && pet.snapshot().energy == 75);
  assert(pet.snapshot().happiness == 80);
  pet.advance(119);
  assert(pet.snapshot().happiness == 80);
  pet.advance(1);
  assert(pet.snapshot().happiness == 79);
  pet.advance(299);
  assert(pet.snapshot().energy == 75);
  pet.advance(1);
  assert(pet.snapshot().energy == 74);
  pet.advance(3000);
  s = pet.snapshot();
  assert(s.fullness == 60 && s.happiness == 68 && s.energy == 69);
  assert(s.active_seconds == 3600);
}

static void feeding_playing_and_refusals() {
  PetState pet;
  assert(pet.act(Action::Feed) == Response::Fed);
  assert(pet.snapshot().fullness == 98);
  assert(pet.act(Action::Feed) == Response::Fed);
  assert(pet.snapshot().fullness == 100);
  const Snapshot full = pet.snapshot();
  assert(pet.act(Action::Feed) == Response::Full);
  assert(equal(full, pet.snapshot()));
  assert(pet.act(Action::Play) == Response::Played);
  Snapshot s = pet.snapshot();
  assert(s.fullness == 97 && s.happiness == 94 && s.energy == 67);
  assert(pet.act(Action::Play) == Response::Played);
  assert(pet.snapshot().happiness == 100);

  s = pet.snapshot();
  s.fullness = 9;
  s.energy = 50;
  assert(pet.restore(s));
  assert(pet.act(Action::Play) == Response::TooHungry);
  assert(equal(s, pet.snapshot()));
  s.fullness = 10;
  s.energy = 11;
  assert(pet.restore(s));
  assert(pet.act(Action::Play) == Response::TooTired);
  assert(equal(s, pet.snapshot()));
  s.energy = 12;
  assert(pet.restore(s));
  assert(pet.act(Action::Play) == Response::Played);
  assert(pet.snapshot().energy == 4 && pet.snapshot().fullness == 7);
}

static void naps_and_wakeups() {
  PetState pet;
  pet.advance(590);
  assert(pet.act(Action::Nap) == Response::FellAsleep);
  assert(pet.snapshot().energy_seconds == 0);
  pet.advance(2);
  assert(pet.snapshot().energy == 75);
  pet.advance(1);
  assert(pet.snapshot().energy == 76);
  pet.advance(3 * 100);
  assert(pet.snapshot().energy == 100 && pet.snapshot().sleeping);
  assert(pet.act(Action::Nap) == Response::WokeUp);
  assert(!pet.snapshot().sleeping && pet.snapshot().energy_seconds == 0);
  pet.advance(599);
  assert(pet.snapshot().energy == 100);
  pet.advance(1);
  assert(pet.snapshot().energy == 99);

  assert(pet.act(Action::Nap) == Response::FellAsleep);
  pet.advance(17);
  assert(pet.act(Action::Feed) == Response::Fed);
  assert(!pet.snapshot().sleeping && pet.snapshot().energy_seconds == 0);
  assert(pet.act(Action::Nap) == Response::FellAsleep);
  assert(pet.act(Action::Play) == Response::Played);
  assert(!pet.snapshot().sleeping);
}

static void persistence_validation() {
  static_assert(std::is_pod<Snapshot>::value, "Snapshot must remain POD");
  PetState original;
  original.advance(723);
  original.act(Action::Nap);
  original.advance(13);
  PetState restored;
  assert(restored.restore(original.snapshot()));
  original.advance(1447);
  restored.advance(1447);
  assert(equal(original.snapshot(), restored.snapshot()));

  const Snapshot good = restored.snapshot();
  Snapshot bad = good;
  bad.fullness = 101;
  assert(!restored.restore(bad));
  assert(equal(good, restored.snapshot()));
  bad = good; bad.happiness = 255;
  assert(!restored.restore(bad));
  bad = good; bad.energy = 101;
  assert(!restored.restore(bad));
  bad = good; bad.sleeping = 2;
  assert(!restored.restore(bad));
  bad = good; bad.fullness_seconds = 180;
  assert(!restored.restore(bad));
  bad = good; bad.happiness_seconds = 300;
  assert(!restored.restore(bad));
  bad = good; bad.energy_seconds = 3;
  assert(!restored.restore(bad));
  bad = good; bad.sleeping = 0; bad.energy_seconds = 600;
  assert(!restored.restore(bad));
  assert(equal(good, restored.snapshot()));
}

static void shaking_and_fast_naps() {
  static_assert(static_cast<uint8_t>(Action::Feed) == 0, "Feed ID changed");
  static_assert(static_cast<uint8_t>(Action::Play) == 1, "Play ID changed");
  static_assert(static_cast<uint8_t>(Action::Nap) == 2, "Nap ID changed");
  PetState pet;
  pet.advance(19);
  assert(pet.act(Action::Shake) == Response::Danced);
  Snapshot s = pet.snapshot();
  assert(s.fullness == 88 && s.happiness == 92 && s.energy == 75);
  assert(s.energy_seconds == 19 && s.fullness_seconds == 19);
  assert(s.happiness_seconds == 19 && s.active_seconds == 19);
  pet.act(Action::Shake);
  pet.act(Action::Shake);
  assert(pet.snapshot().fullness == 100 && pet.snapshot().happiness == 100);
  pet.act(Action::Nap);
  pet.advance(2);
  const Snapshot napping = pet.snapshot();
  assert(pet.act(Action::Shake) == Response::Sleeping);
  assert(equal(napping, pet.snapshot()));
  assert(pet.act(Action::Nap) == Response::WokeUp);
  assert(!pet.snapshot().sleeping && pet.snapshot().energy_seconds == 0);
  assert(pet.snapshot().energy == 75);

  // Shake works even when the pet is hungry and exhausted.
  s = pet.snapshot();
  s.fullness = 0; s.happiness = 0; s.energy = 0;
  assert(pet.restore(s));
  assert(pet.act(Action::Shake) == Response::Danced);
  assert(pet.snapshot().fullness == 8 && pet.snapshot().happiness == 12);
  assert(pet.snapshot().energy == 0);
  pet.act(Action::Nap);
  pet.advance(60);
  assert(pet.snapshot().energy == 20);
  pet.advance(239);
  assert(pet.snapshot().energy == 99 && pet.snapshot().energy_seconds == 2);
  pet.advance(1);
  assert(pet.snapshot().energy == 100 && pet.snapshot().energy_seconds == 0);
  pet.advance(5);
  assert(pet.snapshot().energy == 100 && pet.snapshot().energy_seconds == 2);
  pet.act(Action::Nap);
  assert(pet.snapshot().energy_seconds == 0);
  pet.advance(599);
  assert(pet.snapshot().energy == 100);
  pet.advance(1);
  assert(pet.snapshot().energy == 99);
}

static void sleeping_shake_preserves_needs_and_clocks() {
  PetState pet;
  pet.advance(179);
  pet.act(Action::Nap);
  pet.advance(2);
  const Snapshot before = pet.snapshot();
  assert(before.sleeping && before.fullness < 100 && before.happiness < 100);
  assert(before.energy_seconds == 2);
  for (unsigned attempt = 0; attempt < 5; ++attempt) {
    assert(pet.act(Action::Shake) == Response::Sleeping);
    assert(equal(before, pet.snapshot()));
  }
  pet.advance(1);
  assert(pet.snapshot().sleeping && pet.snapshot().energy == before.energy + 1);
  assert(pet.snapshot().energy_seconds == 0);
  assert(strcmp(sloth::message(Response::Sleeping), "Shh... still napping.") == 0);
}

static void huge_intervals_and_batching() {
  for (int sleeping = 0; sleeping != 2; ++sleeping) {
    PetState one;
    PetState split;
    if (sleeping) { one.act(Action::Nap); split.act(Action::Nap); }
    one.advance(817);
    split.advance(817);
    one.advance(UINT32_MAX);
    split.advance(UINT32_MAX - 31);
    split.advance(31);
    assert(equal(one.snapshot(), split.snapshot()));
    assert(one.snapshot().active_seconds == UINT32_MAX);
    assert(one.snapshot().fullness == 0 && one.snapshot().happiness == 0);
    assert(one.snapshot().energy == (sleeping ? 100 : 0));
    one.advance(777);
    split.advance(700);
    split.advance(77);
    assert(equal(one.snapshot(), split.snapshot()));
    // An empty pet is recoverable without deleting its save.
    assert(one.act(Action::Feed) == Response::Fed);
    if (!sleeping) {
      one.act(Action::Nap);
      one.advance(360);
    }
    assert(one.act(Action::Play) == Response::Played);
  }

  PetState one;
  PetState per_second;
  one.advance(18737);
  for (uint32_t i = 0; i < 18737; ++i) per_second.advance(1);
  assert(equal(one.snapshot(), per_second.snapshot()));
}

int main() {
  needs_and_boundaries();
  feeding_playing_and_refusals();
  naps_and_wakeups();
  shaking_and_fast_naps();
  sleeping_shake_preserves_needs_and_clocks();
  persistence_validation();
  huge_intervals_and_batching();
  assert(strcmp(sloth::actionName(Action::Feed), "Feed") == 0);
  assert(strlen(sloth::message(Response::TooTired)) > 0);
  assert(strcmp(sloth::actionName(Action::Shake), "Shake") == 0);
  assert(strlen(sloth::message(Response::Danced)) > 0);
  puts("pet_state: all tests passed");
}
