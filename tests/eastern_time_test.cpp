#include "../firmware/sloth_pet/eastern_time.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

uint32_t utc(int year, int month, int day, int hour, int minute, int second) {
  pet_rtc::calendar::Date date{static_cast<uint16_t>(year), static_cast<uint8_t>(month),
      static_cast<uint8_t>(day), static_cast<uint8_t>(hour),
      static_cast<uint8_t>(minute), static_cast<uint8_t>(second)};
  uint32_t value = 0;
  assert(pet_rtc::calendar::toUtc(date, value));
  return value;
}

void check(uint32_t value, const char* text, const char* zone) {
  const auto display = sloth::easternTime(value);
  assert(std::strcmp(display.text, text) == 0);
  assert(std::strcmp(display.zone, zone) == 0);
}

int main() {
  check(0, "--:--", "ET");
  check(946684799u, "--:--", "ET");
  check(4102444800u, "--:--", "ET");
  check(UINT32_MAX, "--:--", "ET");
  check(utc(2000, 1, 1, 0, 0, 0), "7:00 PM", "EST"); // Local year is 1999.
  check(utc(2026, 10, 2, 12, 42, 0), "8:42 AM", "EDT");
  check(utc(2026, 1, 2, 5, 0, 0), "12:00 AM", "EST");
  check(utc(2026, 1, 2, 17, 0, 0), "12:00 PM", "EST");
  check(utc(2026, 3, 8, 6, 59, 59), "1:59 AM", "EST");
  check(utc(2026, 3, 8, 7, 0, 0), "3:00 AM", "EDT");
  check(utc(2026, 11, 1, 5, 59, 59), "1:59 AM", "EDT");
  check(utc(2026, 11, 1, 6, 0, 0), "1:00 AM", "EST");
  check(utc(2006, 4, 2, 6, 59, 59), "1:59 AM", "EST");
  check(utc(2006, 4, 2, 7, 0, 0), "3:00 AM", "EDT");
  check(utc(2006, 10, 29, 6, 0, 0), "1:00 AM", "EST");

  // Independent system tzdata oracle: every day in the RTC's century, at
  // times around both transition hours as well as AM/PM and date boundaries.
  assert(setenv("TZ", "America/New_York", 1) == 0);
  tzset();
  const unsigned hours[] = {0, 5, 6, 7, 12, 23};
  for (uint32_t day = 946684800u; day < 4102444800u; day += 86400u) {
    for (unsigned hour : hours) {
      const time_t value = day + hour * 3600u + 30u * 60u;
      tm eastern{};
      assert(localtime_r(&value, &eastern));
      char expected[9];
      std::snprintf(expected, sizeof(expected), "%d:%02d %s",
          eastern.tm_hour % 12 ? eastern.tm_hour % 12 : 12,
          eastern.tm_min, eastern.tm_hour < 12 ? "AM" : "PM");
      check(static_cast<uint32_t>(value), expected, eastern.tm_isdst ? "EDT" : "EST");
    }
  }
  std::puts("Eastern clock: DST boundaries, noon/midnight, invalid dates and 100 years of tzdata passed");
}
