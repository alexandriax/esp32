#include "../firmware/sloth_pet/pet_state.h"
#include "../firmware/sloth_pet/pet_timeline.h"
#include "../firmware/sloth_pet/pet_record.h"

#include <assert.h>
#include <stdio.h>

namespace {
const uint32_t kEpoch = 1790899200u;

bool equal(const sloth::Snapshot& a, const sloth::Snapshot& b) {
  return a.fullness == b.fullness && a.happiness == b.happiness &&
      a.energy == b.energy && a.sleeping == b.sleeping &&
      a.fullness_seconds == b.fullness_seconds &&
      a.happiness_seconds == b.happiness_seconds &&
      a.energy_seconds == b.energy_seconds && a.active_seconds == b.active_seconds;
}

// Composition harness follows the firmware's public contracts, with explicit
// clock inputs so save/reboot behavior can be verified without USB or hardware.
struct Session {
  sloth::PetState pet;
  sloth::PetTimeline timeline;

  void boot(const uint8_t* record, size_t length, uint32_t monotonicMs,
            bool rtcValid, uint32_t rtcUtc) {
    sloth::Snapshot saved{};
    uint32_t savedUtc = 0;
    if (sloth::decodeRecord(record, length, saved, savedUtc)) assert(pet.restore(saved));
    const uint32_t trustedUtc = savedUtc && rtcValid ? rtcUtc : 0;
    pet.advance(timeline.begin(monotonicMs, savedUtc, trustedUtc));
  }

  void advance(uint32_t now) { pet.advance(timeline.advance(now)); }

  sloth::Response act(sloth::Action action, uint32_t now) {
    advance(now);
    return pet.act(action);
  }

  void sync(uint32_t now, uint32_t utc) {
    advance(now);
    timeline.anchorUtc(utc);
  }

