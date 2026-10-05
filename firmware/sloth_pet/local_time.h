#pragma once

#include <stdint.h>

namespace sloth {

struct ClockTime {
  char text[9];  // "12:59 PM" plus terminator, or "--:--" when unavailable.
  const char* zone;
};

// Stable settings IDs: US Eastern, Central, Mountain, Pacific, UTC, London,
// Paris, India, Tokyo, Singapore, Sydney, Auckland, Phoenix, Hawaii.
uint8_t zoneCount();
const char* zoneLabel(uint8_t zone);
uint8_t nextZone(uint8_t zone);

// Convert valid RTC UTC (2000..2099) without changing process-global TZ or the
// UTC clock used for care. Includes historical DST changes within that range;
// future years project currently published rules and require updates if changed.
ClockTime formatLocalTime(uint32_t utc, uint8_t zone);

}  // namespace sloth
