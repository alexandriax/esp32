#include "../firmware/sloth_pet/leaf_sweep.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace sloth {
struct LeafSweepTestAccess {
  static LeafSweepSnapshot& state(LeafSweepGame& game) { return game.state_; }
  static void isolate(LeafSweepGame& game, uint32_t now = 0) {
    game.reset(now, 12);
    for (auto& object : game.state_.objects) { object.active = false; object.respawnMs = 60000; }
    game.canSpawnMs_ = 60000;
  }
  static void object(LeafSweepGame& game, unsigned index, int x, int y, unsigned age) {
    auto& object = game.state_.objects[index];
    object.active = true; object.x = static_cast<int16_t>(x); object.y = static_cast<int16_t>(y);
    object.ageMs = static_cast<uint16_t>(age); object.respawnMs = 0;
  }
};
}  // namespace sloth

using namespace sloth;
namespace {
void tick(LeafSweepGame& game, uint32_t& now, unsigned duration) {
  assert(duration % LeafSweepGame::kStepMs == 0);
  for (unsigned dt = 0; dt < duration; dt += LeafSweepGame::kStepMs) {
    now += LeafSweepGame::kStepMs; game.advance(now);
  }
}

void checkBounds(const LeafSweepGame& game) {
  const auto& s = game.snapshot();
  assert(s.energy <= 100 && s.multiplier >= 1 && s.multiplier <= 300);
  if (s.pawVisible) {
    assert(s.pawX >= LeafSweepGame::kFieldLeft && s.pawX < LeafSweepGame::kFieldRight);
    assert(s.pawY >= LeafSweepGame::kFieldTop && s.pawY < LeafSweepGame::kFieldBottom);
  }
  unsigned leaves = 0, cans = 0;
  for (const auto& object : s.objects) if (object.active) {
    assert(object.x >= 27 && object.x <= 213 && object.y >= 93 && object.y <= 205);
    assert(object.variant < 4);
    if (object.type == LeafSweepObjectType::Leaf) { ++leaves; assert(object.ageMs <= LeafSweepGame::kLeafGrowthMs); }
    else { ++cans; assert(object.ageMs < LeafSweepGame::kCanLifetimeMs); }
  }
  assert(leaves <= 12 && cans <= 5);
}

void growthAndWarnings() {
  LeafSweepGame game;
  LeafSweepTestAccess::isolate(game);
  uint32_t now = 0;
  LeafSweepTestAccess::object(game, 0, 80, 120, 0);
  game.touch(true, 80, 120, now);
  tick(game, now, 480);
  assert(game.snapshot().leafCount == 0 && game.snapshot().objects[0].active);
  tick(game, now, 20);
  assert(game.snapshot().leafCount == 1 && !game.snapshot().objects[0].active);
  assert(game.takeEvents() == LeafSweepCollect && game.takeEvents() == 0);
  tick(game, now, 2000);
  assert(game.snapshot().leafCount == 1);  // New growth avoids the resting paw.
  assert(game.snapshot().objects[0].active);
  const int dx = game.snapshot().objects[0].x - 80, dy = game.snapshot().objects[0].y - 120;
  assert(dx * dx + dy * dy >= 26 * 26);

  LeafSweepTestAccess::isolate(game, now);
  LeafSweepTestAccess::object(game, 12, 80, 120, 0);
  game.touch(true, 80, 120, now);
  tick(game, now, 680);
  assert(game.snapshot().energy == 0 && game.snapshot().objects[12].active);
  tick(game, now, 20);
  assert(game.snapshot().energy == 25 && !game.snapshot().objects[12].active);
  assert(game.takeEvents() == LeafSweepCanHit);
  tick(game, now, 1000);
  assert(game.snapshot().energy == 22 && game.takeEvents() == 0);
  tick(game, now, 8000);
  assert(game.snapshot().energy == 0);  // No negative energy or repeat hits.

  LeafSweepTestAccess::isolate(game, now);
  LeafSweepTestAccess::object(game, 12, 80, 120, LeafSweepGame::kCanLifetimeMs - 20);
  tick(game, now, 20);
  assert(!game.snapshot().objects[12].active);
  game.touch(true, 80, 120, now);
  assert(game.snapshot().energy == 0);  // Expired cans no longer collide.
}

void sweptContacts() {
  LeafSweepGame game;
  LeafSweepTestAccess::isolate(game);
  LeafSweepTestAccess::object(game, 0, 60, 130, 500);
  LeafSweepTestAccess::object(game, 1, 120, 130, 500);
  LeafSweepTestAccess::object(game, 2, 180, 130, 500);
  game.touch(true, 20, 130, 0); game.touch(true, 220, 130, 0);
  assert(game.snapshot().leafCount == 3 && game.snapshot().score == 900);
  assert(game.takeEvents() == LeafSweepCollect);

  LeafSweepTestAccess::isolate(game);
  LeafSweepTestAccess::object(game, 12, 60, 130, 700);
  LeafSweepTestAccess::object(game, 13, 120, 130, 700);
  LeafSweepTestAccess::object(game, 14, 180, 130, 700);
  game.touch(true, 20, 130, 0); game.touch(true, 220, 130, 0);
  assert(game.snapshot().energy == 75 && game.snapshot().phase == LeafSweepPhase::Playing);
  game.touch(true, 20, 130, 0);
  assert(game.snapshot().energy == 75);  // A removed can cannot hit a second time.

  // Can/leaf ordering follows travel, not object-array order. A fatal can stops
  // the sweep before a leaf beyond it; reversing the path collects the leaf first.
  const bool directions[] = {false, true};
  for (bool reverse : directions) {
    LeafSweepTestAccess::isolate(game);
    LeafSweepTestAccess::state(game).energy = 75;
    LeafSweepTestAccess::object(game, 0, 180, 130, 500);
    LeafSweepTestAccess::object(game, 12, 100, 130, 700);
    game.touch(true, reverse ? 220 : 20, 130, 0);
    game.touch(true, reverse ? 20 : 220, 130, 0);
    assert(game.snapshot().phase == LeafSweepPhase::GameOver && game.snapshot().energy == 100);
    assert(game.snapshot().leafCount == (reverse ? 1u : 0u));
    assert(!game.snapshot().pawVisible);
    const auto frozen = game.snapshot();
    const uint8_t events = game.takeEvents();
    assert((events & (LeafSweepCanHit | LeafSweepGameOver)) == (LeafSweepCanHit | LeafSweepGameOver));
    game.advance(900000); game.touch(true, 120, 130, 900001);
    assert(memcmp(&frozen, &game.snapshot(), sizeof(frozen)) == 0);
    assert(game.takeEvents() == 0);
  }
  LeafSweepTestAccess::isolate(game);
  LeafSweepTestAccess::object(game, 12, 100, 130, 700);
  game.touch(true, 20, 142, 0); game.touch(true, 220, 142, 0);
  assert(game.snapshot().energy == 25);  // Exact tangent still touches the can.
  LeafSweepTestAccess::isolate(game);
  LeafSweepTestAccess::object(game, 12, 100, 130, 700);
  game.touch(true, 20, 143, 0); game.touch(true, 220, 143, 0);
  assert(game.snapshot().energy == 0);
}

void gestureContinuity() {
  LeafSweepGame game;
  LeafSweepTestAccess::isolate(game);
  LeafSweepTestAccess::object(game, 0, 120, 130, 500);
  game.touch(true, 20, 130, 0); game.touch(false, 20, 130, 0); game.touch(true, 220, 130, 0);
  assert(game.snapshot().leafCount == 0);  // Separate contacts cannot teleport across leaves.
  game.touch(true, 120, 30, 0); game.touch(true, 20, 130, 0);
  assert(game.snapshot().leafCount == 0);  // Passing through the HUD breaks continuity.
  game.clearTouch(); game.touch(true, 220, 130, 0);
  assert(game.snapshot().leafCount == 0);
  game.resume(60000); game.touch(true, 20, 130, 60000);
  assert(game.snapshot().leafCount == 0 && game.snapshot().elapsedMs == 0);
  game.touch(true, INT_MIN, INT_MAX, 60000);
  assert(!game.snapshot().pawVisible);
  game.touch(true, 220, 130, 60000);
  assert(game.snapshot().leafCount == 0);
  game.touch(true, 20, 130, 60000);
  assert(game.snapshot().leafCount == 1);  // A genuine continuous drag does collect.

  LeafSweepTestAccess::isolate(game);
  LeafSweepTestAccess::object(game, 0, 100, 120, 500);
  LeafSweepTestAccess::object(game, 12, 100, 95, 700);
  game.touch(true, 100, 200, 0); game.touch(true, 100, 20, 0);
  assert(game.snapshot().leafCount == 1 && game.snapshot().energy == 25 && !game.snapshot().pawVisible);
  LeafSweepTestAccess::object(game, 1, 100, 160, 500);
  game.touch(true, 100, 200, 0);
  assert(game.snapshot().leafCount == 1);  // Re-entry does not bridge the clipped exit.
  game.touch(true, 100, 100, 0);
  assert(game.snapshot().leafCount == 2);
}

void scoringAndClock() {
  LeafSweepGame game;
  LeafSweepTestAccess::isolate(game);
  auto& s = LeafSweepTestAccess::state(game);
  s.leafCount = 10;
  game.advance(0);
  assert(s.score == 3000 && s.multiplier == 300);
  game.advance(40000);
  assert(s.elapsedMs == 40000 && s.score == 1000 && s.multiplier == 100);
  game.advance(100000);
  assert(s.score == 500 && s.multiplier == 50);
  game.resume(1000000); game.advance(1001000);
  assert(s.elapsedMs == 101000 && s.score == 60000000u / 121000u);
  LeafSweepTestAccess::isolate(game);
  s.leafCount = 10; game.advance(12345);
  assert(s.score == 60000000u / 32345u);  // Exact milliseconds, not the displayed rounded multiplier.
  s.leafCount = UINT32_MAX; game.advance(12345);
  assert(s.score == UINT32_MAX);
  s.elapsedMs = UINT32_MAX - 5; game.advance(12365);
  assert(s.elapsedMs == UINT32_MAX && s.multiplier == 1);

  LeafSweepTestAccess::isolate(game);
  s.leafCount = 24;
  LeafSweepTestAccess::object(game, 0, 120, 130, 500);
  game.touch(true, 120, 130, 0);
  assert(s.leafCount == 25 && s.phase == LeafSweepPhase::Playing);
  assert(game.takeEvents() == (LeafSweepCollect | LeafSweepRound));

  LeafSweepGame ordinary, wrapped;
  ordinary.reset(0, 43); wrapped.reset(UINT32_MAX - 49, 43);
  for (uint32_t elapsed = 20; elapsed <= 4000; elapsed += 20) {
    ordinary.advance(elapsed); wrapped.advance(UINT32_MAX - 49 + elapsed);
  }
  assert(memcmp(&ordinary.snapshot(), &wrapped.snapshot(), sizeof(LeafSweepSnapshot)) == 0);
  LeafSweepGame delayed;
  delayed.reset(0, 43); delayed.advance(3600000);
  assert(delayed.snapshot().elapsedMs == 3600000);
  unsigned cans = 0;
  for (const auto& object : delayed.snapshot().objects) cans += object.active && object.type == LeafSweepObjectType::Can;
  assert(cans == 0);  // A stalled frame counts toward pace without a hazard avalanche.
}

void pawStaysInsideField() {
  LeafSweepGame game;
  LeafSweepTestAccess::isolate(game);
  uint16_t baseline[240 * 240], guarded[240 * 240 + 2];
  drawLeafSweep(baseline, game);
  const int points[][2] = {{14, 82}, {225, 82}, {14, 217}, {225, 217},
      {120, 82}, {120, 217}, {14, 150}, {225, 150}};
  for (const auto& point : points) {
    game.clearTouch(); game.touch(true, point[0], point[1], 0);
    guarded[0] = 0x1234; guarded[240 * 240 + 1] = 0xabcd;
    drawLeafSweep(guarded + 1, game);
    assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0xabcd);
    bool pawDrawn = false;
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x) {
      const bool changed = baseline[y * 240 + x] != guarded[y * 240 + x + 1];
      if (x < LeafSweepGame::kFieldLeft || x >= LeafSweepGame::kFieldRight ||
          y < LeafSweepGame::kFieldTop || y >= LeafSweepGame::kFieldBottom) assert(!changed);
      else pawDrawn |= changed;
    }
    assert(pawDrawn);
  }
}

