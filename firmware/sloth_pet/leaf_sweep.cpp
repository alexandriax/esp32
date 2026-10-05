#include "leaf_sweep.h"
#include "pet_canvas.h"

#include <math.h>
#include <stdio.h>

namespace sloth {
#if __cplusplus < 201703L
constexpr int LeafSweepGame::kLeafSlots;
constexpr int LeafSweepGame::kCanSlots;
constexpr int LeafSweepGame::kObjectCount;
constexpr int LeafSweepGame::kFieldLeft;
constexpr int LeafSweepGame::kFieldRight;
constexpr int LeafSweepGame::kFieldTop;
constexpr int LeafSweepGame::kFieldBottom;
constexpr int LeafSweepGame::kLeafGrowthMs;
constexpr int LeafSweepGame::kCanWarningMs;
constexpr int LeafSweepGame::kCanLifetimeMs;
constexpr int LeafSweepGame::kStepMs;
constexpr int LeafSweepGame::kMaxCatchupMs;
#endif
namespace {
const int kLeafRadius = 10, kCanRadius = 12;
uint32_t addSaturated(uint32_t a, uint32_t b) { return b > UINT32_MAX - a ? UINT32_MAX : a + b; }
int distanceSquared(int x, int y, int otherX, int otherY) {
  const int dx = x - otherX, dy = y - otherY;
  return dx * dx + dy * dy;
}

// First contact along a segment, including a stationary finger and a segment
// beginning inside the circle. Sorting these times prevents collecting a leaf
// beyond a fatal can, regardless of which object occupies the lower array slot.
bool contactTime(int ax, int ay, int bx, int by, int cx, int cy, int radius, float& time) {
  const float dx = static_cast<float>(bx - ax), dy = static_cast<float>(by - ay);
  const float px = static_cast<float>(ax - cx), py = static_cast<float>(ay - cy);
  const float c = px * px + py * py - radius * radius;
  if (c <= 0) { time = 0; return true; }
  const float a = dx * dx + dy * dy;
  if (a == 0) return false;
  const float b = px * dx + py * dy;
  const float discriminant = b * b - a * c;
  if (discriminant < 0) return false;
  time = (-b - sqrtf(discriminant)) / a;
  return time >= 0 && time <= 1;
}

void clipExit(int ax, int ay, int bx, int by, int& x, int& y) {
  const float dx = static_cast<float>(bx) - ax, dy = static_cast<float>(by) - ay;
  float fraction = 1;
  if (bx < LeafSweepGame::kFieldLeft) fraction = (LeafSweepGame::kFieldLeft - ax) / dx;
  else if (bx >= LeafSweepGame::kFieldRight) fraction = (LeafSweepGame::kFieldRight - 1 - ax) / dx;
  if (by < LeafSweepGame::kFieldTop) {
    const float edge = (LeafSweepGame::kFieldTop - ay) / dy;
    if (edge < fraction) fraction = edge;
  } else if (by >= LeafSweepGame::kFieldBottom) {
    const float edge = (LeafSweepGame::kFieldBottom - 1 - ay) / dy;
    if (edge < fraction) fraction = edge;
  }
  x = static_cast<int>(ax + fraction * dx);
  y = static_cast<int>(ay + fraction * dy);
  if (x < LeafSweepGame::kFieldLeft) x = LeafSweepGame::kFieldLeft;
  if (x >= LeafSweepGame::kFieldRight) x = LeafSweepGame::kFieldRight - 1;
  if (y < LeafSweepGame::kFieldTop) y = LeafSweepGame::kFieldTop;
  if (y >= LeafSweepGame::kFieldBottom) y = LeafSweepGame::kFieldBottom - 1;
}

void drawLeaf(graphics::Canvas& c, const LeafSweepObject& leaf, uint32_t elapsed) {
  using namespace graphics;
  const int grown = leaf.ageMs >= LeafSweepGame::kLeafGrowthMs ? 6 : 2 + leaf.ageMs * 4 / LeafSweepGame::kLeafGrowthMs;
  const uint16_t colors[] = {mint, rgb(161,193,99), rgb(111,173,112), gold};
  const uint16_t color = colors[leaf.variant % 4];
  const int direction = leaf.variant % 2 ? -1 : 1;
  const int sway = static_cast<int>((elapsed / 500 + leaf.variant) % 4) == 1 ? 1 : 0;
  const int x = leaf.x + sway, y = leaf.y;
  c.oval(leaf.x + 1, y + 3, grown + 2, 3, rgb(12,32,27));
  c.oval(x, y + 1, grown, grown - 1, rgb(69,110,60));
  c.oval(x, y - 1, grown, grown - 2, color);
  c.triangle(x, y - grown + 1, x + direction * (grown + 3), y - grown - 2,
             x + direction * grown, y + 1, color);
  c.line(x - direction * (grown - 2), y + grown - 1,
         x + direction * (grown + 1), y - grown, rgb(228,237,185));
  if (grown >= 5) {
    c.line(x, y, x - direction * 3, y - 2, rgb(199,221,144));
    c.line(x + direction * 2, y - 2, x + direction * 4, y - 1, rgb(199,221,144));
    c.dot(x - direction * 2, y + 2, cream);
  }
}

void drawCan(graphics::Canvas& c, const LeafSweepObject& can) {
  using namespace graphics;
  if (can.ageMs < LeafSweepGame::kCanWarningMs) {
    const int radius = can.ageMs / 140 % 2 ? 11 : 9;
    c.oval(can.x, can.y, radius, radius, gold);
    c.oval(can.x, can.y, radius - 1, radius - 1, rgb(21,43,35));
    c.line(can.x - 3, can.y + 6, can.x + 3, can.y + 6, rgb(174,118,70));
    c.text(can.x - 2, can.y - 3, "!", gold);
    return;
  }
  const uint16_t danger = rgb(226,132,112);
  c.oval(can.x + 1, can.y + 8, 7, 3, rgb(10,29,26));
  c.roundRect(can.x - 5, can.y - 8, 11, 17, 2, danger);
  c.rect(can.x - 4, can.y - 6, 2, 12, rgb(252,184,142));
  c.rect(can.x + 3, can.y - 6, 2, 12, rgb(161,80,74));
  c.oval(can.x, can.y - 7, 4, 2, cream);
  c.line(can.x - 1, can.y - 7, can.x + 1, can.y - 7, rgb(104,130,125));
  c.rect(can.x - 3, can.y + 6, 7, 2, rgb(178,197,180));
  c.line(can.x + 1, can.y - 4, can.x - 2, can.y, ink);
  c.line(can.x - 2, can.y, can.x + 2, can.y, ink);
  c.line(can.x + 2, can.y, can.x - 1, can.y + 4, ink);
}

void pawDot(graphics::Canvas& c, int x, int y, uint16_t color) {
  if (x >= LeafSweepGame::kFieldLeft && x < LeafSweepGame::kFieldRight &&
      y >= LeafSweepGame::kFieldTop && y < LeafSweepGame::kFieldBottom) c.dot(x, y, color);
}

void pawOval(graphics::Canvas& c, int x, int y, int rx, int ry, uint16_t color) {
  for (int dy = -ry; dy <= ry; ++dy) for (int dx = -rx; dx <= rx; ++dx)
    if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry) pawDot(c, x + dx, y + dy, color);
}

void pawLine(graphics::Canvas& c, int x, int y, int toX, int toY, uint16_t color, int radius = 0) {
  const int dx = toX > x ? toX - x : x - toX, dy = toY > y ? y - toY : toY - y;
  const int sx = x < toX ? 1 : -1, sy = y < toY ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    if (radius) pawOval(c, x, y, radius, radius, color);
    else pawDot(c, x, y, color);
    if (x == toX && y == toY) break;
    const int twice = error * 2;
    if (twice >= dy) { error += dy; x += sx; }
    if (twice <= dx) { error += dx; y += sy; }
  }
}

