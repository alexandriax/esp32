#pragma once

#include <stdint.h>

namespace pet_rtc {
namespace calendar {

// The chip has a two-digit year and no century register. Firmware consistently
// interprets it as 2000..2099; conversion never uses local time or libc time_t.
constexpr uint32_t kFirstUtc = 946684800u;
constexpr uint32_t kLastUtc = 4102444799u;

struct Date {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
};

inline bool leapYear(uint16_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline uint8_t monthDays(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return days[month - 1] + (month == 2 && leapYear(year) ? 1 : 0);
}

inline bool toUtc(const Date& date, uint32_t& utc) {
  if (date.year < 2000 || date.year > 2099 || date.month < 1 || date.month > 12 ||
      date.day < 1 || date.day > monthDays(date.year, date.month) ||
      date.hour > 23 || date.minute > 59 || date.second > 59) return false;
  uint32_t days = 0;
  for (uint16_t year = 2000; year < date.year; ++year) days += leapYear(year) ? 366 : 365;
  for (uint8_t month = 1; month < date.month; ++month) days += monthDays(date.year, month);
  days += date.day - 1;
  utc = kFirstUtc + days * 86400u + date.hour * 3600u + date.minute * 60u + date.second;
  return true;
}

inline bool fromUtc(uint32_t utc, Date& date) {
  if (utc < kFirstUtc || utc > kLastUtc) return false;
  const uint32_t seconds = utc - kFirstUtc;
  uint32_t days = seconds / 86400u;
  Date result{2000, 1, 1, static_cast<uint8_t>((seconds / 3600u) % 24),
              static_cast<uint8_t>((seconds / 60u) % 60), static_cast<uint8_t>(seconds % 60)};
  while (days >= static_cast<uint32_t>(leapYear(result.year) ? 366 : 365)) {
    days -= leapYear(result.year) ? 366 : 365;
    ++result.year;
  }
  while (days >= monthDays(result.year, result.month)) {
    days -= monthDays(result.year, result.month);
    ++result.month;
  }
  result.day = static_cast<uint8_t>(days + 1);
  date = result;
  return true;
}

inline bool fromBcd(uint8_t bcd, uint8_t& value) {
  if ((bcd & 15) > 9 || (bcd >> 4) > 9) return false;
  value = (bcd >> 4) * 10 + (bcd & 15);
  return true;
}

inline uint8_t toBcd(uint8_t value) {
  return static_cast<uint8_t>((value / 10) << 4) | (value % 10);
}

// PCF85063A seconds..years (04h..0Ah), including OS and 12/24-hour checks.
// See https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf sections 7.2-7.4.
inline bool decode(uint8_t control1, const uint8_t bytes[7], uint32_t& utc) {
  // External test mode and STOP make elapsed wall time unreliable. Reserved
  // bits, invalid BCD, and OS also reject a packet instead of inventing a date.
  if ((control1 & 0xF8) || (bytes[0] & 0x80) || (bytes[1] & 0x80) ||
      (bytes[2] & 0xC0) || (bytes[3] & 0xC0) || bytes[4] > 6 || (bytes[5] & 0xE0)) {
    return false;
  }
  Date date{};
  uint8_t year;
  const bool twelveHour = (control1 & 0x02) != 0;
  if (!fromBcd(bytes[0], date.second) || !fromBcd(bytes[1], date.minute) ||
      !fromBcd(bytes[2] & (twelveHour ? 0x1F : 0x3F), date.hour) ||
      !fromBcd(bytes[3], date.day) || !fromBcd(bytes[5], date.month) ||
      !fromBcd(bytes[6], year)) return false;
  if (twelveHour) {
    if (date.hour < 1 || date.hour > 12) return false;
    date.hour = (date.hour % 12) + ((bytes[2] & 0x20) ? 12 : 0);
  }
  date.year = 2000 + year;
  return toUtc(date, utc);
}

// Encode a complete 24-hour packet, with OS clear and Sunday=0 weekday.
inline bool encode(uint32_t utc, uint8_t bytes[7]) {
  Date date{};
  if (!fromUtc(utc, date)) return false;
  bytes[0] = toBcd(date.second);
  bytes[1] = toBcd(date.minute);
  bytes[2] = toBcd(date.hour);
  bytes[3] = toBcd(date.day);
  bytes[4] = static_cast<uint8_t>(((utc - kFirstUtc) / 86400u + 6) % 7);
  bytes[5] = toBcd(date.month);
  bytes[6] = toBcd(static_cast<uint8_t>(date.year - 2000));
  return true;
}

}  // namespace calendar
}  // namespace pet_rtc
