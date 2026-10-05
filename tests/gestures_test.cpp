#include "../firmware/sloth_pet/gestures.h"
#include <cassert>
#include <cstdio>
#include <limits>

static void touchTests() {
  sloth::TouchTap tap;
  assert(tap.update(true, 1, 100, 209, 100) == -1);
  assert(tap.update(true, 1, 102, 208, 130) == -1);
  assert(tap.update(false, -1, 0, 0, 160) == -1);
  assert(tap.update(false, -1, 0, 0, 220) == 1);
  assert(tap.update(false, -1, 0, 0, 300) == -1);
  // Held contact and an interrupted release never fire repeatedly.
  for (uint32_t t = 400; t < 1900; t += 20)
    assert(tap.update(true, 2, 175, 210, t) == -1);
  tap.update(false, -1, 0, 0, 1900);
  assert(tap.update(false, -1, 0, 0, 1960) == -1);
  tap.update(true, 0, 40, 210, 2000);
  tap.update(false, -1, 0, 0, 2020);
  tap.update(true, 0, 40, 210, 2040);
  tap.update(false, -1, 0, 0, 2060);
  assert(tap.update(false, -1, 0, 0, 2120) == 0);
  // Drags from outside, between buttons, or too far within a button cancel.
  for (int kind = 0; kind < 3; ++kind) {
    uint32_t t = 3000 + kind * 500;
    tap.update(true, kind == 0 ? -1 : 0, 40, 210, t);
    tap.update(true, kind == 1 ? 1 : 0, kind == 2 ? 70 : 45, 210, t + 50);
    tap.update(false, -1, 0, 0, t + 80);
    assert(tap.update(false, -1, 0, 0, t + 140) == -1);
  }
  tap.update(true, 2, 180, 210, UINT32_MAX - 40);
  tap.update(false, -1, 0, 0, 10);
  assert(tap.update(false, -1, 0, 0, 70) == 2);
}

static void shakeTests(uint32_t start = 0) {
  sloth::ShakeGesture shake;
  uint32_t t = start;
  auto quiet = [&](unsigned milliseconds) {
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 20, t += 20)
      assert(!shake.sample(0, 0, 1, t));
  };
  auto wave = [&](unsigned milliseconds) {
    unsigned rewards = 0;
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 20, t += 20) {
      // Three deliberate oscillations per second, with a realistic gravity term.
      const float x = 1.7f * std::sin(elapsed * 0.01884956f);
      if (shake.sample(x, 0, 1, t)) ++rewards;
    }
    return rewards;
  };
  quiet(1000);
  assert(wave(1800) == 1);
  assert(wave(5000) == 0); // Continuous shaking must first settle.
  quiet(1000);
  assert(wave(1000) == 1);
  quiet(1000);
  assert(wave(1000) == 0); // Still inside the four-second cooldown.
  quiet(4000);
  assert(wave(1000) == 1);
  assert(!shake.sample(std::numeric_limits<float>::quiet_NaN(), 0, 0, t));
  // Ordinary gradual orientation change and a single desk bump never reward.
  sloth::ShakeGesture tilt;
  for (uint32_t time = 0; time < 5000; time += 20) {
    const float angle = time * 0.0004f;
    assert(!tilt.sample(std::sin(angle), 0, std::cos(angle), time));
  }
  sloth::ShakeGesture bump;
  for (uint32_t time = 0; time < 2000; time += 20)
    assert(!bump.sample(time == 1000 ? 2.0f : 0.0f, 0, 1, time));
  sloth::ShakeGesture wrapped;
  for (uint32_t time = 0; time < 2000; time += 20)
    assert(!wrapped.sample(0, 1, 0, UINT32_MAX - 1000 + time));
  // A long sampling gap reinitializes gravity instead of fabricating a shake.
  assert(!wrapped.sample(3, 0, 0, 9000));
}