void drawPaw(graphics::Canvas& c, int x, int y) {
  using namespace graphics;
  pawOval(c, x + 1, y + 4, 13, 8, rgb(10,28,25));
  pawOval(c, x, y - 4, 11, 9, furDark);
  pawOval(c, x, y - 4, 10, 8, brown);
  pawOval(c, x, y - 5, 7, 5, furLight);
  pawLine(c, x - 5, y - 9, x - 3, y - 11, face);
  pawLine(c, x, y - 9, x + 1, y - 11, face);
  pawLine(c, x + 5, y - 8, x + 6, y - 10, face);
  for (int finger = -1; finger <= 1; ++finger) {
    const int toeX = x + finger * 6;
    pawOval(c, toeX, y, 4, 5, brown);
    pawOval(c, toeX - 1, y - 1, 2, 3, furLight);
    pawLine(c, toeX + 1, y + 3, toeX + 2, y + 8, mask, 1);
    pawLine(c, toeX, y + 3, toeX + 1, y + 8, cream, 1);
    pawLine(c, toeX + 1, y + 8, toeX - 1, y + 10, cream);
  }
}
}  // namespace

LeafSweepGame::LeafSweepGame() { reset(0, 0x1eaf5eedu); }

void LeafSweepGame::reset(uint32_t now, uint32_t seed) {
  state_ = LeafSweepSnapshot();
  random_ = seed ? seed : 0x1eaf5eedu;
  lastMs_ = now;
  accumulatedMs_ = coolingFraction_ = 0;
  canSpawnMs_ = 2000;
  events_ = 0;
  for (unsigned i = 0; i < kObjectCount; ++i) {
    state_.objects[i].type = i < kLeafSlots ? LeafSweepObjectType::Leaf : LeafSweepObjectType::Can;
    if (i < kLeafSlots && !spawn(i)) state_.objects[i].respawnMs = 200;
  }
}

