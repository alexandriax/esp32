#pragma once
#include <stdint.h>
#include <cmath>

namespace sloth {

// The telemetry object is updated only by real samples, never by rendering.
// Acceleration/motion/peak are g; tilt uses the filtered gravity vector in degrees.
struct ShakeTelemetry {
  float x = 0, y = 0, z = 0, magnitude = 0, motion = 0, peak = 0;
  float tiltX = 0, tiltY = 0;
  uint32_t samples = 0, triggers = 0, lastTrigger = 0;
  unsigned peaks = 0;
  bool ready = false, cooldown = false;
};

// Gravity-compensated, orientation-independent shake detection. Three
// alternating motion peaks in 1.2 seconds make one gesture. Direction reversal
// can rearm a peak even when polling missed the brief near-zero crossing.
// A change in total acceleration distinguishes translation from pure rotation.
// After a reward, require both a four-second cooldown and 300 ms of quiet.
class ShakeGesture {
 public:
  // IDs match Settings: Very Low, Low, Normal, High, Very High. Changing the
  // level clears a partial gesture, but preserves sensor history and cooldown.
  // Reapplying the same level is a no-op, safe during continuous UI updates.
  void setSensitivity(uint8_t sensitivity) {
    const uint8_t valid = sensitivity < 5 ? sensitivity : 2;
    if (valid == sensitivity_) return;
    sensitivity_ = valid;
    peaks_ = 0;
    armed_ = true;
    quiet_ = false;
    settled_ = !rewarded_;
    updateStatus(lastSample_);
  }

  uint8_t sensitivity() const { return sensitivity_; }
  float threshold() const {
    static const float thresholds[] = {1.6f, 1.2f, 0.85f, 0.6f, 0.4f};
    return thresholds[sensitivity_];
  }
  const ShakeTelemetry& telemetry() const { return telemetry_; }
  void resetPeak() { telemetry_.peak = 0; }

  bool sample(float x, float y, float z, uint32_t now) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    const float magnitudeSquared = x * x + y * y + z * z;
    if (!std::isfinite(magnitudeSquared)) return false;
    telemetry_.x = x; telemetry_.y = y; telemetry_.z = z;
    telemetry_.magnitude = std::sqrt(magnitudeSquared);
    if (telemetry_.samples < UINT32_MAX) ++telemetry_.samples;
    if (!initialized_ || now - lastSample_ > 250) {
      gx_ = x; gy_ = y; gz_ = z;
      restMagnitude_ = telemetry_.magnitude;
      initialized_ = true;
      lastSample_ = warmup_ = now;
      peaks_ = 0; armed_ = true; quiet_ = false;
      telemetry_.motion = 0;
      updateTilt();
      updateStatus(now);
      return false;
    }
    const uint32_t dt = now - lastSample_;
    if (!dt) return false;
    lastSample_ = now;
    const float dx = x - gx_, dy = y - gy_, dz = z - gz_;
    const float motion = std::sqrt(dx * dx + dy * dy + dz * dz);
    telemetry_.motion = motion;
    if (motion > telemetry_.peak) telemetry_.peak = motion;
    const float alpha = static_cast<float>(dt) / (350.0f + dt);
    gx_ += alpha * dx; gy_ += alpha * dy; gz_ += alpha * dz;
    updateTilt();
    const float trigger = threshold();
    // Track sensor scale/offset independently of vector filtering: a rotating
    // gravity vector may average toward zero even though its true length does
    // not change. Real boards can rest at 1.08 g, rather than exactly 1.0 g.
    if (now - warmup_ < 500 || motion < trigger * 0.4f) {
      restMagnitude_ += alpha * (telemetry_.magnitude - restMagnitude_);
    }
    if (now - warmup_ < 500) { updateStatus(now); return false; }

