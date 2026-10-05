#include "../firmware/sloth_pet/display_idle.h"
#include "../firmware/sloth_pet/pet_state.h"

#include <assert.h>
#include <stdio.h>

using sloth::DisplayIdle;
using sloth::DisplayLevel;

static bool equal(const sloth::Snapshot& a, const sloth::Snapshot& b) {
  return a.fullness == b.fullness && a.happiness == b.happiness &&
      a.energy == b.energy && a.sleeping == b.sleeping &&
      a.fullness_seconds == b.fullness_seconds &&
      a.happiness_seconds == b.happiness_seconds &&
      a.energy_seconds == b.energy_seconds && a.active_seconds == b.active_seconds;
}

static void exactDeadlines() {
  DisplayIdle display;
  assert(display.level() == DisplayLevel::Bright);
  display.begin(100);
  assert(display.update(60099) == DisplayLevel::Bright);
  assert(display.update(60100) == DisplayLevel::Dim);
  assert(display.update(120099) == DisplayLevel::Dim);
  assert(display.update(120100) == DisplayLevel::Off);
  assert(display.update(120101) == DisplayLevel::Off);
  // A delayed loop may go directly from bright to off.
  display.begin(500);
  assert(display.update(120500) == DisplayLevel::Off);
}

static void visibleInteractionsResetTheWholeInterval() {
  DisplayIdle display;
  display.begin(0);
  assert(display.visibleInteraction(59999));
  assert(display.update(60000) == DisplayLevel::Bright);
  assert(display.update(119999) == DisplayLevel::Dim);
  assert(display.visibleInteraction(120000));
  assert(display.level() == DisplayLevel::Bright);
  assert(display.update(179999) == DisplayLevel::Bright);
  assert(display.update(180000) == DisplayLevel::Dim);
  assert(display.update(239999) == DisplayLevel::Dim);
  assert(display.update(240000) == DisplayLevel::Off);
}

static void firstHardwarePressIsWakeOnly() {
  DisplayIdle display;
  display.begin(0);
  assert(!display.hardwarePress(10)); // Visible input performs its normal action.
  assert(display.update(60010) == DisplayLevel::Dim);
  assert(!display.hardwarePress(60011)); // Dim is still visible.
  assert(display.level() == DisplayLevel::Bright);
  assert(display.update(180011) == DisplayLevel::Off);
  assert(display.hardwarePress(200000)); // First press wakes without an action.
  assert(display.level() == DisplayLevel::Bright);
  assert(!display.hardwarePress(200100)); // A subsequent press performs an action.
  assert(display.update(260099) == DisplayLevel::Bright);
  assert(display.update(260100) == DisplayLevel::Dim);
}

static void offIgnoresHiddenInputsAndStaysLatchedAcrossFullWrap() {
  DisplayIdle display;
  display.begin(100);
  assert(display.update(120100) == DisplayLevel::Off);
  const uint32_t hiddenTimes[] = {120101u, 200000u, UINT32_MAX, 0u, 99u, 100u, 120099u};
  for (uint32_t now : hiddenTimes) {
    assert(!display.visibleInteraction(now));
    assert(display.update(now) == DisplayLevel::Off);
    assert(display.level() == DisplayLevel::Off);
  }
  assert(display.hardwarePress(100));
  assert(display.level() == DisplayLevel::Bright);
  assert(display.update(60099) == DisplayLevel::Bright);
  assert(display.update(60100) == DisplayLevel::Dim);
}

static void manualScreenOffAndDiagnostics() {
  DisplayIdle display;
  assert(display.idleMs(500) == 0);
  display.begin(500);
  assert(display.idleMs(725) == 225);
  display.screenOffNow();
  assert(display.level() == DisplayLevel::Off);
  assert(!display.visibleInteraction(800));
  assert(display.idleMs(800) == 300); // Hidden input did not reset the origin.
  assert(display.update(800) == DisplayLevel::Off);
  assert(display.hardwarePress(1000));
  assert(display.level() == DisplayLevel::Bright && display.idleMs(1000) == 0);
  assert(!display.hardwarePress(1100));
  // Even a diagnostic issued before an explicit begin remains latched.
  DisplayIdle early;
  early.screenOffNow();
  assert(early.update(120) == DisplayLevel::Off);
  assert(early.hardwarePress(140));
  assert(early.update(140) == DisplayLevel::Bright);
}