static void sensitivityAndOrientationTests() {
  // Gravity and the direction of movement rotate together. Moderate deliberate
  // shaking still works upright, sideways, upside down, and at oblique angles.
  const float orientations[][6] = {
    {0, 0, 1, 1, 0, 0},
    {0, 1, 0, 0, 0, 1},
    {0, 0, -1, 0, -1, 0},
    {0.6f, 0.8f, 0, -0.8f, 0.6f, 0},
    {0.57735027f, 0.57735027f, 0.57735027f, 0.70710678f, -0.70710678f, 0}
  };
  const float amplitudes[] = {0.5f, 1.05f, 1.7f};
  for (const auto& orientation : orientations) {
    for (float amplitude : amplitudes) {
      sloth::ShakeGesture shake;
      shake.setSensitivity(1); // Low retains the previous 1.2-g threshold.
      for (uint32_t t = 0; t < 1000; t += 20)
        assert(!shake.sample(orientation[0], orientation[1], orientation[2], t));
      unsigned rewards = 0;
      for (uint32_t elapsed = 0; elapsed < 1800; elapsed += 20) {
        const float motion = amplitude * std::sin(elapsed * 0.01884956f);
        if (shake.sample(orientation[0] + orientation[3] * motion,
                         orientation[1] + orientation[4] * motion,
                         orientation[2] + orientation[5] * motion, 1000 + elapsed)) {
          ++rewards;
        }
      }
      // The previous 0.85-g detector rewarded the 1.05-g wave. It is now ignored.
      assert(rewards == (amplitude == 1.7f ? 1u : 0u));
    }
  }
}

static void deliberatePeakSequenceTests() {
  sloth::ShakeGesture deliberate;
  for (uint32_t t = 0; t <= 1240; t += 20) {
    const float x = t == 1000 || t == 1240 ? 1.6f : t == 1120 ? -1.6f : 0.0f;
    // Two clear peaks are insufficient; the third completes the gesture.
    assert(deliberate.sample(x, 0, 1, t) == (t == 1240));
  }
  sloth::ShakeGesture separated;
  for (uint32_t t = 0; t < 3000; t += 20) {
    const float x = t == 1000 || t == 2400 ? 1.6f : t == 1700 ? -1.6f : 0.0f;
    // Three isolated bumps spread over more than 1.2 seconds never accumulate.
    assert(!separated.sample(x, 0, 1, t));
  }
  sloth::ShakeGesture jitter;
  for (uint32_t t = 0; t < 1800; t += 20) {
    const float x = t == 1000 || t == 1080 ? 1.6f : t == 1040 ? -1.6f : 0.0f;
    // Rapid ringing from one bump must not count as three separate peaks.
    assert(!jitter.sample(x, 0, 1, t));
  }
  sloth::ShakeGesture interrupted;
  for (uint32_t t = 0; t <= 1120; t += 20) {
    const float x = t == 1000 ? 1.6f : t == 1120 ? -1.6f : 0.0f;
    assert(!interrupted.sample(x, 0, 1, t));
  }
  // Losing sensor updates cancels partial gestures, including across orientation.
  assert(!interrupted.sample(0, 1, 1.6f, 1500));
  for (uint32_t t = 1520; t < 2500; t += 20)
    assert(!interrupted.sample(0, 1, 0, t));
}

static unsigned waveAtSensitivity(uint8_t sensitivity, float amplitude, bool sideways) {
  sloth::ShakeGesture shake;
  shake.setSensitivity(sensitivity);
  for (uint32_t t = 0; t < 1000; t += 20)
    assert(!shake.sample(0, sideways ? 1.08f : 0, sideways ? 0 : 1.08f, t));
  unsigned triggers = 0;
  for (uint32_t elapsed = 0; elapsed < 1800; elapsed += 20) {
    const float motion = amplitude * std::sin(elapsed * 0.01884956f);
    if (shake.sample(motion, sideways ? 1.08f : 0, sideways ? 0 : 1.08f,
                     1000 + elapsed)) ++triggers;
  }
  assert(shake.telemetry().triggers == triggers);
  return triggers;
}

