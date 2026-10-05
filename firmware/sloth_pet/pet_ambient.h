#pragma once

#include <stdint.h>
#include "pet_settings.h"

namespace sloth {

// Pure display choreography: no timers, randomness, allocations or care-state
// writes. Each leisurely outing observes, visits two nearby places, grooms,
// briefly closes its eyes, then returns home before the next cycle.
enum class AmbientActivity : uint8_t { Observe, Travel, Groom, Doze };
struct AmbientPose {
  AmbientActivity activity = AmbientActivity::Observe;
  int x = 0, y = 0, headX = 0, headY = 0, tilt = 0, gaze = 0;
  int step = 0, wing = 0, travel = 0, groom = 0, tail = 0;
  bool closed = false;
};

inline uint32_t ambientCycleMs(Animal animal) {
  return animal == Animal::Sloth ? 72000u : animal == Animal::Cat ? 48000u :
      animal == Animal::Frog ? 42000u : 36000u;
}

namespace ambient_detail {
inline int triangle(uint32_t ms, uint32_t period, int amplitude) {
  const uint32_t step = ms % period;
  return step < period / 2
      ? -amplitude + static_cast<int>(step * 4 * amplitude / period)
      : 3 * amplitude - static_cast<int>(step * 4 * amplitude / period);
}
inline int interpolate(int from, int to, uint32_t elapsed, uint32_t duration) {
  const int64_t t = static_cast<int64_t>(elapsed) * 1024 / duration;
  const int eased = static_cast<int>(t * t * (3072 - 2 * t) / (1024 * 1024));
  return from + (to - from) * eased / 1024;
}
}  // namespace ambient_detail

inline AmbientPose ambientPose(Animal animal, Scene scene, uint32_t ms, bool sleeping) {
  AmbientPose p;
  if (sleeping) return p;
  const uint32_t cycle = ambientCycleMs(animal);
  const uint32_t t = static_cast<uint32_t>(static_cast<uint64_t>(ms % cycle) * 72000 / cycle);
  const int direction = (ms / cycle) % 2 ? -1 : 1;
  const int reach = scene == Scene::NYC ? 21 : scene == Scene::Space ? 26 : 24;
  if (t < 6000) {
    p.x = 0;
  } else if (t < 16000) {
    p.activity = AmbientActivity::Travel;
    p.x = ambient_detail::interpolate(0, -reach, t - 6000, 10000);
  } else if (t < 24000) {
    p.x = -reach;
  } else if (t < 34000) {
    p.activity = AmbientActivity::Groom;
    p.x = -reach;
    const uint32_t since = t - 24000;
    p.groom = since < 1500 ? static_cast<int>(since * 256 / 1500) :
        since > 8500 ? static_cast<int>((10000 - since) * 256 / 1500) : 256;
  } else if (t < 44000) {
    p.activity = AmbientActivity::Travel;
    p.x = ambient_detail::interpolate(-reach, reach, t - 34000, 10000);
  } else if (t < 52000) {
    p.x = reach;
  } else if (t < 62000) {
    p.activity = AmbientActivity::Doze;
    p.x = reach;
    const uint32_t since = t - 52000;
    p.closed = since > 1200 && since < 8500;
    p.headY = since < 1800 ? static_cast<int>(since / 360) :
        since > 8200 ? static_cast<int>((10000 - since) / 360) : 5;
    p.tilt = p.headY / 2;
  } else {
    p.activity = AmbientActivity::Travel;
    p.x = ambient_detail::interpolate(reach, 0, t - 62000, 10000);
  }
  p.x *= direction;
  if (p.activity == AmbientActivity::Observe) {
    p.gaze = ambient_detail::triangle(ms, animal == Animal::Sloth ? 11000 : 5500, 2);
    p.headX = p.gaze;
    p.tilt = p.gaze * (animal == Animal::SunConure ? 2 : 1);
  }
  if (p.activity == AmbientActivity::Travel) {
    const uint32_t since = t < 16000 ? t - 6000 : t < 44000 ? t - 34000 : t - 62000;
    p.travel = since < 1500 ? static_cast<int>(since * 256 / 1500) :
        since > 8500 ? static_cast<int>((10000 - since) * 256 / 1500) : 256;
    const uint32_t pace = animal == Animal::Sloth ? 3600 : animal == Animal::Cat ? 900 :
        animal == Animal::Frog ? 1400 : 500;
    p.step = ambient_detail::triangle(ms, pace, animal == Animal::Sloth ? 3 : 5);
    const int lift = p.step < 0 ? -p.step : p.step;
    p.y = (animal == Animal::Frog ? lift - 7 : animal == Animal::SunConure ? lift - 6 : lift / 3) * p.travel / 256;
    p.step = p.step * p.travel / 256;
    p.wing = animal == Animal::SunConure ? 7 + ambient_detail::triangle(ms, 420, 7) : 0;
    p.headX = p.x < 0 ? -2 : 2;
    p.gaze = p.headX;
  }
  if (animal == Animal::Cat)
    p.tail = ambient_detail::triangle(ms, p.activity == AmbientActivity::Travel ? 1600 : 5200, 7);
  if (scene == Scene::Space || scene == Scene::UnderSea) {
    // Gently float in the two weightless scenes, without drifting into the HUD.
    const int floatY = ambient_detail::triangle(ms, 5000, 2);
    p.y += floatY + 2;
  }
  return p;
}

}  // namespace sloth