static void deadlinesAcrossRollover() {
  DisplayIdle display;
  const uint32_t start = UINT32_MAX - 30000;
  display.begin(start);
  assert(display.update(start + 59999u) == DisplayLevel::Bright);
  assert(display.update(start + 60000u) == DisplayLevel::Dim);
  assert(display.update(start + 119999u) == DisplayLevel::Dim);
  assert(display.update(start + 120000u) == DisplayLevel::Off);
  const uint32_t wake = UINT32_MAX - 1000;
  assert(display.hardwarePress(wake));
  assert(display.update(wake + 59999u) == DisplayLevel::Bright);
  assert(display.update(wake + 60000u) == DisplayLevel::Dim);
}

static void heldHardwarePreventsTimeoutWithoutRepeatedActions() {
  DisplayIdle display;
  display.begin(0);
  display.update(120000);
  assert(display.hardwarePress(120001));
  for (uint32_t now = 120021; now < 300000; now += 20) {
    // Parent checks the physical level, rather than synthesizing extra presses.
    assert(display.visibleInteraction(now));
    assert(display.update(now) == DisplayLevel::Bright);
  }
  assert(display.update(359980) == DisplayLevel::Bright);
  assert(display.update(360000) == DisplayLevel::Dim);
}

static void wakingScreenDoesNotWakeOrCareForPet() {
  sloth::PetState pet;
  pet.act(sloth::Action::Nap);
  pet.advance(2);
  const auto sleeping = pet.snapshot();
  DisplayIdle display;
  display.begin(0);
  display.update(120000);
  if (!display.hardwarePress(130000)) pet.act(sloth::Action::Feed);
  assert(display.level() == DisplayLevel::Bright);
  assert(equal(sleeping, pet.snapshot()));
  // The next actual press is intentional care and follows ordinary game rules.
  if (!display.hardwarePress(130100)) pet.act(sloth::Action::Feed);
  assert(!pet.snapshot().sleeping);
  assert(pet.snapshot().fullness == sleeping.fullness + 18);
}

static void shortPowerTapTogglesWithoutCare() {
  sloth::PetState pet;
  pet.act(sloth::Action::Nap);
  const auto before = pet.snapshot();
  DisplayIdle display;
  display.begin(100);
  assert(!display.powerTap(200));
  assert(display.level() == DisplayLevel::Off);
  assert(!display.visibleInteraction(250)); // Held KEY cannot undo power-off.
  assert(display.powerTap(300));
  assert(display.level() == DisplayLevel::Bright && display.idleMs(300) == 0);
  assert(display.update(60300) == DisplayLevel::Dim);
  assert(!display.powerTap(60301)); // Dim also toggles directly off.
  assert(display.level() == DisplayLevel::Off);
  assert(display.powerTap(UINT32_MAX - 10));
  assert(!display.powerTap(20));
  assert(display.level() == DisplayLevel::Off);
  assert(equal(before, pet.snapshot()));
}

int main() {
  exactDeadlines();
  visibleInteractionsResetTheWholeInterval();
  firstHardwarePressIsWakeOnly();
  offIgnoresHiddenInputsAndStaysLatchedAcrossFullWrap();
  manualScreenOffAndDiagnostics();
  deadlinesAcrossRollover();
  heldHardwarePreventsTimeoutWithoutRepeatedActions();
  wakingScreenDoesNotWakeOrCareForPet();
  shortPowerTapTogglesWithoutCare();
  puts("display_idle: idle deadlines, hardware wake, hidden inputs, held buttons and rollover passed");
}