static void configurableThresholdsAndBiasedRotation() {
  const float thresholds[] = {1.6f, 1.2f, 0.85f, 0.6f, 0.4f};
  for (uint8_t level = 0; level < 5; ++level) {
    sloth::ShakeGesture detector;
    detector.setSensitivity(level);
    assert(detector.sensitivity() == level && detector.threshold() == thresholds[level]);
    for (unsigned sideways = 0; sideways < 2; ++sideways) {
      assert(waveAtSensitivity(level, thresholds[level] * 0.8f, sideways) == 0);
      assert(waveAtSensitivity(level, thresholds[level] * 1.3f, sideways) == 1);
    }
    // Continuous and alternating orientation changes have a constant measured
    // magnitude, including this real board's 1.08-g resting scale. Neither is
    // translation evidence, even at the most sensitive setting.
    for (unsigned kind = 0; kind < 2; ++kind) {
      sloth::ShakeGesture rotation;
      rotation.setSensitivity(level);
      for (uint32_t t = 0; t < 7000; t += 20) {
        const float angle = kind ? 1.3f * std::sin(t * 0.015f) : t * 0.005f;
        assert(!rotation.sample(1.08f * std::sin(angle), 0, 1.08f * std::cos(angle), t));
      }
      assert(rotation.telemetry().triggers == 0);
    }
    sloth::ShakeGesture bump;
    bump.setSensitivity(level);
    for (uint32_t t = 0; t < 2000; t += 20)
      assert(!bump.sample(t == 1000 ? 2.2f : 0, 0, 1.08f, t));
  }
  // A shake that the previous fixed 1.2-g setting rejected works at Normal.
  assert(waveAtSensitivity(1, 1.05f, false) == 0);
  assert(waveAtSensitivity(2, 1.05f, false) == 1);
  assert(waveAtSensitivity(3, 0.75f, false) == 1);
  assert(waveAtSensitivity(4, 0.55f, false) == 1);
}

static void missedZeroCrossingsAndLiveTelemetry() {
  sloth::ShakeGesture shake;
  assert(shake.sensitivity() == 2 && !shake.telemetry().ready);
  for (uint32_t t = 0; t < 1000; t += 20) assert(!shake.sample(0, 0, 1.08f, t));
  assert(shake.telemetry().ready && shake.telemetry().samples == 50);
  // Coarse sampling misses every near-zero point. Vector reversal must still
  // recognize three opposite peaks, without fabricated UI samples or resets.
  assert(!shake.sample(1.2f, 0, 1.08f, 1000));
  assert(shake.telemetry().peaks == 1);
  shake.setSensitivity(2);
  assert(shake.telemetry().peaks == 1); // Reapplying live preview settings is inert.
  assert(!shake.sample(-1.2f, 0, 1.08f, 1120));
  assert(shake.telemetry().peaks == 2);
  assert(shake.sample(1.2f, 0, 1.08f, 1240));
  const auto detected = shake.telemetry();
  assert(detected.samples == 53 && detected.triggers == 1 && detected.lastTrigger == 1240);
  assert(detected.cooldown && !detected.ready && detected.peaks == 0);
  assert(detected.x == 1.2f && detected.y == 0 && detected.z == 1.08f);
  assert(std::fabs(detected.magnitude - std::sqrt(1.2f * 1.2f + 1.08f * 1.08f)) < 0.001f);
  assert(detected.motion > shake.threshold() && detected.peak >= detected.motion);
  shake.resetPeak();
  assert(shake.telemetry().peak == 0 && shake.telemetry().samples == detected.samples);
  assert(shake.telemetry().triggers == 1 && shake.telemetry().cooldown);
  assert(!shake.sample(std::numeric_limits<float>::quiet_NaN(), 0, 1, 1260));
  assert(!shake.sample(0, std::numeric_limits<float>::infinity(), 1, 1260));
  assert(shake.telemetry().samples == detected.samples);
  shake.setSensitivity(4); // A threshold change must not bypass reward cooldown.
  assert(shake.telemetry().cooldown && shake.telemetry().triggers == 1);
  for (uint32_t t = 1260; t <= 5240; t += 20) assert(!shake.sample(0, 0, 1.08f, t));
  assert(shake.telemetry().ready && !shake.telemetry().cooldown);
  shake.setSensitivity(255);
  assert(shake.sensitivity() == 2);
  assert(!shake.sample(0, 1.08f, 0, 6000)); // A long gap requires fresh warmup.
  assert(!shake.telemetry().ready && shake.telemetry().peaks == 0);

  sloth::ShakeGesture tilted;
  assert(!tilted.sample(0.54f, 0, 0.9353074f, 0));
  assert(std::fabs(tilted.telemetry().tiltX - 30.0f) < 0.01f);
  assert(std::fabs(tilted.telemetry().tiltY) < 0.01f);
  assert(std::fabs(tilted.telemetry().magnitude - 1.08f) < 0.001f);
}

int main() {
  touchTests();
  shakeTests();
  shakeTests(UINT32_MAX - 1400); // Recognition and cooldown across millis rollover.
  sensitivityAndOrientationTests();
  deliberatePeakSequenceTests();
  configurableThresholdsAndBiasedRotation();
  missedZeroCrossingsAndLiveTelemetry();
  puts("gestures: all tests passed");
}