  void save(uint32_t now, uint8_t* record) {
    advance(now);
    sloth::encodeRecord(pet.snapshot(), timeline.savedUtc(), record);
  }
};

void pairedSave(const Session& session, const uint8_t* record) {
  sloth::Snapshot decoded{};
  uint32_t utc = 0;
  assert(sloth::decodeRecord(record, sloth::kRecordSize, decoded, utc));
  assert(equal(decoded, session.pet.snapshot()));
  assert(utc == session.timeline.savedUtc());
}

void coldBootAndExactlyOnceCatchup() {
  uint8_t record[sloth::kRecordSize];
  Session initial;
  initial.boot(NULL, 0, 900, true, kEpoch - 86400);
  assert(initial.timeline.savedUtc() == 0); // A plausible factory RTC is untrusted.
  assert(initial.pet.snapshot().active_seconds == 0);
  initial.sync(900, kEpoch);
  initial.save(900, record);
  pairedSave(initial, record);

  Session firstReturn;
  firstReturn.boot(record, sizeof(record), 123, true, kEpoch + 3600);
  const auto first = firstReturn.pet.snapshot();
  assert(first.fullness == 60 && first.happiness == 68 && first.energy == 69);
  assert(first.active_seconds == 3600);
  firstReturn.save(123, record); // Commit the catch-up and matching UTC together.
  pairedSave(firstReturn, record);

  Session immediateReboot;
  immediateReboot.boot(record, sizeof(record), 700, true, kEpoch + 3600);
  assert(equal(immediateReboot.pet.snapshot(), first));
  immediateReboot.save(700, record);
  Session secondReturn;
  secondReturn.boot(record, sizeof(record), 250, true, kEpoch + 7200);
  const auto second = secondReturn.pet.snapshot();
  assert(second.fullness == 40 && second.happiness == 56 && second.energy == 63);
  assert(second.active_seconds == 7200);
}

void napOfflineCareAndReboot() {
  uint8_t record[sloth::kRecordSize];
  Session session;
  session.boot(NULL, 0, 0, false, 0);
  session.sync(0, kEpoch);
  assert(session.act(sloth::Action::Nap, 120000) == sloth::Response::FellAsleep);
  session.save(130000, record);
  assert(session.pet.snapshot().energy == 78);
  assert(session.pet.snapshot().energy_seconds == 1);
  pairedSave(session, record);
  const auto beforeShake = session.pet.snapshot();
  assert(session.act(sloth::Action::Shake, 130000) == sloth::Response::Sleeping);
  assert(equal(session.pet.snapshot(), beforeShake));
  session.save(130000, record);
  pairedSave(session, record);

  Session returned;
  returned.boot(record, sizeof(record), 500, true, kEpoch + 490);
  auto s = returned.pet.snapshot();
  assert(s.sleeping && s.fullness == 78 && s.happiness == 79 && s.energy == 100);
  assert(s.fullness_seconds == 130 && s.happiness_seconds == 190);
  assert(s.active_seconds == 490 && s.energy_seconds == 1);
  assert(returned.act(sloth::Action::Shake, 500) == sloth::Response::Sleeping);
  assert(equal(returned.pet.snapshot(), s));
  assert(returned.act(sloth::Action::Feed, 500) == sloth::Response::Fed);
  returned.save(500, record);
  s = returned.pet.snapshot();
  assert(!s.sleeping && s.fullness == 96 && s.energy_seconds == 0);
  pairedSave(returned, record);
  Session sameSecond;
  sameSecond.boot(record, sizeof(record), 200, true, kEpoch + 490);
  assert(equal(sameSecond.pet.snapshot(), s));
  sameSecond.save(120200, record);
  s = sameSecond.pet.snapshot();
  assert(s.fullness == 95 && s.happiness == 78 && s.energy == 100);
  assert(s.active_seconds == 610 && sameSecond.timeline.savedUtc() == kEpoch + 610);
  pairedSave(sameSecond, record);
}

void invalidRtcAndClockCorrections() {
  uint8_t record[sloth::kRecordSize];
  Session initial;
  initial.boot(NULL, 0, 0, false, 0);
  initial.sync(0, kEpoch);
  initial.save(0, record);
  Session unknown;
  unknown.boot(record, sizeof(record), 100, false, kEpoch + 1000000);
  assert(unknown.pet.snapshot().active_seconds == 0 && unknown.timeline.savedUtc() == 0);
  unknown.act(sloth::Action::Feed, 180100);
  const auto cared = unknown.pet.snapshot();
  assert(cared.fullness == 97 && cared.active_seconds == 180);
  unknown.sync(180100, kEpoch + 1000000);
  assert(equal(unknown.pet.snapshot(), cared));
  unknown.sync(180100, kEpoch - 600);
  assert(equal(unknown.pet.snapshot(), cared));
  unknown.save(180100, record);
  pairedSave(unknown, record);
  Session backward;
  backward.boot(record, sizeof(record), 0, true, kEpoch - 900);
  assert(equal(backward.pet.snapshot(), cared)); // No unsigned underflow penalty.
  backward.save(60000, record);
  assert(backward.pet.snapshot().active_seconds == 240);
  assert(backward.timeline.savedUtc() == kEpoch - 840);
  pairedSave(backward, record);
}

void legacyAuthorityAndLiveFractionalCalls() {
  const uint8_t legacy[24] = {
    0x53,0x4c,0x54,0x48,0x01,0x00,0x50,0x51,0x52,0x00,0xff,0x01,
    0x83,0x03,0x57,0x02,0x78,0x56,0x34,0x12,0x56,0xe8,0x6e,0x8e
  };
  Session migrated;
  migrated.boot(legacy, sizeof(legacy), 0, true, kEpoch);
  const auto before = migrated.pet.snapshot();
  assert(before.fullness == 80 && before.happiness == 81 && before.energy == 82);
  assert(before.fullness_seconds == 153 && before.happiness_seconds == 299);
  assert(migrated.timeline.savedUtc() == 0);
  migrated.sync(0, kEpoch);
  migrated.advance(900);
  migrated.act(sloth::Action::Shake, 900); // Action and save at the same instant.
  uint8_t record[sloth::kRecordSize];
  migrated.save(900, record);
  assert(migrated.pet.snapshot().active_seconds == before.active_seconds);
  migrated.advance(1000);
  auto s = migrated.pet.snapshot();
  assert(s.active_seconds == before.active_seconds + 1);
  assert(s.fullness == 88 && s.happiness == 92 && s.energy == 81);
  migrated.save(1000, record);
  pairedSave(migrated, record);
  Session next;
  next.boot(record, sizeof(record), 3500, true, kEpoch + 1);
  assert(equal(next.pet.snapshot(), s));
}
}  // namespace

int main() {
  coldBootAndExactlyOnceCatchup();
  napOfflineCareAndReboot();
  invalidRtcAndClockCorrections();
  legacyAuthorityAndLiveFractionalCalls();
  puts("elapsed_care: cold boot, offline care, nap, clock correction, migration and paired saves passed");
}
