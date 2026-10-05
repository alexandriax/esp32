#include "rtc_mocks/Wire.h"
#include "../firmware/sloth_pet/pet_rtc.h"
#include "../firmware/sloth_pet/rtc_calendar.h"

#include <cassert>
#include <cstdio>
#include <cstring>

WireMock Wire;
constexpr uint32_t kOldUtc = 1709164800u;
constexpr uint32_t kNewUtc = 2147483648u;

void seedClock() {
  Wire = WireMock{};
  assert(pet_rtc::calendar::encode(kOldUtc, Wire.registers + 4));
}

int main() {
  seedClock();
  uint32_t utc = 123;
  uint8_t old[256];
  std::memcpy(old, Wire.registers, sizeof(old));
  assert(pet_rtc::read(utc) == pet_rtc::Status::Valid && utc == kOldUtc);
  assert(std::memcmp(old, Wire.registers, sizeof(old)) == 0); // Reads never modify RTC.
  Wire.registers[4] |= 0x80;
  assert(pet_rtc::read(utc) == pet_rtc::Status::Invalid && utc == kOldUtc);
  Wire.present = false;
  assert(pet_rtc::read(utc) == pet_rtc::Status::Unavailable && utc == kOldUtc);

  seedClock();
  Wire.registers[0] = 0x86; // Test mode, correction interrupt, 12-hour mode.
  Wire.registers[1] = 0xA5;
  Wire.registers[2] = 0x42;
  Wire.registers[3] = 0x39;
  assert(pet_rtc::set(kNewUtc));
  const int setOperations = Wire.operation;
  assert(Wire.registers[0] == 0x05); // Normal 24h, retain CIE, board CAP_SEL.
  assert(Wire.registers[1] == 0xA5 && Wire.registers[2] == 0x42 && Wire.registers[3] == 0x39);
  assert(pet_rtc::read(utc) == pet_rtc::Status::Valid && utc == kNewUtc);
  assert(Wire.timeout == 100);

  for (int fail = 1; fail <= setOperations; ++fail) {
    seedClock();
    Wire.failAt = fail;
    assert(!pet_rtc::set(kNewUtc));
    assert(Wire.timeout == 100);
    Wire.failAt = 0;
    const auto result = pet_rtc::read(utc);
    // A failed operation must never expose a valid partially updated date.
    assert(result != pet_rtc::Status::Valid || utc == kOldUtc || utc == kNewUtc);
  }

  seedClock();
  Wire.ignoreControlWrites = true;
  assert(!pet_rtc::set(kNewUtc)); // STOP must be confirmed before changing time.
  assert(pet_rtc::read(utc) == pet_rtc::Status::Valid && utc == kOldUtc);
  seedClock();
  Wire.corruptCalendarWrites = true;
  assert(!pet_rtc::set(kNewUtc));
  assert(pet_rtc::read(utc) == pet_rtc::Status::Invalid); // Bad readback stays stopped.
  seedClock();
  Wire.oscillatorStopped = true;
  assert(!pet_rtc::set(kNewUtc));
  assert(pet_rtc::read(utc) == pet_rtc::Status::Invalid); // OS cannot be cleared yet.
  assert(Wire.timeout == 100);

  seedClock();
  assert(!pet_rtc::set(946684799u) && Wire.operation == 0);
  assert(!pet_rtc::set(4102444800u) && Wire.operation == 0);
  std::puts("RTC driver transaction/failure checks passed");
}