uint32_t LeafSweepGame::randomBelow(uint32_t limit) {
  random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
  return static_cast<uint32_t>((static_cast<uint64_t>(random_) * limit) >> 32);
}

bool LeafSweepGame::spawn(unsigned index) {
  LeafSweepObject& object = state_.objects[index];
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    const int x = 27 + static_cast<int>(randomBelow(187));
    const int y = 93 + static_cast<int>(randomBelow(113));
    const int pawGap = object.type == LeafSweepObjectType::Can ? 32 : 26;
    if (state_.pawVisible && distanceSquared(x, y, state_.pawX, state_.pawY) < pawGap * pawGap) continue;
    bool clear = true;
    for (unsigned other = 0; other < kObjectCount; ++other) {
      if (other == index || !state_.objects[other].active) continue;
      const auto& neighbor = state_.objects[other];
      const int gap = object.type == LeafSweepObjectType::Can || neighbor.type == LeafSweepObjectType::Can ? 27 : 20;
      if (distanceSquared(x, y, neighbor.x, neighbor.y) < gap * gap) { clear = false; break; }
    }
    if (!clear) continue;
    object.active = true; object.x = static_cast<int16_t>(x); object.y = static_cast<int16_t>(y);
    object.ageMs = object.respawnMs = 0;
    object.variant = static_cast<uint8_t>(randomBelow(4));
    return true;
  }
  return false;
}

void LeafSweepGame::updateScore() {
  const uint64_t denominator = static_cast<uint64_t>(state_.elapsedMs) + 20000u;
  const uint64_t score = static_cast<uint64_t>(state_.leafCount) * 100u * 60000u / denominator;
  state_.score = score > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(score);
  const uint64_t pace = 6000000u / denominator;
  state_.multiplier = static_cast<uint16_t>(pace ? pace : 1);
}

void LeafSweepGame::finish() {
  state_.phase = LeafSweepPhase::GameOver;
  clearTouch();
  updateScore();
  events_ |= LeafSweepGameOver;
}