void spawnsAndRandomPlay() {
  LeafSweepGame a, b;
  a.reset(0, 43); b.reset(0, 43);
  assert(memcmp(&a.snapshot(), &b.snapshot(), sizeof(LeafSweepSnapshot)) == 0);
  uint32_t now = 0;
  a.touch(true, 120, 150, 0);
  for (unsigned i = 0; i < 1000; ++i) {
    tick(a, now, 20);
    checkBounds(a);
    for (const auto& object : a.snapshot().objects)
      if (object.active && object.type == LeafSweepObjectType::Can && object.ageMs == 0) {
        const int dx = object.x - 120, dy = object.y - 150;
        assert(dx * dx + dy * dy >= 32 * 32);
      }
    if (now < 2000) for (const auto& object : a.snapshot().objects)
      assert(!object.active || object.type != LeafSweepObjectType::Can);
  }
  uint16_t guarded[240 * 240 + 2];
  guarded[0] = 0x1234; guarded[240 * 240 + 1] = 0xabcd;
  drawLeafSweep(nullptr, a);
  unsigned games = 0, collected = 0, hits = 0;
  uint32_t random = 913;
  a.reset(now, random);
  for (unsigned frame = 0; frame < 40000; ++frame) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    if (a.snapshot().phase == LeafSweepPhase::GameOver) {
      drawLeafSweep(guarded + 1, a);
      assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0xabcd);
      ++games; a.reset(now, random);
    }
    now += 20;
    a.touch(random % 13 != 0, 15 + random % 210, 83 + (random >> 9) % 134, now);
    const uint8_t events = a.takeEvents();
    collected += (events & LeafSweepCollect) != 0; hits += (events & LeafSweepCanHit) != 0;
    checkBounds(a);
    if (frame % 113 == 0) {
      const auto frozen = a.snapshot();
      drawLeafSweep(guarded + 1, a);
      assert(memcmp(&frozen, &a.snapshot(), sizeof(frozen)) == 0);
      assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0xabcd);
    }
  }
  assert(games > 5 && collected > 100 && hits > 100);
  printf("Leaf Sweep randomized play: %u completed games, %u collection events, %u can-hit events\n", games, collected, hits);
}
}  // namespace

int main() {
  static_assert(sizeof(LeafSweepGame) < 400, "Leaf Sweep must use compact fixed state");
  growthAndWarnings(); sweptContacts(); gestureContinuity(); scoringAndClock(); pawStaysInsideField(); spawnsAndRandomPlay();
  puts("Leaf Sweep checks passed: swept/tangent/ordered contacts, warning/growth/expiry, single-hit cans, cooling, spawn safety, exact whole-run efficiency, rounds, pause/wrap/stalls, no contact teleports, frozen endings and render bounds");
}
