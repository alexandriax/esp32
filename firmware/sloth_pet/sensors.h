#pragma once

#include <stdint.h>

namespace sensors {

struct Capabilities {
  bool touch;
  bool acceleration;
};

// Call after board::begin(). Uses its existing Wire bus; initialization failure
// only disables that sensor. The touch reset takes 600 ms during startup.
Capabilities begin();

// Poll at about 50 Hz. Returns true only for a new, complete acceleration sample
// in g, in the sensor's native axes. Does not wait for data to become ready.
// Unavailable/not-ready/failed reads return false and leave all outputs alone.
bool readAcceleration(float& xg, float& yg, float& zg);

// Enable the gyro only for radio sweep guidance. Keeps the accelerometer's
// range and native axes, and returns to accel-only mode on disable. No delay.
bool enableGyroscope(bool enabled);
bool gyroscopeEnabled();
// One burst reads acceleration (g) and angular rate (degrees/s) in matching
// native right-handed axes. Discards five fresh samples after enabling. Failed
// or not-ready reads leave every output unchanged. Use instead of a separate
// acceleration read while the gyro is enabled (STATUS0 acknowledges both).
bool readMotion(float& ax, float& ay, float& az, float& gx, float& gy, float& gz);

enum class TouchStatus : uint8_t { Unavailable, Error, Released, Pressed };

// Poll at about 50 Hz. Pressed supplies coordinates in the 480 x 480 display
// space (not the renderer's 240 x 240 space). Released supplies the last valid
// contact position, or (0,0) before the first contact. Error/Unavailable leave
// x/y alone. Release packet coordinates must never be used as a new position.
// Error is NOT a release: callers should retain their press/debounce state.
TouchStatus readTouch(uint16_t& x, uint16_t& y);

}  // namespace sensors
