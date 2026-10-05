#pragma once

#include <stdint.h>

namespace pet_rtc {

enum class Status : uint8_t { Valid, Invalid, Unavailable };

// Uses the existing Wire bus after board::begin(), without changing the clock.
// Valid supplies UTC Unix seconds for 2000..2099; other results leave utc alone.
// Invalid includes oscillator loss, STOP/test mode, and malformed calendar data.
// Unavailable means the I2C transaction failed; it may be retried later.
Status read(uint32_t& utc);

// Set an explicit host-supplied UTC time. Never called implicitly at boot.
// Stops counting while the whole date is written, verifies it, and restarts in
// 24-hour mode. On a failed/partial write the clock can remain stopped/invalid
// until a later successful set, rather than exposing a partially updated date.
bool set(uint32_t utc);

}  // namespace pet_rtc
