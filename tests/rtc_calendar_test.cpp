#include "../firmware/sloth_pet/rtc_calendar.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace calendar = pet_rtc::calendar;

void knownDate(calendar::Date date, uint32_t expected) {
  uint32_t utc = 0;
  assert(calendar::toUtc(date, utc) && utc == expected);
  calendar::Date result{};
  assert(calendar::fromUtc(utc, result));
  assert(result.year == date.year && result.month == date.month && result.day == date.day);
  assert(result.hour == date.hour && result.minute == date.minute && result.second == date.second);
}

void invalidPacket(const uint8_t packet[7], uint8_t control = 0) {
  uint32_t untouched = 123;
  assert(!calendar::decode(control, packet, untouched) && untouched == 123);
}

int main() {
  knownDate({2000, 1, 1, 0, 0, 0}, 946684800u);
  knownDate({2000, 2, 29, 0, 0, 0}, 951782400u);
  knownDate({2024, 2, 29, 0, 0, 0}, 1709164800u);
  knownDate({2038, 1, 19, 3, 14, 8}, 2147483648u);
  knownDate({2099, 12, 31, 23, 59, 59}, 4102444799u);

  // Independent UTC oracle for every supported calendar day, including every
  // leap year and dates past signed 32-bit time_t. Never depends on local TZ.
  for (uint32_t utc = calendar::kFirstUtc; utc <= calendar::kLastUtc; utc += 86400u) {
    const time_t hostUtc = static_cast<time_t>(utc);
    const tm* expected = std::gmtime(&hostUtc);
    assert(expected);
    calendar::Date date{};
    assert(calendar::fromUtc(utc, date));
    assert(date.year == expected->tm_year + 1900 && date.month == expected->tm_mon + 1 &&
           date.day == expected->tm_mday && date.hour == 0 && date.minute == 0 && date.second == 0);
    uint8_t packet[7];
    assert(calendar::encode(utc, packet) && packet[4] == expected->tm_wday);
    uint32_t decoded = 0;
    assert(calendar::decode(0, packet, decoded) && decoded == utc);
  }

  uint32_t unchanged = 123;
  assert(!calendar::toUtc({1999, 12, 31, 23, 59, 59}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2100, 1, 1, 0, 0, 0}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2001, 2, 29, 0, 0, 0}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2024, 4, 31, 0, 0, 0}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2024, 0, 1, 0, 0, 0}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2024, 13, 1, 0, 0, 0}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2024, 1, 0, 0, 0, 0}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2024, 1, 1, 24, 0, 0}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2024, 1, 1, 0, 60, 0}, unchanged) && unchanged == 123);
  assert(!calendar::toUtc({2024, 1, 1, 0, 0, 60}, unchanged) && unchanged == 123);
  calendar::Date invalidDate{2024, 1, 2, 3, 4, 5};
  assert(!calendar::fromUtc(calendar::kFirstUtc - 1, invalidDate) && invalidDate.year == 2024);
  assert(!calendar::fromUtc(calendar::kLastUtc + 1, invalidDate) && invalidDate.day == 2);

  uint8_t valid[7];
  assert(calendar::encode(calendar::kFirstUtc, valid));
  invalidPacket(valid, 0x20); // STOP.
  invalidPacket(valid, 0x80); // External test clock.
  invalidPacket(valid, 0x40); // Reserved bit must read zero.
  const uint8_t corruptValues[] = {0x80, 0x6A, 0x24, 0, 7, 0x13, 0xFA};
  for (unsigned i = 0; i < 7; ++i) {
    uint8_t packet[7];
    std::memcpy(packet, valid, sizeof(packet));
    packet[i] = corruptValues[i];
    invalidPacket(packet);
  }
  uint8_t impossible[] = {0, 0, 0, 0x29, 1, 0x02, 0x01};
  invalidPacket(impossible); // BCD is valid but 2001-02-29 is not a real day.

  // Read pre-existing 12-hour clocks without changing their configuration.
  uint32_t decoded;
  valid[2] = 0x12;
  assert(calendar::decode(0x02, valid, decoded) && decoded == calendar::kFirstUtc); // 12 AM.
  valid[2] = 0x32;
  assert(calendar::decode(0x02, valid, decoded) && decoded == calendar::kFirstUtc + 12 * 3600); // 12 PM.
  valid[2] = 0x21;
  assert(calendar::decode(0x02, valid, decoded) && decoded == calendar::kFirstUtc + 13 * 3600); // 1 PM.
  valid[2] = 0;
  invalidPacket(valid, 0x02);
  valid[2] = 0x13;
  invalidPacket(valid, 0x02);
  std::puts("RTC calendar checks passed");
}
