#include "../firmware/sloth_pet/pet_timeline.h"

#include <cassert>
#include <cstdio>

namespace {
const uint32_t epoch = 1790899200u;

void bootCatchUpAndPairedSave() {
  sloth::PetTimeline clock;
  assert(clock.begin(500, epoch, epoch + 3600) == 3600);
  assert(clock.savedUtc() == epoch + 3600);
  assert(clock.advance(5500) == 5);
  assert(clock.savedUtc() == epoch + 3605);
  // Repeated action/save calls at the same time never charge a second twice.
  assert(clock.advance(5500) == 0);
  const uint32_t pairedSave = clock.savedUtc();
  sloth::PetTimeline reboot;
  assert(reboot.begin(42, pairedSave, epoch + 3665) == 60);
  assert(reboot.savedUtc() == epoch + 3665);
  assert(reboot.advance(1042) == 1);
}

void fractionsAcrossActionsAndSync() {
  sloth::PetTimeline clock;
  assert(clock.begin(0, 0, 0) == 0);
  assert(clock.advance(900) == 0);  // Time immediately before an action.
  assert(clock.advance(900) == 0);  // Same-loop save after that action.
  assert(clock.advance(1500) == 1);
  assert(clock.savedUtc() == 0);
  clock.anchorUtc(epoch);
  assert(clock.savedUtc() == epoch);
  assert(clock.advance(1999) == 0); // Synchronizing must not discard 500 ms.
  assert(clock.advance(2000) == 1);
  assert(clock.savedUtc() == epoch + 1);
  assert(clock.advance(2500) == 0);
  clock.anchorUtc(epoch - 300);     // Backward corrections add no gameplay.
  assert(clock.advance(3000) == 1);
  assert(clock.savedUtc() == epoch - 299);
  clock.anchorUtc(epoch + 9000);    // Neither do forward corrections.
  assert(clock.advance(3000) == 0);
  assert(clock.savedUtc() == epoch + 9000);
}

void unknownAndBackwardBootTime() {
  sloth::PetTimeline clock;
  assert(clock.begin(20, epoch, 0) == 0);
  assert(clock.savedUtc() == 0);
  assert(clock.advance(3020) == 3);
  // When time becomes known later, unknown offline time is not charged after
  // the user has already cared for the pet during this session.
  clock.anchorUtc(epoch + 8000);
  assert(clock.advance(3020) == 0);
  assert(clock.savedUtc() == epoch + 8000);
  assert(clock.begin(20, epoch, epoch - 100) == 0);
  assert(clock.savedUtc() == epoch - 100);
  assert(clock.advance(1020) == 1);
  assert(clock.savedUtc() == epoch - 99);
  assert(clock.begin(20, 0, epoch) == 0);
  assert(clock.savedUtc() == epoch);
  assert(clock.begin(20, UINT32_MAX, epoch) == 0);
  assert(clock.savedUtc() == epoch);
}

void utcBoundsAndRevocation() {
  sloth::PetTimeline clock;
  assert(sloth::PetTimeline::validUtc(sloth::PetTimeline::kFirstUtc));
  assert(sloth::PetTimeline::validUtc(sloth::PetTimeline::kEndUtc - 1));
  assert(!sloth::PetTimeline::validUtc(sloth::PetTimeline::kFirstUtc - 1));
  assert(!sloth::PetTimeline::validUtc(sloth::PetTimeline::kEndUtc));
  assert(clock.begin(0, 0, sloth::PetTimeline::kEndUtc) == 0);
  assert(clock.savedUtc() == 0);
  assert(clock.begin(0, sloth::PetTimeline::kFirstUtc,
                      sloth::PetTimeline::kEndUtc - 1) ==
         sloth::PetTimeline::kEndUtc - 1 - sloth::PetTimeline::kFirstUtc);
  assert(clock.advance(1000) == 1);
  assert(clock.savedUtc() == 0);  // Crossing into 2100 cannot wrap/revalidate.
  assert(clock.advance(2000) == 1);
  clock.anchorUtc(epoch);
  assert(clock.advance(2500) == 0);
  clock.anchorUtc(UINT32_MAX);
  assert(clock.savedUtc() == 0);
  assert(clock.advance(3000) == 1); // Revocation preserves the live fraction.
}

void millisWrapAndLargestUnambiguousInterval() {
  sloth::PetTimeline clock;
  clock.begin(UINT32_MAX - 500, 0, epoch);
  assert(clock.advance(UINT32_MAX) == 0);
  assert(clock.advance(499) == 1);
  assert(clock.savedUtc() == epoch + 1);
  assert(clock.advance(999) == 0);
  assert(clock.advance(1499) == 1);

  clock.begin(0, 0, epoch);
  assert(clock.advance(UINT32_MAX) == UINT32_MAX / 1000u);
  assert(clock.advance(0) == 0);
  assert(clock.advance(704) == 1);
  assert(clock.savedUtc() == epoch + UINT32_MAX / 1000u + 1);

  // With an existing 999 ms fraction, the largest delta needs 64-bit addition.
  clock.begin(0, 0, epoch);
  assert(clock.advance(999) == 0);
  assert(clock.advance(998) == 4294968u);
  assert(clock.advance(1704) == 1);
}

void uninitializedClockAndReset() {
  sloth::PetTimeline clock;
  assert(clock.savedUtc() == 0);
  assert(clock.advance(3000) == 0);
  assert(clock.advance(4000) == 1);
  clock.anchorUtc(epoch);
  assert(clock.begin(0, 0, 0) == 0);
  assert(clock.savedUtc() == 0);
  assert(clock.advance(500) == 0);
}
}  // namespace

int main() {
  bootCatchUpAndPairedSave();
  fractionsAcrossActionsAndSync();
  unknownAndBackwardBootTime();
  utcBoundsAndRevocation();
  millisWrapAndLargestUnambiguousInterval();
  uninitializedClockAndReset();
  std::puts("timeline tests passed: boot catch-up, atomic pairing, fractions, sync, UTC bounds, millis rollover");
}
