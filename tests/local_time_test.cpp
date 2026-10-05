#include "../firmware/sloth_pet/local_time.h"
#include "../firmware/sloth_pet/rtc_calendar.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

const char* hostZones[] = {
  "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
  "Etc/UTC", "Europe/London", "Europe/Paris", "Asia/Kolkata", "Asia/Tokyo",
  "Asia/Singapore", "Australia/Sydney", "Pacific/Auckland", "America/Phoenix", "Pacific/Honolulu",
};
const char* standardNames[] = {"EST","CST","MST","PST","UTC","GMT","CET","IST","JST","SGT","AEST","NZST","MST","HST"};
const char* daylightNames[] = {"EDT","CDT","MDT","PDT","UTC","BST","CEST","IST","JST","SGT","AEDT","NZDT","MST","HST"};

bool hostSummer(uint32_t utc) {
  const time_t value = utc;
  tm local{};
  assert(localtime_r(&value, &local));
  return local.tm_isdst > 0;
}

void compareHost(uint32_t utc, uint8_t zone) {
  const time_t value = utc;
  tm local{};
  assert(localtime_r(&value, &local));
  char expected[9];
  std::snprintf(expected, sizeof(expected), "%d:%02d %s", local.tm_hour % 12 ? local.tm_hour % 12 : 12,
                local.tm_min, local.tm_hour < 12 ? "AM" : "PM");
  const auto actual = sloth::formatLocalTime(utc, zone);
  if (std::strcmp(actual.text, expected) != 0) {
    std::fprintf(stderr, "%s utc=%u expected=%s actual=%s\n", hostZones[zone], utc, expected, actual.text);
    assert(false);
  }
  assert(std::strcmp(actual.zone, local.tm_isdst > 0 ? daylightNames[zone] : standardNames[zone]) == 0);
}

int main() {
  using namespace pet_rtc::calendar;
  assert(sloth::zoneCount() == sizeof(hostZones) / sizeof(hostZones[0]));
  assert(sloth::nextZone(sloth::zoneCount() - 1) == 0 && sloth::nextZone(255) == 0);
  assert(std::strcmp(sloth::zoneLabel(255), "Unknown") == 0);
  const uint32_t invalid[] = {0, kFirstUtc - 1, kLastUtc + 1, UINT32_MAX};
  for (uint32_t utc : invalid) {
    const auto unavailable = sloth::formatLocalTime(utc, 0);
    assert(std::strcmp(unavailable.text, "--:--") == 0);
    assert(std::strcmp(unavailable.zone, "--") == 0);
  }
  assert(std::strcmp(sloth::formatLocalTime(kFirstUtc, 255).text, "--:--") == 0);

  unsigned transitions = 0;
  for (uint8_t zone = 0; zone < sloth::zoneCount(); ++zone) {
    assert(setenv("TZ", hostZones[zone], 1) == 0);
    tzset();
    compareHost(kFirstUtc, zone); // Negative offsets reach local 1999 correctly.
    compareHost(kLastUtc, zone);  // Positive offsets reach local 2100 correctly.
    bool previousSummer = hostSummer(kFirstUtc);
    uint32_t previous = kFirstUtc;
    for (uint32_t noon = kFirstUtc + 43200; noon <= kLastUtc; noon += 86400) {
      compareHost(noon, zone); // Independent tzdata oracle for every supported day.
      const bool summer = hostSummer(noon);
      if (summer != previousSummer) {
        // Find the exact second of every transition using only the host oracle.
        uint32_t low = previous, high = noon;
        while (high - low > 1) {
          const uint32_t middle = low + (high - low) / 2;
          if (hostSummer(middle) == previousSummer) low = middle;
          else high = middle;
        }
        compareHost(high - 1, zone);
        compareHost(high, zone);
        compareHost(high + 1, zone);
        ++transitions;
      }
      previous = noon;
      previousSummer = summer;
    }
  }
  assert(transitions == 1600); // Eight DST zones, two transitions, 100 years.
  assert(setenv("TZ", "UTC0", 1) == 0);
  tzset();
  const auto india = sloth::formatLocalTime(kFirstUtc + 5 * 60, 7);
  assert(std::strcmp(india.text, "5:35 AM") == 0);
  for (uint8_t zone = 0; zone < sloth::zoneCount(); ++zone) sloth::formatLocalTime(kFirstUtc, zone);
  assert(std::strcmp(std::getenv("TZ"), "UTC0") == 0); // Firmware never mutates global TZ.
  std::puts("Local clock: 14 zones, every day 2000-2099, 1600 exact DST boundaries passed");
}
