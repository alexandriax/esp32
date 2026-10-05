#include "../firmware/sloth_pet/forest_fidget.h"

#include <assert.h>
#include <cmath>
#include <limits.h>
#include <stdio.h>
#include <string.h>

using namespace sloth;
namespace {
void tick(ForestFidget& toy, uint32_t& now, unsigned duration) {
  assert(duration % ForestFidget::kStepMs == 0);
  for (unsigned elapsed = 0; elapsed < duration; elapsed += ForestFidget::kStepMs)
    toy.advance(now += ForestFidget::kStepMs);
}
void choose(ForestFidget& toy, ForestToy kind, uint32_t now = 0, uint32_t seed = 42) {
  toy.reset(now,seed); toy.cycle(static_cast<int>(kind),now);
}
bool near(float a, float b) { return std::fabs(a-b) < .0001f; }
bool sameBodies(const ForestFidgetSnapshot& a, const ForestFidgetSnapshot& b) {
  if (a.bodyCount != b.bodyCount || a.trailCount != b.trailCount || a.trailHead != b.trailHead) return false;
  for (unsigned i = 0; i < a.bodyCount; ++i) {
    const auto& x = a.bodies[i]; const auto& y = b.bodies[i];
    if (!near(x.x,y.x) || !near(x.y,y.y) || !near(x.vx,y.vx) || !near(x.vy,y.vy) ||
        !near(x.angle,y.angle) || !near(x.spin,y.spin) || !near(x.radius,y.radius) ||
        !near(x.value,y.value) || x.active != y.active || x.variant != y.variant) return false;
  }
  for (unsigned i = 0; i < ForestFidgetSnapshot::kTrailPoints; ++i)
    if (!near(a.trail[i].x,b.trail[i].x) || !near(a.trail[i].y,b.trail[i].y) ||
        !near(a.trail[i].strength,b.trail[i].strength)) return false;
  return true;
}
void bounded(const ForestFidget& toy) {
  const auto& s = toy.snapshot();
  assert(static_cast<unsigned>(s.toy) < static_cast<unsigned>(ForestToy::Count));
  assert(s.bodyCount <= ForestFidgetSnapshot::kBodies && s.trailCount <= ForestFidgetSnapshot::kTrailPoints);
  assert(s.trailHead < ForestFidgetSnapshot::kTrailPoints);
  assert(std::isfinite(s.tiltX) && std::fabs(s.tiltX) <= 1.5f);
  assert(std::isfinite(s.tiltY) && std::fabs(s.tiltY) <= 1.5f);
  for (unsigned i = 0; i < s.bodyCount; ++i) {
    const auto& b = s.bodies[i];
    assert(std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.vx) && std::isfinite(b.vy));
    assert(std::isfinite(b.angle) && std::isfinite(b.spin) && std::isfinite(b.value) && std::isfinite(b.radius));
    assert(b.x >= ForestFidget::kFieldLeft && b.x < ForestFidget::kFieldRight);
    assert(b.y >= ForestFidget::kFieldTop && b.y < ForestFidget::kFieldBottom);
    assert(std::fabs(b.vx) < 500 && std::fabs(b.vy) < 500 && std::fabs(b.spin) <= 16);
    assert(b.radius >= 0 && b.radius < 150 && b.variant < 4);
    if (s.toy == ForestToy::LeafGlobe)
      assert(std::pow((b.x-120)/(102-b.radius),2) + std::pow((b.y-141)/(70-b.radius),2) <= 1.00001f);
    if (s.toy == ForestToy::FireflyJar) {
      assert(b.x >= 34+b.radius && b.x <= 206-b.radius);
      assert(b.y >= 90+b.radius && b.y <= 198-b.radius);
    }
  }
  for (const auto& p : s.trail) {
    assert(std::isfinite(p.strength) && p.strength >= 0 && p.strength <= 1);
    if (p.strength) {
      assert(p.x >= ForestFidget::kFieldLeft && p.x < ForestFidget::kFieldRight);
      assert(p.y >= ForestFidget::kFieldTop && p.y < ForestFidget::kFieldBottom);
    }
  }
}
void touchWorksForEveryToy() {
  ForestFidget control, touched;
  for (unsigned kind = 0; kind < static_cast<unsigned>(ForestToy::Count); ++kind) {
    const auto toy = static_cast<ForestToy>(kind);
    choose(control,toy); choose(touched,toy);
    assert(sameBodies(control.snapshot(),touched.snapshot()));
    const auto& s = touched.snapshot();
    int x = s.bodyCount ? static_cast<int>(s.bodies[0].x) : 60;
    int y = s.bodyCount ? static_cast<int>(s.bodies[0].y) : 130;
    if (toy == ForestToy::DewPond) { x = 120; y = 145; }
    if (toy == ForestToy::FernBrush) y = 174;
    if (toy == ForestToy::PineconeSpin) x = 155;
    touched.touch(true,x,y,0);
    touched.touch(true,x+18,y > 185 ? y-18 : y+18,40);
    touched.touch(false,0,0,40);
    control.advance(40);
    assert(!sameBodies(control.snapshot(),touched.snapshot()));
    assert(!touched.snapshot().touching && !touched.snapshot().motionAvailable);
    assert(strlen(forestToyName(toy)) <= 16 && strlen(forestToyHint(toy)) <= 30);
    bounded(touched);
    for (unsigned other = 0; other < kind; ++other)
      assert(strcmp(forestToyName(toy),forestToyName(static_cast<ForestToy>(other))) != 0);
  }
  assert(!strcmp(forestToyName(static_cast<ForestToy>(255)),"FOREST FIDGET"));
  assert(*forestToyHint(static_cast<ForestToy>(255)));
}
void mechanics() {
  ForestFidget toy;
  uint32_t now = 0;
  choose(toy,ForestToy::DewPond);
  toy.touch(true,120,145,now); toy.touch(false,0,0,now);
  const auto& ripple = toy.snapshot().bodies[2];
  assert(ripple.active && near(ripple.value,1));
  tick(toy,now,1000);
  assert(ripple.radius > 39 && ripple.value > .4f && ripple.value < .5f);
  tick(toy,now,1000); assert(!ripple.active);
  choose(toy,ForestToy::DewPond,now);
  toy.touch(true,60,130,now);
  for (int i = 1; i <= 12; ++i) toy.touch(true,60+i*2,130,now+=20);
  unsigned strokeRings = 0;
  for (unsigned i = 0; i < toy.snapshot().bodyCount; ++i)
    strokeRings += toy.snapshot().bodies[i].active && near(toy.snapshot().bodies[i].y,130);
  assert(strokeRings == 4);  // Slow 2px reports accumulate into an actual ripple trail.

  choose(toy,ForestToy::AcornRoll,now);
  toy.touch(true,40,100,now); toy.touch(true,60,80,now+=40); toy.touch(false,0,0,now);
  assert(toy.snapshot().bodies[0].vx == 260 && toy.snapshot().bodies[0].vy == -260);
  tick(toy,now,100); assert(toy.snapshot().bodies[0].x > 75);
  choose(toy,ForestToy::AcornRoll,now);
  toy.touch(true,40,100,now); toy.touch(true,60,100,now+=40);
  toy.touch(true,60,100,now+=20); toy.touch(false,0,0,now);
  assert(toy.snapshot().bodies[0].vx == 260);  // One repeated report must not erase a flick.
  choose(toy,ForestToy::AcornRoll,now);
  toy.touch(true,40,100,now); toy.touch(true,60,100,now+=40);
  toy.touch(true,60,100,now+=120); toy.touch(false,0,0,now);
  assert(toy.snapshot().bodies[0].vx == 0);  // A deliberate stationary hold does stop it.

  const ForestToy springToys[] = {ForestToy::MushroomPop,ForestToy::MossSquish};
  for (ForestToy kind : springToys) {
    choose(toy,kind,now);
    const auto& b = toy.snapshot().bodies[0];
    toy.touch(true,static_cast<int>(b.x),static_cast<int>(b.y),now);
    tick(toy,now,300); assert(b.value > .7f);
    toy.touch(false,0,0,now); tick(toy,now,3000);
    assert(b.value < .02f && std::fabs(b.angle) < .01f);
  }
  choose(toy,ForestToy::FernBrush,now);
  toy.touch(true,30,174,now); toy.touch(true,60,174,now+=40); toy.touch(false,0,0,now);
  assert(toy.snapshot().bodies[1].angle > .2f);
  tick(toy,now,6000); assert(std::fabs(toy.snapshot().bodies[1].angle) < .03f);

  choose(toy,ForestToy::FireflyJar,now);
  float before = 0;
  for (unsigned i = 0; i < toy.snapshot().bodyCount; ++i)
    before += std::hypot(toy.snapshot().bodies[i].x-120,toy.snapshot().bodies[i].y-142);
  toy.touch(true,120,142,now); tick(toy,now,1000);
  float after = 0;
  for (unsigned i = 0; i < toy.snapshot().bodyCount; ++i)
    after += std::hypot(toy.snapshot().bodies[i].x-120,toy.snapshot().bodies[i].y-142);
  assert(after < before*.7f);

  choose(toy,ForestToy::PineconeSpin,now);
  toy.touch(true,158,142,now); toy.touch(true,120,180,now+=100); toy.touch(false,0,0,now);
  const float angle = toy.snapshot().bodies[0].angle, spin = toy.snapshot().bodies[0].spin;
  assert(spin > 10); tick(toy,now,100);
  assert(!near(toy.snapshot().bodies[0].angle,angle));
  assert(toy.snapshot().bodies[0].spin > 0 && toy.snapshot().bodies[0].spin < spin);
  choose(toy,ForestToy::PineconeSpin,now);
  toy.touch(true,158,142,now); toy.touch(true,120,180,now+=100);
  toy.touch(true,120,180,now+=20); toy.touch(false,0,0,now);
  assert(toy.snapshot().bodies[0].spin > 10);
  choose(toy,ForestToy::PineconeSpin,now);
  toy.touch(true,158,142,now); toy.touch(true,120,180,now+=100);
  toy.touch(true,120,180,now+=120); toy.touch(false,0,0,now);
  assert(toy.snapshot().bodies[0].spin == 0);

  choose(toy,ForestToy::PebbleStack,now);
  toy.touch(true,120,204,now); toy.touch(true,60,100,now+=100);
  toy.touch(true,60,100,now+=140); toy.touch(false,0,0,now);
  assert(near(toy.snapshot().bodies[0].vy,0));
  tick(toy,now,700); assert(toy.snapshot().bodies[0].y > 130);

  choose(toy,ForestToy::LeafGlobe,now);
  toy.touch(true,120,140,now); toy.touch(true,180,140,now+=100); toy.touch(false,0,0,now);
  float speed = 0;
  for (unsigned i = 0; i < toy.snapshot().bodyCount; ++i) speed += toy.snapshot().bodies[i].vx;
  assert(speed > 200);

  choose(toy,ForestToy::VineSwing,now);
  toy.touch(true,195,145,now); toy.touch(false,0,0,now);
  const float pulled = toy.snapshot().bodies[0].angle;
  assert(pulled > .7f); tick(toy,now,400);
  assert(toy.snapshot().bodies[0].angle < pulled);
  const auto& vine = toy.snapshot().bodies[0];
  assert(std::fabs(std::hypot(vine.x-120,vine.y-76)-vine.value) < .001f);

  choose(toy,ForestToy::Rainstick,now);
  toy.touch(true,100,110,now); toy.touch(false,0,0,now);
  unsigned fresh = 0;
  for (unsigned i = 0; i < toy.snapshot().bodyCount; ++i) fresh += toy.snapshot().bodies[i].value == 1;
  assert(fresh == 12); tick(toy,now,1000);
  for (unsigned i = 0; i < toy.snapshot().bodyCount; ++i) {
    const auto& b = toy.snapshot().bodies[i];
    for (unsigned peg = 0; peg < kForestRainPegs; ++peg)
      assert(std::hypot(b.x-forestRainPegX(peg),b.y-forestRainPegY(peg)) >= b.radius+2.99f);
  }

  choose(toy,ForestToy::ZenRake,now);
  toy.touch(true,20,120,now); toy.touch(true,220,120,now+=20); toy.touch(false,0,0,now);
  assert(toy.snapshot().trailCount >= 50);
  for (unsigned i = 1; i < toy.snapshot().trailCount; ++i) {
    const auto& a = toy.snapshot().trail[i-1]; const auto& b = toy.snapshot().trail[i];
    assert(std::hypot(b.x-a.x,b.y-a.y) < 4.1f);
  }
  tick(toy,now,1000); assert(toy.snapshot().trail[0].strength > .98f);
  toy.touch(true,100,160,now);
  const unsigned marks = toy.snapshot().trailCount, head = toy.snapshot().trailHead;
  for (unsigned sample = 0; sample < 150; ++sample) toy.touch(true,100,160,now+=20);
  assert(toy.snapshot().trailCount == marks && toy.snapshot().trailHead == head);
  assert(near(toy.snapshot().trail[0].x,20) && toy.snapshot().trail[0].strength > .95f);
}
void clocksAndInputs() {
  ForestFidget a,b;
  for (unsigned kind = 0; kind < static_cast<unsigned>(ForestToy::Count); ++kind) {
    const auto toy = static_cast<ForestToy>(kind);
    choose(a,toy,0); choose(b,toy,UINT32_MAX-31);
    for (uint32_t elapsed = 1; elapsed <= 2000; ++elapsed) {
      a.advance(elapsed); b.advance(UINT32_MAX-31+elapsed);
    }
    assert(sameBodies(a.snapshot(),b.snapshot()));
    choose(a,toy); choose(b,toy);
    a.advance(200); b.advance(3600000);
    assert(sameBodies(a.snapshot(),b.snapshot()) && b.snapshot().elapsedMs == 200);
    choose(a,toy); choose(b,toy);
    a.advance(200); b.advance(200);
    a.resume(500000); a.advance(500200); b.advance(400);
    assert(sameBodies(a.snapshot(),b.snapshot()));
  }
  choose(a,ForestToy::AcornRoll);
  const auto original = a.snapshot();
  a.motion(NAN,0,1,100); a.motion(0,INFINITY,1,200); a.motion(0,0,9,300);
  assert(!a.snapshot().motionAvailable && a.snapshot().elapsedMs == 0 && sameBodies(original,a.snapshot()));
  a.motion(4,-3,1,0);
  assert(a.snapshot().motionAvailable && a.snapshot().tiltX == 1.5f && a.snapshot().tiltY == -1.5f);
  a.advance(1001); assert(!a.snapshot().motionAvailable && a.snapshot().tiltX == 0);
  choose(a,ForestToy::AcornRoll);
  a.touch(true,40,100,0); a.touch(true,80,100,40); a.clearTouch(); a.touch(false,0,0,60);
  assert(!a.snapshot().touching && std::fabs(a.snapshot().bodies[0].vx) < .01f);
  a.touch(true,static_cast<int>(a.snapshot().bodies[0].x),static_cast<int>(a.snapshot().bodies[0].y),60);
  a.resume(200000); assert(!a.snapshot().touching && !a.snapshot().motionAvailable);
  a.touch(true,120,140,200000); a.cycle(1,200000);
  assert(a.snapshot().toy == ForestToy::MushroomPop && !a.snapshot().touching);
  const auto fresh = a.snapshot(); a.touch(false,0,0,200000); assert(sameBodies(fresh,a.snapshot()));
  a.cycle(-3,200000); assert(a.snapshot().toy == ForestToy::ZenRake);
  a.cycle(INT_MIN,200000); bounded(a); a.cycle(INT_MAX,200000); bounded(a);
  a.touch(true,INT_MIN,INT_MAX,200000); assert(!a.snapshot().touching);
  a.reset(300000,0); b.reset(300000,0); assert(sameBodies(a.snapshot(),b.snapshot()));
}
void motionAndSoak() {
  ForestFidget tilted,flat;
  const ForestToy toys[] = {ForestToy::AcornRoll,ForestToy::FireflyJar,ForestToy::PineconeSpin,
      ForestToy::PebbleStack,ForestToy::LeafGlobe,ForestToy::VineSwing,ForestToy::Rainstick};
  for (ForestToy kind : toys) {
    choose(tilted,kind); choose(flat,kind);
    tilted.motion(.8f,-.4f,1,0);
    for (uint32_t t = 20; t <= 600; t += 20) { tilted.advance(t); flat.advance(t); }
    assert(!sameBodies(tilted.snapshot(),flat.snapshot()));
  }
  uint32_t random = 111, now = UINT32_MAX-200;
  unsigned frames = 0;
  for (unsigned kind = 0; kind < static_cast<unsigned>(ForestToy::Count); ++kind) {
    choose(tilted,static_cast<ForestToy>(kind),now,random);
    for (unsigned frame = 0; frame < 10000; ++frame) {
      random ^= random << 13; random ^= random >> 17; random ^= random << 5;
      now += 20;
      if (frame % 7 == 0)
        tilted.motion((static_cast<int>(random % 301)-150)/100.f,
            (static_cast<int>((random>>10) % 301)-150)/100.f,1,now);
      if (frame % 3 == 0) tilted.touch(random % 7 != 0,random % 240,(random>>8) % 240,now);
      tilted.advance(now); bounded(tilted); ++frames;
      if (frame % 137 == 0) tilted.resume(now += 5000);
    }
  }
  printf("Forest Fidget soak: %u steps across all 12 toys\n",frames);
}
}
int main() {
  static_assert(sizeof(ForestFidget) < 2300,"Forest Fidget must retain bounded fixed state");
  touchWorksForEveryToy(); mechanics(); clocksAndInputs(); motionAndSoak();
  puts("Forest Fidget passed: all 12 touch-only toys, springs/inertia/lure/pendulum/pegs/trails, tilt, finite bounds, seed/pause/wrap/stall, cancellation and cycling");
}