void LeafSweepGame::sweep(int ax, int ay, int bx, int by) {
  struct Hit { float time; uint8_t index; } hits[kObjectCount];
  unsigned count = 0;
  for (unsigned i = 0; i < kObjectCount; ++i) {
    const auto& object = state_.objects[i];
    if (!object.active || object.ageMs < (object.type == LeafSweepObjectType::Leaf ? kLeafGrowthMs : kCanWarningMs)) continue;
    float time = 0;
    if (!contactTime(ax, ay, bx, by, object.x, object.y,
                     object.type == LeafSweepObjectType::Leaf ? kLeafRadius : kCanRadius, time)) continue;
    unsigned place = count;
    while (place && (hits[place - 1].time > time ||
        (hits[place - 1].time == time && object.type == LeafSweepObjectType::Can &&
         state_.objects[hits[place - 1].index].type == LeafSweepObjectType::Leaf))) {
      hits[place] = hits[place - 1]; --place;
    }
    hits[place] = Hit{time, static_cast<uint8_t>(i)};
    ++count;
  }
  for (unsigned hit = 0; hit < count && state_.phase == LeafSweepPhase::Playing; ++hit) {
    auto& object = state_.objects[hits[hit].index];
    object.active = false;
    if (object.type == LeafSweepObjectType::Leaf) {
      state_.leafCount = addSaturated(state_.leafCount, 1);
      object.respawnMs = 500;
      events_ |= LeafSweepCollect;
      if (state_.leafCount % 25 == 0) events_ |= LeafSweepRound;
    } else {
      state_.energy = static_cast<uint8_t>(state_.energy >= 75 ? 100 : state_.energy + 25);
      events_ |= LeafSweepCanHit;
      if (state_.energy == 100) finish();
    }
  }
  updateScore();
}

void LeafSweepGame::step() {
  if (state_.energy) {
    coolingFraction_ += kStepMs * 3;
    if (coolingFraction_ >= 1000) {
      const unsigned cooling = coolingFraction_ / 1000;
      coolingFraction_ %= 1000;
      state_.energy = static_cast<uint8_t>(state_.energy > cooling ? state_.energy - cooling : 0);
    }
  } else coolingFraction_ = 0;
  for (unsigned i = 0; i < kObjectCount; ++i) {
    auto& object = state_.objects[i];
    if (object.active) {
      const unsigned cap = object.type == LeafSweepObjectType::Leaf ? kLeafGrowthMs : kCanLifetimeMs;
      object.ageMs = static_cast<uint16_t>(object.ageMs + kStepMs < cap ? object.ageMs + kStepMs : cap);
      if (object.type == LeafSweepObjectType::Can && object.ageMs >= kCanLifetimeMs) object.active = false;
    } else if (object.type == LeafSweepObjectType::Leaf) {
      object.respawnMs = object.respawnMs > kStepMs ? object.respawnMs - kStepMs : 0;
      if (!object.respawnMs && !spawn(i)) object.respawnMs = 200;
    }
  }
  canSpawnMs_ = canSpawnMs_ > kStepMs ? canSpawnMs_ - kStepMs : 0;
  if (!canSpawnMs_) {
    bool spawned = false;
    for (unsigned i = kLeafSlots; i < kObjectCount; ++i)
      if (!state_.objects[i].active) { spawned = spawn(i); break; }
    canSpawnMs_ = spawned ? static_cast<uint16_t>(2000 + randomBelow(1001)) : 200;
  }
  if (state_.pawVisible) sweep(state_.pawX, state_.pawY, state_.pawX, state_.pawY);
}

void LeafSweepGame::advance(uint32_t now) {
  const uint32_t elapsed = now - lastMs_;
  lastMs_ = now;
  if (state_.phase != LeafSweepPhase::Playing) { accumulatedMs_ = 0; return; }
  // Pace always counts active wall time; hazard catch-up is bounded separately.
  state_.elapsedMs = addSaturated(state_.elapsedMs, elapsed);
  accumulatedMs_ += static_cast<uint16_t>(elapsed > kMaxCatchupMs ? kMaxCatchupMs : elapsed);
  while (accumulatedMs_ >= kStepMs && state_.phase == LeafSweepPhase::Playing) {
    accumulatedMs_ -= kStepMs;
    step();
  }
  if (state_.phase == LeafSweepPhase::Playing) updateScore();
}

void LeafSweepGame::resume(uint32_t now) { lastMs_ = now; accumulatedMs_ = 0; clearTouch(); }
void LeafSweepGame::clearTouch() { state_.pawVisible = false; }
uint8_t LeafSweepGame::takeEvents() { const uint8_t result = events_; events_ = 0; return result; }

