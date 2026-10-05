#include "forest_fidget.h"

#include <cmath>
#include <limits.h>

namespace sloth {
#if __cplusplus < 201703L
constexpr unsigned ForestFidgetSnapshot::kBodies;
constexpr unsigned ForestFidgetSnapshot::kTrailPoints;
constexpr int ForestFidget::kFieldLeft;
constexpr int ForestFidget::kFieldRight;
constexpr int ForestFidget::kFieldTop;
constexpr int ForestFidget::kFieldBottom;
constexpr int ForestFidget::kStepMs;
constexpr int ForestFidget::kMaxCatchupMs;
#endif
namespace {
constexpr float kDt = .02f, kPi = 3.141592654f;
float bound(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
float square(float x) { return x * x; }
float wrapped(float angle) {
  if (angle > kPi) angle -= 2 * kPi;
  if (angle < -kPi) angle += 2 * kPi;
  return angle;
}
bool field(int x, int y) {
  return x >= ForestFidget::kFieldLeft && x < ForestFidget::kFieldRight &&
      y >= ForestFidget::kFieldTop && y < ForestFidget::kFieldBottom;
}
void globeConfine(ForestBody& b) {
  const float rx = 102-b.radius, ry = 70-b.radius;
  const float dx = b.x-120, dy = b.y-141;
  const float distance = square(dx/rx)+square(dy/ry);
  if (distance <= 1) return;
  const float scale = 1/std::sqrt(distance);
  b.x = 120+dx*scale; b.y = 141+dy*scale;
  float nx = dx/(rx*rx), ny = dy/(ry*ry);
  const float length = std::sqrt(nx*nx+ny*ny); nx /= length; ny /= length;
  const float outward = b.vx*nx+b.vy*ny;
  if (outward > 0) { b.vx -= 1.7f*outward*nx; b.vy -= 1.7f*outward*ny; }
}
void jarConfine(ForestBody& b) {
  const float left = 34+b.radius, right = 206-b.radius;
  const float top = 90+b.radius, bottom = 198-b.radius;
  if (b.x < left) { b.x = left; if (b.vx < 0) b.vx *= -.7f; }
  if (b.x > right) { b.x = right; if (b.vx > 0) b.vx *= -.7f; }
  if (b.y < top) { b.y = top; if (b.vy < 0) b.vy *= -.7f; }
  if (b.y > bottom) { b.y = bottom; if (b.vy > 0) b.vy *= -.7f; }
}
}

const char* forestToyName(ForestToy toy) {
  const char* const names[] = {"DEW POND", "ACORN ROLL", "MUSHROOM POP", "FERN BRUSH",
      "FIREFLY JAR", "PINECONE SPIN", "PEBBLE STACK", "MOSS SQUISH", "LEAF GLOBE",
      "VINE SWING", "RAINSTICK", "ZEN RAKE"};
  const unsigned index = static_cast<unsigned>(toy);
  return index < static_cast<unsigned>(ForestToy::Count) ? names[index] : "FOREST FIDGET";
}
const char* forestToyHint(ForestToy toy) {
  const char* const hints[] = {"TAP OR SWIRL THE WATER", "TILT OR FLICK THE ACORNS",
      "PRESS CAPS. LET THEM POP.", "BRUSH THE FRONDS", "TOUCH TO LURE. TILT TO SWIRL.",
      "TURN, RELEASE, WATCH IT SPIN", "DRAG TO STACK. TILT TO TUMBLE.",
      "PRESS AND STROKE THE MOSS", "TILT OR SWIPE A LEAF BREEZE", "PULL THE VINE. LET IT SWING.",
      "TILT SEEDS. TAP FOR A SHOWER.", "DRAW IN SAND. TILT TO SOFTEN."};
  const unsigned index = static_cast<unsigned>(toy);
  return index < static_cast<unsigned>(ForestToy::Count) ? hints[index] : "TOUCH AND EXPLORE";
}
float forestRainPegX(unsigned index) { return index < kForestRainPegs ? 65.f + (index % 3) * 44.f + ((index / 3) % 2) * 22.f : 120.f; }
float forestRainPegY(unsigned index) { return index < kForestRainPegs ? 92.f + (index / 3) * 22.f : 142.f; }

ForestFidget::ForestFidget() { reset(0, 0x4d055u); }
uint32_t ForestFidget::randomBelow(uint32_t limit) {
  random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
  return static_cast<uint32_t>((static_cast<uint64_t>(random_) * limit) >> 32);
}
void ForestFidget::reset(uint32_t now, uint32_t seed) {
  random_ = seed ? seed : 0x4d055u;
  beginToy(ForestToy::DewPond, now);
}
void ForestFidget::cycle(int direction, uint32_t now) {
  const int count = static_cast<int>(ForestToy::Count);
  const int next = (static_cast<int>(state_.toy) + direction % count + count) % count;
  beginToy(static_cast<ForestToy>(next), now);
}
void ForestFidget::beginToy(ForestToy toy, uint32_t now) {
  state_ = ForestFidgetSnapshot(); state_.toy = toy;
  lastMs_ = motionMs_ = pointerMs_ = movementMs_ = now; accumulatedMs_ = 0;
  grabbed_ = -1; cursor_ = 0; pointerVx_ = pointerVy_ = spinTouchAngle_ = 0;
  emittedX_ = emittedY_ = 0;
  const unsigned counts[] = {12,6,12,10,18,1,6,20,18,1,24,0};
  state_.bodyCount = static_cast<uint8_t>(counts[static_cast<unsigned>(toy)]);
  for (unsigned i = 0; i < state_.bodyCount; ++i) {
    auto& b = state_.bodies[i]; b.active = true; b.variant = static_cast<uint8_t>(i % 4);
    b.x = 28.f + randomBelow(184); b.y = 80.f + randomBelow(124);
    b.radius = 5; b.value = 0;
    switch (toy) {
      case ForestToy::DewPond: b.active = false; break;
      case ForestToy::AcornRoll:
        b.x = 40.f + i * 32.f; b.y = 100.f + (i % 3) * 30.f;
        b.radius = 8.f + i % 2; b.vx = static_cast<int>(randomBelow(41)) - 20.f; break;
      case ForestToy::MushroomPop:
        b.x = 43.f + (i % 4) * 51.f; b.y = 94.f + (i / 4) * 49.f; b.radius = 12; break;
      case ForestToy::FernBrush:
        b.x = 30.f + i * 20.f; b.y = 207; b.radius = 43.f + (i % 3) * 13.f; b.value = .85f; break;
      case ForestToy::FireflyJar:
        b.radius = 2.f + i % 2; b.value = .7f;
        b.x = 36.f + randomBelow(168); b.y = 92.f + randomBelow(104);
        b.vx = static_cast<int>(randomBelow(41)) - 20.f;
        b.vy = static_cast<int>(randomBelow(41)) - 20.f; b.angle = i * .4f; jarConfine(b); break;
      case ForestToy::PineconeSpin:
        b.x = 120; b.y = 142; b.radius = 38; b.spin = .6f; break;
      case ForestToy::PebbleStack:
        b.x = 120; b.y = 204.f - i * 23.f; b.radius = 11; break;
      case ForestToy::MossSquish:
        b.x = 36.f + (i % 5) * 42.f; b.y = 85.f + (i / 5) * 38.f; b.radius = 15; break;
      case ForestToy::LeafGlobe:
        b.radius = 5.f + i % 3; b.value = i * .71f;
        b.vx = static_cast<int>(randomBelow(31)) - 15.f;
        b.vy = static_cast<int>(randomBelow(21)) - 10.f; globeConfine(b); break;
      case ForestToy::VineSwing:
        b.radius = 10; b.value = 104; b.angle = .4f;
        b.x = 120 + std::sin(b.angle) * b.value; b.y = 76 + std::cos(b.angle) * b.value; break;
      case ForestToy::Rainstick:
        b.radius = 2.5f; b.x = 30.f + i * 7.5f; b.y = 74.f + (i % 3) * 5.f; break;
      case ForestToy::ZenRake: case ForestToy::Count: break;
    }
  }
  if (toy == ForestToy::DewPond) { ring(83, 126); ring(157, 172); }
}

void ForestFidget::clearTouch() {
  if (grabbed_ >= 0) {
    auto& b = state_.bodies[static_cast<unsigned>(grabbed_)];
    b.vx = b.vy = b.spin = 0;
  }
  state_.touching = false; grabbed_ = -1; pointerVx_ = pointerVy_ = 0;
}
void ForestFidget::resume(uint32_t now) {
  clearTouch(); lastMs_ = now; accumulatedMs_ = 0;
  state_.motionAvailable = false; state_.tiltX = state_.tiltY = 0;
}
void ForestFidget::motion(float xg, float yg, float zg, uint32_t now) {
  if (!std::isfinite(xg) || !std::isfinite(yg) || !std::isfinite(zg) ||
      std::fabs(xg) > 8 || std::fabs(yg) > 8 || std::fabs(zg) > 8) return;
  advance(now); motionMs_ = now; state_.motionAvailable = true;
  state_.tiltX = bound(xg, -1.5f, 1.5f); state_.tiltY = bound(yg, -1.5f, 1.5f);
}
void ForestFidget::advance(uint32_t now) {
  const uint32_t elapsed = now - lastMs_; lastMs_ = now;
  if (state_.motionAvailable && now - motionMs_ > 1000) {
    state_.motionAvailable = false; state_.tiltX = state_.tiltY = 0;
  }
  accumulatedMs_ += static_cast<uint16_t>(elapsed > kMaxCatchupMs ? kMaxCatchupMs : elapsed);
  while (accumulatedMs_ >= kStepMs) {
    accumulatedMs_ -= kStepMs;
    state_.elapsedMs += kStepMs;
    step();
  }
}
int ForestFidget::nearest(float x, float y, float reach) const {
  int result = -1; float distance = reach * reach;
  for (unsigned i = 0; i < state_.bodyCount; ++i) {
    const auto& b = state_.bodies[i];
    const float d = square(x - b.x) + square(y - b.y);
    if (b.active && d <= distance) { result = static_cast<int>(i); distance = d; }
  }
  return result;
}
void ForestFidget::ring(float x, float y) {
  auto& b = state_.bodies[cursor_++ % state_.bodyCount];
  b.active = true; b.x = x; b.y = y; b.radius = 2; b.value = 1;
}
void ForestFidget::trail(float x, float y) {
  auto& p = state_.trail[state_.trailHead]; p.x = x; p.y = y; p.strength = 1;
  state_.trailHead = (state_.trailHead + 1) % ForestFidgetSnapshot::kTrailPoints;
  if (state_.trailCount < ForestFidgetSnapshot::kTrailPoints) ++state_.trailCount;
}
void ForestFidget::deform(float x, float y, float dx) {
  for (unsigned i = 0; i < state_.bodyCount; ++i) {
    auto& b = state_.bodies[i];
    const float targetY = state_.toy == ForestToy::FernBrush ? b.y - b.radius * .6f : b.y;
    const float distance = std::sqrt(square(x - b.x) + square(y - targetY));
    const float reach = state_.toy == ForestToy::FernBrush ? 65.f : 34.f;
    const float strength = bound(1 - distance / reach, 0, 1);
    if (strength <= 0) continue;
    if (state_.toy == ForestToy::FernBrush) {
      b.angle = bound(b.angle + (dx * .025f + (x - b.x) * .008f) * strength, -1.1f, 1.1f);
      b.spin = bound(b.spin + dx * .08f * strength, -5, 5);
      b.value = bound(b.value + .35f * strength, 0, 1);
    } else {
      b.value = bound(b.value + .65f * strength, 0, 1);
      b.vy = -2.5f * strength;
      b.angle = bound((dx + x - b.x) * .025f * strength, -.6f, .6f);
    }
  }
}

void ForestFidget::touch(bool down, int x, int y, uint32_t now) {
  advance(now);
  if (!down || !field(x,y)) {
    // A physical release keeps the measured flick. Cancelling a gesture through
    // clearTouch/resume/cycle instead removes it, so old contacts cannot bleed.
    if (!down && state_.touching && grabbed_ >= 0) {
      auto& b = state_.bodies[static_cast<unsigned>(grabbed_)];
      if (now-movementMs_ > 100) { pointerVx_ = pointerVy_ = 0; b.spin = 0; }
      if (state_.toy == ForestToy::AcornRoll || state_.toy == ForestToy::PebbleStack) {
        b.vx = pointerVx_; b.vy = pointerVy_;
      }
      grabbed_ = -1;
    }
    clearTouch(); return;
  }
  const bool first = !state_.touching;
  const float dx = first ? 0.f : static_cast<float>(x) - state_.touchX;
  const float dy = first ? 0.f : static_cast<float>(y) - state_.touchY;
  const uint32_t gap = now - pointerMs_;
  const float seconds = static_cast<float>(gap < 10 ? 10 : gap > 100 ? 100 : gap) / 1000;
  const bool moving = dx != 0 || dy != 0;
  if (first || moving) {
    pointerVx_ = bound(dx / seconds, -260, 260); pointerVy_ = bound(dy / seconds, -260, 260);
    movementMs_ = now;
  } else if (now-movementMs_ > 100) pointerVx_ = pointerVy_ = 0;
  if (!first && moving && state_.toy == ForestToy::ZenRake) {
    const float length = std::sqrt(dx * dx + dy * dy);
    const unsigned pieces = static_cast<unsigned>(length / 4) + 1;
    for (unsigned i = 1; i <= pieces; ++i)
      trail(state_.touchX + dx * i / pieces, state_.touchY + dy * i / pieces);
  }
  state_.touching = true; state_.touchX = static_cast<int16_t>(x); state_.touchY = static_cast<int16_t>(y);
  pointerMs_ = now;
  switch (state_.toy) {
    case ForestToy::DewPond:
      if (first || square(x-emittedX_)+square(y-emittedY_) >= 64) {
        ring(x,y); emittedX_ = static_cast<int16_t>(x); emittedY_ = static_cast<int16_t>(y);
      }
      break;
    case ForestToy::AcornRoll: case ForestToy::PebbleStack: {
      if (first) grabbed_ = static_cast<int8_t>(nearest(x,y,36));
      if (grabbed_ >= 0) {
        auto& b = state_.bodies[static_cast<unsigned>(grabbed_)];
        b.x = x; b.y = y; b.vx = b.vy = 0; b.value = .5f; confine(b,.6f);
      }
      break;
    }
    case ForestToy::MushroomPop: case ForestToy::FernBrush: case ForestToy::MossSquish:
      deform(x,y,dx); break;
    case ForestToy::FireflyJar:
      // An immediate tug makes even a quick tap visibly lure the nearest glow.
      for (unsigned i = 0; i < state_.bodyCount; ++i) {
        auto& b = state_.bodies[i];
        b.vx = bound(b.vx + (x - b.x) * .15f, -160, 160);
        b.vy = bound(b.vy + (y - b.y) * .15f, -160, 160); b.value = 1;
      }
      break;
    case ForestToy::PineconeSpin: {
      auto& b = state_.bodies[0];
      const float angle = std::atan2(y - b.y, x - b.x);
      if (first) { b.angle = wrapped(b.angle + .12f); b.spin = .8f; grabbed_ = 0; }
      else {
        const float delta = wrapped(angle - spinTouchAngle_);
        b.angle = wrapped(b.angle + delta);
        if (moving) b.spin = bound(delta / seconds, -16, 16);
        else if (now-movementMs_ > 100) b.spin = 0;
      }
      b.value = 1; spinTouchAngle_ = angle; break;
    }
    case ForestToy::LeafGlobe:
      for (unsigned i = 0; i < state_.bodyCount; ++i) {
        auto& b = state_.bodies[i];
        const float influence = bound(1 - std::sqrt(square(x-b.x) + square(y-b.y)) / 160, 0, 1);
        b.vx = bound(b.vx + (first ? (b.x < x ? -30.f : 30.f) : dx * 3) * influence, -180, 180);
        b.vy = bound(b.vy + (first ? -25.f : dy * 3) * influence, -180, 180);
      }
      break;
    case ForestToy::VineSwing: {
      auto& b = state_.bodies[0]; grabbed_ = 0;
      const float angle = bound(std::atan2(static_cast<float>(x)-120, static_cast<float>(y)-76), -1.15f, 1.15f);
      if (first || moving) b.spin = first ? 0 : bound(wrapped(angle - b.angle) / seconds, -5, 5);
      else if (now-movementMs_ > 100) b.spin = 0;
      b.angle = angle;
      b.x = 120 + std::sin(angle) * b.value; b.y = 76 + std::cos(angle) * b.value; break;
    }
    case ForestToy::Rainstick:
      if (first || square(x-emittedX_)+square(y-emittedY_) >= 64) {
        emittedX_ = static_cast<int16_t>(x); emittedY_ = static_cast<int16_t>(y);
        const unsigned count = first ? 12 : 3;
        for (unsigned i = 0; i < count; ++i) {
          auto& b = state_.bodies[cursor_++ % state_.bodyCount];
          b.x = x + static_cast<int>(randomBelow(35)) - 17.f;
          b.y = y - randomBelow(24); b.vx = static_cast<int>(randomBelow(61)) - 30.f;
          b.vy = 45 + randomBelow(30); b.value = 1; confine(b,.7f);
        }
      }
      break;
    case ForestToy::ZenRake: if (first) trail(x,y); break;
    case ForestToy::Count: break;
  }
}

void ForestFidget::confine(ForestBody& b, float bounce) {
  const float left = kFieldLeft + b.radius, right = kFieldRight - b.radius - 1;
  const float top = kFieldTop + b.radius, bottom = kFieldBottom - b.radius - 1;
  if (b.x < left) { b.x = left; if (b.vx < 0) b.vx *= -bounce; }
  if (b.x > right) { b.x = right; if (b.vx > 0) b.vx *= -bounce; }
  if (b.y < top) { b.y = top; if (b.vy < 0) b.vy *= -bounce; }
  if (b.y > bottom) { b.y = bottom; if (b.vy > 0) b.vy *= -bounce; }
}
void ForestFidget::collide(float bounce) {
  for (unsigned i = 0; i < state_.bodyCount; ++i) for (unsigned j = i + 1; j < state_.bodyCount; ++j) {
    auto& a = state_.bodies[i]; auto& b = state_.bodies[j];
    float dx = b.x-a.x, dy = b.y-a.y;
    const float minimum = a.radius + b.radius;
    const float distanceSquared = dx*dx+dy*dy;
    if (distanceSquared >= minimum*minimum) continue;
    float distance = std::sqrt(distanceSquared);
    if (distance < .001f) { dx = 1; dy = 0; distance = 1; }
    const float nx = dx/distance, ny = dy/distance;
    const bool grabA = grabbed_ == static_cast<int>(i), grabB = grabbed_ == static_cast<int>(j);
    const float wa = grabA ? 0 : grabB ? 1 : .5f, wb = 1-wa;
    a.x -= nx*(minimum-distance)*wa; a.y -= ny*(minimum-distance)*wa;
    b.x += nx*(minimum-distance)*wb; b.y += ny*(minimum-distance)*wb;
    const float approach = (b.vx-a.vx)*nx + (b.vy-a.vy)*ny;
    if (approach < 0) {
      const float impulse = -(1+bounce)*approach;
      a.vx -= nx*impulse*wa; a.vy -= ny*impulse*wa;
      b.vx += nx*impulse*wb; b.vy += ny*impulse*wb;
      a.value = b.value = bound(-approach / 160, 0, 1);
    }
  }
  for (unsigned i = 0; i < state_.bodyCount; ++i) confine(state_.bodies[i],bounce);
}

void ForestFidget::step() {
  const float tx = state_.tiltX, ty = state_.tiltY;
  const float time = (state_.elapsedMs % 60000) / 1000.f;
  if (state_.toy == ForestToy::ZenRake) {
    for (auto& p : state_.trail) p.strength = bound(p.strength - kDt * (.008f + (std::fabs(tx)+std::fabs(ty))*.018f),0,1);
    return;
  }
  if (state_.touching && (state_.toy == ForestToy::MushroomPop || state_.toy == ForestToy::MossSquish))
    deform(state_.touchX,state_.touchY,0);
  for (unsigned i = 0; i < state_.bodyCount; ++i) {
    auto& b = state_.bodies[i]; if (!b.active) continue;
    const bool grabbed = grabbed_ == static_cast<int>(i);
    switch (state_.toy) {
      case ForestToy::DewPond:
        b.radius += kDt*38; b.value = bound(b.value-kDt*.55f,0,1);
        if (!b.value) b.active = false; break;
      case ForestToy::AcornRoll: case ForestToy::PebbleStack:
        if (!grabbed) {
          const bool pebble = state_.toy == ForestToy::PebbleStack;
          b.vx = bound((b.vx + tx*kDt*240)*.992f,-260,260);
          b.vy = bound((b.vy + (ty*240 + (pebble ? 220 : 0))*kDt)*.992f,-260,260);
          b.x += b.vx*kDt; b.y += b.vy*kDt;
          b.angle = wrapped(b.angle + b.vx*kDt / b.radius);
          confine(b,pebble ? .22f : .7f);
        }
        b.value *= .86f; break;
      case ForestToy::MushroomPop: case ForestToy::MossSquish:
        b.vy += (-28*b.value - 5*b.vy)*kDt;
        b.value = bound(b.value+b.vy*kDt,0,1);
        if (!b.value && b.vy < 0) b.vy *= -.42f;
        b.angle *= .91f; break;
      case ForestToy::FernBrush:
        b.spin += (-7*b.angle - 2*b.spin + tx*.8f)*kDt;
        b.angle = bound(b.angle+b.spin*kDt,-1.2f,1.2f);
        b.value += (.72f-b.value)*kDt*.6f; break;
      case ForestToy::FireflyJar: {
        float ax = std::sin(time*1.3f+i)*25+tx*75, ay = std::cos(time*1.1f+i*.8f)*20+ty*75;
        if (state_.touching) { ax += (state_.touchX-b.x)*2.5f; ay += (state_.touchY-b.y)*2.5f; }
        b.vx = bound((b.vx+ax*kDt)*.974f,-150,150); b.vy = bound((b.vy+ay*kDt)*.974f,-150,150);
        b.x += b.vx*kDt; b.y += b.vy*kDt; b.value = .55f+.45f*std::sin(time*2+i*.7f);
        jarConfine(b); break;
      }
      case ForestToy::PineconeSpin:
        if (!grabbed) { b.spin = bound((b.spin+tx*.8f*kDt)*.989f,-16,16); b.angle = wrapped(b.angle+b.spin*kDt); }
        b.value *= .96f; break;
      case ForestToy::LeafGlobe:
        b.value = wrapped(b.value+kDt*(1+i*.05f));
        b.vx = bound((b.vx+(tx*105+std::sin(b.value)*10)*kDt)*.992f,-180,180);
        b.vy = bound((b.vy+(ty*105+std::cos(b.value)*8)*kDt)*.992f,-180,180);
        b.x += b.vx*kDt; b.y += b.vy*kDt; b.angle = std::sin(b.value)*.6f+b.vx*.009f;
        globeConfine(b); break;
      case ForestToy::VineSwing:
        if (!grabbed) {
          b.spin = bound((b.spin + (-5*std::sin(b.angle)+tx*2.8f)*kDt)*.998f,-5,5);
          b.angle += b.spin*kDt;
          if (b.angle < -1.15f || b.angle > 1.15f) { b.angle = bound(b.angle,-1.15f,1.15f); b.spin *= -.6f; }
          b.x = 120+std::sin(b.angle)*b.value; b.y = 76+std::cos(b.angle)*b.value;
        }
        break;
      case ForestToy::Rainstick:
        b.vx = bound((b.vx+tx*220*kDt)*.995f,-180,180);
        b.vy = bound((b.vy+(ty*220+160)*kDt)*.995f,-180,180);
        b.x += b.vx*kDt; b.y += b.vy*kDt; b.value *= .9f;
        for (unsigned peg = 0; peg < kForestRainPegs; ++peg) {
          float dx = b.x-forestRainPegX(peg), dy = b.y-forestRainPegY(peg);
          const float minimum = b.radius+3;
          const float distanceSquared = dx*dx+dy*dy;
          if (distanceSquared >= minimum*minimum) continue;
          float distance = std::sqrt(distanceSquared);
          if (distance < .001f) { dx = 0; dy = -1; distance = 1; }
          const float nx = dx/distance, ny = dy/distance;
          b.x = forestRainPegX(peg)+nx*(b.radius+3); b.y = forestRainPegY(peg)+ny*(b.radius+3);
          const float normal = b.vx*nx+b.vy*ny;
          if (normal < 0) { b.vx -= 1.65f*normal*nx; b.vy -= 1.65f*normal*ny; b.value = 1; }
        }
        confine(b,.68f); break;
      case ForestToy::ZenRake: case ForestToy::Count: break;
    }
  }
  if (state_.toy == ForestToy::AcornRoll) collide(.65f);
  if (state_.toy == ForestToy::PebbleStack) { collide(.12f); collide(.12f); }
}

}  // namespace sloth
