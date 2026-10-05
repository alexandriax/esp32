#pragma once

#include <stdint.h>
#include <stdio.h>
#include "rtc_calendar.h"

namespace sloth {

struct EasternTime {
  char text[9]; // Longest display: "12:59 PM" plus terminator.
  const char* zone;
};

namespace eastern_detail {
inline uint32_t transition(uint16_t year, uint8_t month, unsigned sunday,
                           uint8_t utcHour) {
  pet_rtc::calendar::Date date{year, month, 1, utcHour, 0, 0};
  uint32_t utc = 0;
  pet_rtc::calendar::toUtc(date, utc);
  const unsigned weekday = (utc / 86400u + 4u) % 7u; // Sunday=0, epoch Thursday.
  unsigned day = 1u + (7u - weekday) % 7u;
  if (sunday) day += (sunday - 1u) * 7u;
  else {
    const unsigned last = pet_rtc::calendar::monthDays(year, month);
    while (day + 7u <= last) day += 7u;
  }
  return utc + (day - 1u) * 86400u;
}
}  // namespace eastern_detail

// U.S. Eastern time, including the DST jump/repeated hour, without changing the
// UTC clock used for care. Current rules from 2007: second Sunday of March at
// 02:00 EST (07:00 UTC), first Sunday of November at 02:00 EDT (06:00 UTC).
// Earlier supported years use first Sunday of April / last Sunday of October.
// Sources: NIST daylight-saving-time-dst and time-frequency-z-d pages.
inline EasternTime easternTime(uint32_t utc) {
  EasternTime result{"--:--", "ET"};
  pet_rtc::calendar::Date date{};
  if (!pet_rtc::calendar::fromUtc(utc, date)) return result;
  const bool modern = date.year >= 2007;
  const uint32_t start = eastern_detail::transition(date.year, modern ? 3 : 4,
                                                    modern ? 2 : 1, 7);
  const uint32_t end = eastern_detail::transition(date.year, modern ? 11 : 10,
                                                  modern ? 1 : 0, 6);
  const bool daylight = utc >= start && utc < end;
  const uint32_t local = utc - (daylight ? 4u : 5u) * 3600u;
  const unsigned hour = (local / 3600u) % 24u;
  const unsigned minute = (local / 60u) % 60u;
  snprintf(result.text, sizeof(result.text), "%u:%02u %s",
           hour % 12u ? hour % 12u : 12u, minute, hour < 12 ? "AM" : "PM");
  result.zone = daylight ? "EDT" : "EST";
  return result;
}

}  // namespace sloth
