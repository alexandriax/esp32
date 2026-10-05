#include "local_time.h"
#include "rtc_calendar.h"

#include <stdio.h>

namespace sloth {
namespace {
enum class Rule : uint8_t { Fixed, US, Europe, Sydney, Auckland };
struct Zone {
  const char* label;
  int16_t standardMinutes;
  const char* standardName;
  const char* daylightName;
  Rule rule;
};

// IDs are persisted: append new zones rather than reordering this table.
const Zone zones[] = {
  {"US Eastern", -300, "EST", "EDT", Rule::US},
  {"US Central", -360, "CST", "CDT", Rule::US},
  {"US Mountain", -420, "MST", "MDT", Rule::US}, // Denver; Phoenix is separate.
  {"US Pacific", -480, "PST", "PDT", Rule::US},
  {"UTC", 0, "UTC", "UTC", Rule::Fixed},
  {"London", 0, "GMT", "BST", Rule::Europe},
  {"Paris", 60, "CET", "CEST", Rule::Europe},
  {"India", 330, "IST", "IST", Rule::Fixed},
  {"Tokyo", 540, "JST", "JST", Rule::Fixed},
  {"Singapore", 480, "SGT", "SGT", Rule::Fixed},
  {"Sydney", 600, "AEST", "AEDT", Rule::Sydney},
  {"Auckland", 720, "NZST", "NZDT", Rule::Auckland},
  {"Phoenix", -420, "MST", "MST", Rule::Fixed},
  {"Hawaii", -600, "HST", "HST", Rule::Fixed},
};

// Nth Sunday at the specified clock time; nth=0 means the last Sunday. The
// offset explicitly identifies whether that rule is in UTC, standard, or local
// daylight time. Signed math handles southern-hemisphere prior-UTC-day changes.
int64_t transition(uint16_t year, uint8_t month, unsigned nth, unsigned hour,
                   int minutesBefore) {
  pet_rtc::calendar::Date date{year, month, 1, 0, 0, 0};
  uint32_t midnight = 0;
  pet_rtc::calendar::toUtc(date, midnight);
  const unsigned weekday = (midnight / 86400u + 4u) % 7u;
  unsigned day = 1u + (7u - weekday) % 7u;
  if (nth) day += (nth - 1) * 7u;
  else while (day + 7 <= pet_rtc::calendar::monthDays(year, month)) day += 7;
  return static_cast<int64_t>(midnight) + (day - 1) * 86400u + hour * 3600u -
         static_cast<int64_t>(minutesBefore) * 60;
}

// Rules verified against IANA tzdb source, 2026-10-02:
// https://github.com/eggert/tz/blob/main/northamerica (US rules)
// https://github.com/eggert/tz/blob/main/europe (EU rules, London and Paris)
// https://github.com/eggert/tz/blob/main/australasia (AN and NZ rules)
// https://github.com/eggert/tz/blob/main/asia (fixed India, Japan, Singapore)
bool daylight(uint32_t utc, uint16_t year, const Zone& zone) {
  int64_t start = 0, end = 0;
  switch (zone.rule) {
    case Rule::Fixed: return false;
    case Rule::US: {
      const bool modern = year >= 2007;
      start = transition(year, modern ? 3 : 4, modern ? 2 : 1, 2, zone.standardMinutes);
      end = transition(year, modern ? 11 : 10, modern ? 1 : 0, 2, zone.standardMinutes + 60);
      return utc >= start && utc < end;
    }
    case Rule::Europe:
      start = transition(year, 3, 0, 1, 0);
      end = transition(year, 10, 0, 1, 0);
      return utc >= start && utc < end;
    case Rule::Sydney:
      // Olympic Games in 2000 moved the start to August. The 2006 end was
      // delayed to April; from 2008, first Sunday April/October is the rule.
      start = transition(year, year == 2000 ? 8 : 10, year >= 2008 ? 1 : 0,
                         2, zone.standardMinutes);
      end = transition(year, year == 2006 || year >= 2008 ? 4 : 3,
                       year == 2006 || year >= 2008 ? 1 : 0, 2, zone.standardMinutes);
      return utc < end || utc >= start;
    case Rule::Auckland:
      start = transition(year, year >= 2007 ? 9 : 10, year >= 2007 ? 0 : 1,
                         2, zone.standardMinutes);
      end = transition(year, year >= 2008 ? 4 : 3, year >= 2008 ? 1 : 3,
                       2, zone.standardMinutes);
      return utc < end || utc >= start;
  }
  return false;
}
}  // namespace

uint8_t zoneCount() { return static_cast<uint8_t>(sizeof(zones) / sizeof(zones[0])); }

const char* zoneLabel(uint8_t zone) {
  return zone < zoneCount() ? zones[zone].label : "Unknown";
}

uint8_t nextZone(uint8_t zone) {
  return zone < zoneCount() ? static_cast<uint8_t>((zone + 1) % zoneCount()) : 0;
}

ClockTime formatLocalTime(uint32_t utc, uint8_t zone) {
  ClockTime result{"--:--", "--"};
  if (zone >= zoneCount()) return result;
  const Zone& selected = zones[zone];
  pet_rtc::calendar::Date date{};
  if (!pet_rtc::calendar::fromUtc(utc, date)) return result;
  const bool summer = daylight(utc, date.year, selected);
  const int offset = selected.standardMinutes + (summer ? 60 : 0);
  const int64_t local = static_cast<int64_t>(utc) + offset * 60;
  const unsigned hour = static_cast<unsigned>((local / 3600) % 24);
  const unsigned minute = static_cast<unsigned>((local / 60) % 60);
  snprintf(result.text, sizeof(result.text), "%u:%02u %s",
           hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
  result.zone = summer ? selected.daylightName : selected.standardName;
  return result;
}

}  // namespace sloth