    if (motion < trigger * 0.4f) {
      armed_ = true;
      if (!quiet_) { quiet_ = true; quietSince_ = now; }
      if (now - quietSince_ >= 300) settled_ = true;
    } else quiet_ = false;
    if (rewarded_ && (now - lastReward_ < 4000 || !settled_)) {
      updateStatus(now);
      return false;
    }
    if (peaks_ && now - firstPeak_ > 1200) peaks_ = 0;
    // Pure changes in orientation rotate the gravity vector without changing
    // its length. Require a modest magnitude departure, even on Very High.
    const bool translating = std::fabs(telemetry_.magnitude - restMagnitude_) >= trigger * 0.15f;
    const float projection = dx * peakX_ + dy * peakY_ + dz * peakZ_;
    const bool reversed = peaks_ && projection < -0.25f * motion * peakMotion_;
    if (motion > trigger && translating && (armed_ || reversed) &&
        (!peaks_ || now - lastPeak_ >= 100)) {
      armed_ = false;
      if (!peaks_ || !reversed) { peaks_ = 1; firstPeak_ = now; }
      else ++peaks_;
      lastPeak_ = now;
      peakX_ = dx; peakY_ = dy; peakZ_ = dz; peakMotion_ = motion;
      if (peaks_ == 3) {
        peaks_ = 0;
        rewarded_ = true;
        settled_ = false;
        quiet_ = false;
        lastReward_ = telemetry_.lastTrigger = now;
        if (telemetry_.triggers < UINT32_MAX) ++telemetry_.triggers;
        updateStatus(now);
        return true;
      }
    }
    updateStatus(now);
    return false;
  }
 private:
  void updateTilt() {
    const float degrees = 57.2957795f;
    telemetry_.tiltX = std::atan2(gx_, std::sqrt(gy_ * gy_ + gz_ * gz_)) * degrees;
    telemetry_.tiltY = std::atan2(gy_, std::sqrt(gx_ * gx_ + gz_ * gz_)) * degrees;
  }
  void updateStatus(uint32_t now) {
    telemetry_.peaks = peaks_;
    telemetry_.cooldown = rewarded_ && now - lastReward_ < 4000;
    telemetry_.ready = initialized_ && now - warmup_ >= 500 &&
        !telemetry_.cooldown && (!rewarded_ || settled_);
  }

  uint8_t sensitivity_ = 2;
  ShakeTelemetry telemetry_;
  bool initialized_ = false, armed_ = true, rewarded_ = false;
  bool quiet_ = false, settled_ = true;
  float gx_ = 0, gy_ = 0, gz_ = 0;
  float restMagnitude_ = 1;
  float peakX_ = 0, peakY_ = 0, peakZ_ = 0, peakMotion_ = 0;
  unsigned peaks_ = 0;
  uint32_t lastSample_ = 0, warmup_ = 0, firstPeak_ = 0, lastPeak_ = 0;
  uint32_t lastReward_ = 0, quietSince_ = 0;
};

// One action on release after a short tap in the same button. Ignore drags,
// long holds, and taps that began outside the buttons. The caller skips this
// update on an I2C error: an error is never treated as a finger release.
class TouchTap {
 public:
  int update(bool down, int target, int x, int y, uint32_t now) {
    if (down) {
      releasing_ = false;
      if (!tracking_) {
        tracking_ = true;
        candidate_ = target;
        startX_ = x; startY_ = y; started_ = now;
      }
      const int dx = x - startX_, dy = y - startY_;
      if (target != candidate_ || dx * dx + dy * dy > 18 * 18 ||
          now - started_ > 1200) candidate_ = -1;
      return -1;
    }
    if (!tracking_) return -1;
    if (!releasing_) { releasing_ = true; released_ = now; }
    if (now - released_ < 50) return -1;
    const int tapped = now - started_ <= 1250 ? candidate_ : -1;
    tracking_ = releasing_ = false;
    candidate_ = -1;
    return tapped;
  }
 private:
  bool tracking_ = false, releasing_ = false;
  int candidate_ = -1, startX_ = 0, startY_ = 0;
  uint32_t started_ = 0, released_ = 0;
};
}  // namespace sloth
