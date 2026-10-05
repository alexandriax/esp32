#include "../firmware/sloth_pet/tetris_controls.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

using namespace sloth;
namespace {
int cx(TetrisAction a) { const auto r = tetrisButtonRect(a); return r.x + r.width / 2; }
int cy(TetrisAction a) { const auto r = tetrisButtonRect(a); return r.y + r.height / 2; }
TetrisAction tap(TetrisControls& c, TetrisAction a) {
  assert(c.touch(true, cx(a), cy(a)) == TetrisAction::None);
  assert(c.pressed() == a);
  return c.touch(false, 0, 0); // Release coordinates are not meaningful.
}
void geometry() {
  for (unsigned action = 1; action <= 4; ++action) {
    const auto a = static_cast<TetrisAction>(action);
    const auto r = tetrisButtonRect(a);
    assert(r.width >= 44 && r.height >= 40);
    assert(r.x >= tetris_layout::kBoardLeft + 80 + 8 && r.x + r.width <= 224);
    assert(r.y >= 120 && r.y + r.height <= 224);
    for (int y = r.y; y < r.y + r.height; ++y) for (int x = r.x; x < r.x + r.width; ++x)
      assert(tetrisHitTest(x, y) == a);
    assert(tetrisHitTest(r.x - 1, cy(a)) == TetrisAction::None);
    assert(tetrisHitTest(r.x + r.width, cy(a)) == TetrisAction::None);
    assert(tetrisHitTest(cx(a), r.y - 1) == TetrisAction::None);
    assert(tetrisHitTest(cx(a), r.y + r.height) == TetrisAction::None);
    for (unsigned other = action + 1; other <= 4; ++other) {
      const auto b = tetrisButtonRect(static_cast<TetrisAction>(other));
      const bool separateX = b.x >= r.x + r.width || r.x >= b.x + b.width;
      const bool separateY = b.y >= r.y + r.height || r.y >= b.y + b.height;
      assert(separateX || separateY);
      if (separateX) assert(b.x >= r.x + r.width + 10 || r.x >= b.x + b.width + 10);
      if (separateY) assert(b.y >= r.y + r.height + 10 || r.y >= b.y + b.height + 10);
    }
  }
  for (int x = 160; x < 172; ++x) for (int y = 126; y < 222; ++y)
    assert(tetrisHitTest(x, y) == TetrisAction::None);
  for (int y = 168; y < 180; ++y) for (int x = 112; x < 220; ++x)
    assert(tetrisHitTest(x, y) == TetrisAction::None);
  for (int y = 56; y < 216; ++y) for (int x = 24; x < 104; ++x)
    assert(tetrisHitTest(x, y) == TetrisAction::None);
  assert(tetrisHitTest(20, 8) == TetrisAction::Right && tetrisHitTest(85, 23) == TetrisAction::Right);
  assert(tetrisHitTest(86, 8) == TetrisAction::Pause && tetrisHitTest(153, 23) == TetrisAction::Pause);
  assert(tetrisHitTest(154, 8) == TetrisAction::Rotate && tetrisHitTest(219, 23) == TetrisAction::Rotate);
  assert(tetrisHitTest(19, 8) == TetrisAction::None && tetrisHitTest(220, 23) == TetrisAction::None);
  assert(tetrisHitTest(100, 7) == TetrisAction::None && tetrisHitTest(100, 24) == TetrisAction::None);
  const int xs[] = {-1, 240}, ys[] = {-1, 0, 239, 240};
  for (int x : xs) for (int y : ys) assert(tetrisHitTest(x, y) == TetrisAction::None);
}
void firstReleaseAndHold() {
  TetrisControls c;
  assert(c.touch(true, cx(TetrisAction::Drop), cy(TetrisAction::Drop)) == TetrisAction::None);
  assert(c.pressed() == TetrisAction::None);
  assert(c.touch(false, 0, 0) == TetrisAction::None); // Initial screen-entry guard.
  for (unsigned action = 1; action <= 4; ++action) {
    const auto a = static_cast<TetrisAction>(action);
    assert(c.touch(true, cx(a), cy(a)) == TetrisAction::None && c.pressed() == a);
    for (unsigned sample = 0; sample < 500; ++sample)
      assert(c.touch(true, cx(a), cy(a)) == TetrisAction::None);
    assert(c.touch(false, 0, 0) == a && c.pressed() == TetrisAction::None);
    assert(c.touch(false, 0, 0) == TetrisAction::None);
  }
  // No artificial debounce: a different button can follow the first release.
  for (unsigned cycle = 0; cycle < 20; ++cycle) {
    assert(tap(c, TetrisAction::Left) == TetrisAction::Left);
    assert(tap(c, TetrisAction::Rotate) == TetrisAction::Rotate);
    assert(tap(c, TetrisAction::Drop) == TetrisAction::Drop);
    assert(tap(c, TetrisAction::Right) == TetrisAction::Right);
  }
  c.touch(true, 120, 14); assert(c.pressed() == TetrisAction::Pause);
  assert(c.touch(false, 0, 0) == TetrisAction::Pause);
}
void cancelAndReset() {
  TetrisControls c; c.touch(false, 0, 0);
  const int x = cx(TetrisAction::Drop), y = cy(TetrisAction::Drop);
  c.touch(true, x, y); c.touch(true, x + 11, y); c.touch(true, x, y);
  assert(c.pressed() == TetrisAction::None && c.touch(false, 0, 0) == TetrisAction::None);
  c.touch(true, x, y); c.touch(true, 165, y); c.touch(true, x, y); // Out and back through a gap.
  assert(c.touch(false, 0, 0) == TetrisAction::None);
  c.touch(true, 165, y); c.touch(true, x, y); // Gap-to-button never acquires a new target.
  assert(c.touch(false, 0, 0) == TetrisAction::None);
  c.touch(true, cx(TetrisAction::Left), cy(TetrisAction::Left)); c.touch(true, x, y);
  assert(c.pressed() == TetrisAction::None && c.touch(false, 0, 0) == TetrisAction::None);
  c.touch(true, x, y); c.touch(true, -1, -1); c.touch(true, x, y);
  assert(c.touch(false, 0, 0) == TetrisAction::None);
  c.touch(true, x, y); c.touch(true, x + 6, y + 6); // Small contact jitter survives.
  assert(c.touch(false, 0, 0) == TetrisAction::Drop);
  c.touch(true, x, y); c.reset();
  assert(c.pressed() == TetrisAction::None);
  c.touch(true, x, y); assert(c.touch(false, 0, 0) == TetrisAction::None);
  assert(tap(c, TetrisAction::Drop) == TetrisAction::Drop); // Exactly one global release rearms it.
}
void physicalShortAndHold() {
  TetrisMoveButton c;
  assert(c.update(true, 0, 1) == TetrisAction::None); // Held across entry.
  assert(c.update(true, 1000, 1) == TetrisAction::None);
  assert(c.update(false, 1010, 1) == TetrisAction::None);
  uint32_t now = 2000;
  for (uint32_t duration : {0u, 50u, 200u, 649u}) {
    assert(c.update(true, now, 1) == TetrisAction::None);
    assert(c.update(false, now + duration, 1) == TetrisAction::Right);
    assert(c.update(false, now + duration + 1, 1) == TetrisAction::None);
    now += 2000;
  }
  assert(c.update(true, now, 1) == TetrisAction::None);
  for (uint32_t t = 1; t < 650; ++t) assert(c.update(true, now+t, 1) == TetrisAction::None);
  assert(c.update(true, now+650, 1) == TetrisAction::Drop);
  assert(c.update(true, now+2000, 2) == TetrisAction::None);
  assert(c.update(false, now+2001, 2) == TetrisAction::None);
  now += 4000;
  c.update(true, now, 2);
  assert(c.update(false, now+700, 2) == TetrisAction::Drop); // Release after last poll.
  c.update(true, now+1000, 2);
  assert(c.update(true, now+1200, 3) == TetrisAction::None); // Gravity spawned next.
  assert(c.update(true, now+1800, 3) == TetrisAction::None);
  assert(c.update(false, now+2000, 3) == TetrisAction::None);
  c.update(true, now+3000, 3);
  assert(c.update(true, now+3300, 3, false) == TetrisAction::None); // Pause cancels.
  assert(c.update(true, now+6000, 3) == TetrisAction::None);
  assert(c.update(false, now+6010, 3) == TetrisAction::None);
  const uint32_t wrap = UINT32_MAX-400;
  assert(c.update(true, wrap, 4) == TetrisAction::None);
  assert(c.update(true, wrap+650u, 4) == TetrisAction::Drop);
  assert(c.update(false, wrap+700u, 5) == TetrisAction::None);
  // The debounced edge can be latched during display DMA before gravity spawns
  // another piece. Keep that edge's time/generation, not the later poll's.
  c.press(100, 10);
  assert(c.update(true, 130, 11) == TetrisAction::None);
  assert(c.update(true, 900, 11) == TetrisAction::None);
  assert(c.update(false, 950, 11) == TetrisAction::None);
  c.press(1000, 11);
  assert(c.update(false, 1100, 11) == TetrisAction::Right); // Full tap during DMA.
}
}
int main() {
  geometry(); firstReleaseAndHold(); cancelAndReset(); physicalShortAndHold();
  puts("tetris_controls: touch targets/cancellation, release-only hardware move, single hold drop, deferred-edge generation, pause/wake and wrap passed");
}