void LeafSweepGame::touch(bool down, int x, int y, uint32_t now) {
  advance(now);
  if (!down || state_.phase != LeafSweepPhase::Playing) {
    clearTouch();
    return;
  }
  if (x < kFieldLeft || x >= kFieldRight || y < kFieldTop || y >= kFieldBottom) {
    if (state_.pawVisible) {
      int edgeX = 0, edgeY = 0;
      clipExit(state_.pawX, state_.pawY, x, y, edgeX, edgeY);
      sweep(state_.pawX, state_.pawY, edgeX, edgeY);
    }
    clearTouch();
    return;
  }
  const int fromX = state_.pawVisible ? state_.pawX : x;
  const int fromY = state_.pawVisible ? state_.pawY : y;
  state_.pawVisible = true; state_.pawX = static_cast<int16_t>(x); state_.pawY = static_cast<int16_t>(y);
  sweep(fromX, fromY, x, y);
}

void drawLeafSweep(uint16_t* pixels, const LeafSweepGame& game) {
  if (!pixels) return;
  using namespace graphics;
  const auto& s = game.snapshot();
  Canvas c{pixels};
  c.rect(0, 0, 240, 240, ink);
  c.roundRect(92, 8, 56, 16, 3, mint);
  c.text(105, 12, "PAUSE", ink);
  char text[32];
  snprintf(text, sizeof(text), "LEAF SWEEP / R%lu", static_cast<unsigned long>(s.leafCount / 25u + 1u));
  c.centered(30, text, cream);
  c.text(20, 44, "SCORE", muted);
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(s.score));
  c.text(20, 55, text, cream);
  c.text(103, 44, "PACE", muted);
  snprintf(text, sizeof(text), "%u.%02uX", s.multiplier / 100u, s.multiplier % 100u);
  c.text(103, 55, text, gold);
  c.text(174, 44, "LEAVES", muted);
  snprintf(text, sizeof(text), "%lu/25", static_cast<unsigned long>(s.leafCount % 25u));
  c.text(174, 55, text, mint);
  c.roundRect(173, 64, 44, 3, 1, rgb(48,74,57));
  c.rect(174, 65, static_cast<int>((s.leafCount % 25u) * 42 / 25u), 1, mint);
  c.text(20, 70, "ENERGY", muted);
  c.roundRect(62, 70, 126, 6, 2, rgb(48,74,57));
  const uint16_t energyColor = s.energy >= 75 ? rgb(226,132,112) : gold;
  c.rect(64, 72, s.energy * 122 / 100, 2, energyColor);
  snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(s.energy));
  c.text(194, 70, text, energyColor);
  c.line(20, 81, 220, 81, rgb(44,67,46));
  for (int row = 0; row < 6; ++row)
    c.rect(LeafSweepGame::kFieldLeft, 82 + row * 23, 212, row == 5 ? 21 : 23,
           rgb(17 + row, 38 + row * 2, 32 + row));
  // Low-contrast clumps suggest a forest floor without masquerading as pickups.
  for (int y = 100; y < 218; y += 32)
    for (int x = 32; x < 224; x += 40) {
      c.line(x, y + 2, x + 1, y - 1, rgb(35,61,39));
      c.dot(x - 2, y + 2, rgb(39,63,41));
    }
  for (const auto& object : s.objects) {
    if (object.active) {
      if (object.type == LeafSweepObjectType::Leaf) drawLeaf(c, object, s.elapsedMs);
      else drawCan(c, object);
    } else if (object.type == LeafSweepObjectType::Leaf && object.respawnMs > 160 && object.respawnMs <= 500) {
      const int spread = 3 + (500 - object.respawnMs) / 45;
      pawLine(c, object.x - spread - 1, object.y - 2, object.x - spread + 1, object.y - 2, mint);
      pawLine(c, object.x + spread, object.y - 5, object.x + spread, object.y - 3, gold);
      pawDot(c, object.x, object.y - spread, cream);
    }
  }
  if (s.elapsedMs < 6000) c.centered(225, "SWEEP LEAVES. DODGE CANS.", muted);
  if (s.pawVisible) drawPaw(c, s.pawX, s.pawY);
  if (s.phase == LeafSweepPhase::GameOver) {
    c.roundRect(30, 102, 182, 58, 6, rgb(8,25,23));
    c.roundRect(29, 99, 182, 58, 6, gold);
    c.roundRect(30, 100, 180, 56, 5, cream);
    c.centered(110, "TOO MUCH ENERGY!", ink);
    c.centered(139, "PRESS TO CONTINUE", rgb(77,96,63));
  }
}

}  // namespace sloth
