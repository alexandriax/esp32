#include "../firmware/sloth_pet/pong_game.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

using sloth::PongGame;
using sloth::PongPhase;
using sloth::PongSnapshot;

namespace {
bool near(float a, float b, float tolerance = 0.001f) { return fabsf(a - b) < tolerance; }

void tick(PongGame& game, uint32_t& now, unsigned duration) {
  assert(duration % PongGame::kStepMs == 0);
  for (unsigned elapsed = 0; elapsed < duration; elapsed += PongGame::kStepMs) {
    now += PongGame::kStepMs;
    game.advance(now);
  }
}

void checkBounds(const PongSnapshot& state) {
  for (unsigned player = 0; player < 2; ++player) {
    assert(state.paddleY[player] >= PongGame::kPaddleMinY);
    assert(state.paddleY[player] <= PongGame::kPaddleMaxY);
    assert(state.speed[player] >= 1 && state.speed[player] <= 5);
    assert(state.score[player] <= PongGame::kWinningScore);
    assert(state.paddleDirection[player] >= -1 && state.paddleDirection[player] <= 1);
  }
  assert(isfinite(state.ballX) && isfinite(state.ballY));
  assert(isfinite(state.ballVX) && isfinite(state.ballVY));
  assert(state.ballY >= PongGame::kCourtTop + PongGame::kBallRadius - 0.001f);
  assert(state.ballY <= PongGame::kCourtBottom - PongGame::kBallRadius + 0.001f);
  assert(state.ballX >= -PongGame::kBallRadius - 0.001f);
  assert(state.ballX <= 240 + PongGame::kBallRadius + 0.001f);
  assert(hypotf(state.ballVX, state.ballVY) <= 250.001f);
}

void controlTowards(PongGame& game, unsigned player, float y, uint32_t now) {
  const PongSnapshot& s = game.snapshot();
  const int desired = y > s.paddleY[player] ? 1 : -1;
  if (s.paddleDirection[player] != desired) {
    game.press(player, now);
    // Starting in mid-field goes down; a second press gives precise upwards control.
    if (game.snapshot().paddleDirection[player] != desired) game.press(player, now);
  }
}

void pointWithoutPaddles(PongGame& game, uint32_t& now, unsigned player = 0) {
  game.press(player, now);
  for (unsigned frame = 0; frame < 150 && game.snapshot().phase == PongPhase::Playing; ++frame)
    tick(game, now, 8);
  assert(game.snapshot().phase != PongPhase::Playing);
}

void checkScoreCheer() {
  PongGame game;
  uint32_t now = 0;
  assert(!game.snapshot().cheering && game.snapshot().cheerElapsedMs == 0);
  game.advance(now = 5000);
  assert(!game.snapshot().cheering);  // Initial Ready never starts an idle cheer.

  uint16_t resting[240 * 240], first[240 * 240], next[240 * 240];
  sloth::drawPong(resting, game);
  pointWithoutPaddles(game, now);
  const uint32_t scoredAt = now;
  assert(game.snapshot().cheering && game.snapshot().cheerElapsedMs == 0);
  sloth::drawPong(first, game);
  game.advance(now = scoredAt + 240);
  assert(game.snapshot().cheering && game.snapshot().cheerElapsedMs == 240);
  sloth::drawPong(next, game);
  unsigned changes = 0, mascotChanges = 0, confettiChanges = 0;
  for (unsigned y = 0; y < 240; ++y) {
    for (unsigned x = 0; x < 240; ++x) {
      if (first[y * 240 + x] != next[y * 240 + x]) {
        ++changes;
        const bool mascot = x >= 98 && x <= 141 && y < PongGame::kCourtTop;
        const bool confetti = x >= 28 && x <= 212 && y >= 39 && y <= 78;
        assert(mascot || confetti);
        confettiChanges += confetti;
      }
      if (x >= 98 && x <= 141 && y < PongGame::kCourtTop &&
          first[y * 240 + x] != resting[y * 240 + x]) ++mascotChanges;
    }
  }
  assert(changes > 0 && mascotChanges > 0 && confettiChanges > 0);
  game.advance(now = scoredAt + PongGame::kCheerDurationMs - 1);
  assert(game.snapshot().cheering);
  game.advance(now = scoredAt + PongGame::kCheerDurationMs);
  assert(!game.snapshot().cheering && game.snapshot().cheerElapsedMs == 0);
  sloth::drawPong(next, game);
  for (unsigned y = 0; y < PongGame::kCourtTop; ++y)
    for (unsigned x = 98; x <= 141; ++x)
      assert(next[y * 240 + x] == resting[y * 240 + x]);

  // Serving during the animation preserves its point-based timer. Another point
  // restarts the short cheer; it is not triggered by any ordinary button press.
  game.press(1, now);
  assert(!game.snapshot().cheering);
  for (unsigned frame = 0; frame < 150 && game.snapshot().phase == PongPhase::Playing; ++frame)
    tick(game, now, 8);
  assert(game.snapshot().cheering && game.snapshot().cheerElapsedMs == 0);
  game.press(0, now += 80);
  assert(game.snapshot().phase == PongPhase::Playing);
  assert(game.snapshot().cheering && game.snapshot().cheerElapsedMs == 80);
  for (unsigned frame = 0; frame < 150 && game.snapshot().phase == PongPhase::Playing; ++frame)
    tick(game, now, 8);
  assert(game.snapshot().cheering && game.snapshot().cheerElapsedMs == 0);
  game.reset(now);
  assert(!game.snapshot().cheering && game.snapshot().cheerElapsedMs == 0);

  // The wall-clock timer works across millis wrap and expires after a long stall.
  now = UINT32_MAX - 1500;
  game.reset(now);
  pointWithoutPaddles(game, now, 1);
  const uint32_t wrapScore = now;
  game.advance(now = wrapScore + PongGame::kCheerDurationMs - 1);
  assert(game.snapshot().cheering && game.snapshot().cheerElapsedMs == PongGame::kCheerDurationMs - 1);
  game.advance(now = wrapScore + PongGame::kCheerDurationMs);
  assert(!game.snapshot().cheering);
  pointWithoutPaddles(game, now, 1);
  game.advance(now += 3600000);
  assert(!game.snapshot().cheering);

  // Match-ending points cheer once, then rest on the winner screen indefinitely.
  game.reset(now);
  for (unsigned point = 0; point < PongGame::kWinningScore; ++point)
    pointWithoutPaddles(game, now);
  assert(game.snapshot().phase == PongPhase::MatchOver && game.snapshot().cheering);
  game.advance(now += PongGame::kCheerDurationMs);
  assert(game.snapshot().phase == PongPhase::MatchOver && !game.snapshot().cheering);
  game.press(0, now);
  assert(game.snapshot().phase == PongPhase::MatchOver && !game.snapshot().cheering);
}

void checkPauseAndEvents() {
  PongGame game;
  assert(game.takeEvents() == 0);
  game.press(3, 1000);
  assert(game.takeEvents() == 0);
  uint32_t now = UINT32_MAX - 100;
  game.reset(now);
  game.press(0, now);
  assert(game.takeEvents() == sloth::PongServe);
  assert(game.takeEvents() == 0);
  game.advance(now += 5);  // Preserve the partial fixed step during a pause.
  const PongSnapshot before = game.snapshot();
  game.resume(now += 60000);
  game.advance(now);
  assert(memcmp(&before, &game.snapshot(), sizeof(before)) == 0);
  assert(game.takeEvents() == 0);
  game.advance(now += 3);
  assert(game.snapshot().ballX > before.ballX);
  assert(game.takeEvents() == 0);
  while (game.snapshot().phase == PongPhase::Playing) tick(game, now, 8);
  assert(game.takeEvents() == sloth::PongPoint);
  assert(game.takeEvents() == 0);
  game.advance(now += 240);
  assert(game.snapshot().cheerElapsedMs == 240);
  const PongSnapshot cheering = game.snapshot();
  game.resume(now += 3600000);
  game.advance(now);
  assert(memcmp(&cheering, &game.snapshot(), sizeof(cheering)) == 0);
  game.advance(now += 959);
  assert(game.snapshot().cheering && game.snapshot().cheerElapsedMs == 1199);
  game.advance(++now);
  assert(!game.snapshot().cheering);
  for (unsigned point = 1; point < PongGame::kWinningScore; ++point) {
    pointWithoutPaddles(game, now);
    const uint16_t expected = sloth::PongServe | sloth::PongPoint |
        (point == PongGame::kWinningScore - 1 ? sloth::PongGameOver : 0);
    assert(game.takeEvents() == expected);
  }
  assert(game.snapshot().winner == 0);
  const PongSnapshot finished = game.snapshot();
  game.press(0, now); game.press(1, now);
  assert(memcmp(&finished, &game.snapshot(), sizeof(finished)) == 0);
  assert(game.takeEvents() == 0);
  game.reset(now);
  assert(game.takeEvents() == 0);
  assert(game.snapshot().phase == PongPhase::Ready);
}
}  // namespace

int main() {
  static_assert(sizeof(PongGame) < 100, "Pong state must stay small on ESP32-C6");
  checkScoreCheer();
  checkPauseAndEvents();
  PongGame game;
  uint32_t now = 0;
  assert(game.snapshot().phase == PongPhase::Ready);
  assert(game.snapshot().speed[0] == 3 && game.snapshot().speed[1] == 3);
  game.setSpeeds(0, 255);
  assert(game.snapshot().speed[0] == 1 && game.snapshot().speed[1] == 5);
  game.press(2, 100);
  assert(game.snapshot().phase == PongPhase::Ready);
  game.reset(0);
  assert(game.snapshot().speed[0] == 1 && game.snapshot().speed[1] == 5);

  game.press(0, now);
  assert(game.snapshot().phase == PongPhase::Playing && game.snapshot().ballVX > 0);
  assert(game.snapshot().paddleDirection[0] == 0);
  game.press(0, now); game.press(1, now);
  tick(game, now, 80);
  assert(near(game.snapshot().paddleY[0], 127 + 52 * 0.080f));
  assert(near(game.snapshot().paddleY[1], 127 + 160 * 0.080f));
  const float beforeReverse = game.snapshot().paddleY[1];
  game.press(1, now);
  assert(game.snapshot().paddleDirection[1] == -1);
  tick(game, now, 80);
  assert(game.snapshot().paddleY[1] < beforeReverse);
  assert(near(game.snapshot().paddleY[1], 127));
  game.press(1, now);  // Down, then stop at bottom.
  tick(game, now, 464);
  assert(near(game.snapshot().paddleY[1], PongGame::kPaddleMaxY));
  assert(game.snapshot().paddleDirection[1] == 0);
  tick(game, now, 40);
  assert(near(game.snapshot().paddleY[1], PongGame::kPaddleMaxY));
  game.press(1, now);
  tick(game, now, 80);
  assert(game.snapshot().paddleY[1] < PongGame::kPaddleMaxY);
  assert(game.snapshot().paddleDirection[1] == -1);

  now = 0; game.reset(now); game.press(0, now);
  game.press(1, now); game.press(1, now);
  tick(game, now, 464);
  assert(near(game.snapshot().paddleY[1], PongGame::kPaddleMinY));
  assert(game.snapshot().paddleDirection[1] == 0);
  tick(game, now, 40);
  assert(near(game.snapshot().paddleY[1], PongGame::kPaddleMinY));
  game.press(1, now); tick(game, now, 80);
  assert(game.snapshot().paddleY[1] > PongGame::kPaddleMinY);
  assert(game.snapshot().paddleDirection[1] == 1);

  // Serving from the other player reverses horizontal direction. A miss scores
  // once and leaves a stable ready screen until either player deliberately serves.
  now = 0; game.reset(now); game.press(1, now);
  assert(game.snapshot().ballVX < 0);
  tick(game, now, 1000);
  assert(game.snapshot().phase == PongPhase::Ready);
  assert(game.snapshot().score[1] == 1 && game.snapshot().score[0] == 0);
  tick(game, now, 8000);
  assert(game.snapshot().score[1] == 1);
  assert(game.snapshot().ballVX == 0 && game.snapshot().ballVY == 0);
  for (unsigned point = 2; point <= PongGame::kWinningScore; ++point) {
    game.press(1, now); tick(game, now, 1000);
    assert(game.snapshot().score[1] == point);
  }
  assert(game.snapshot().phase == PongPhase::MatchOver && game.snapshot().winner == 1);
  game.press(0, now);
  assert(game.snapshot().phase == PongPhase::MatchOver && game.snapshot().winner == 1);
  assert(game.snapshot().score[0] == 0 && game.snapshot().score[1] == 7);
  assert(game.snapshot().speed[0] == 1 && game.snapshot().speed[1] == 5);

  // A correctly timed right paddle returns the first serve. It crosses the
  // collision plane between physics samples rather than requiring exact x equality.
  now = 0; game.reset(now); game.setSpeeds(3, 3); game.press(0, now);
  tick(game, now, 400);
  assert(game.takeEvents() == sloth::PongServe);
  game.press(1, now); game.press(1, now);
  tick(game, now, 416);
  assert(game.snapshot().ballVX < 0);
  assert(game.snapshot().score[0] == 0 && game.snapshot().score[1] == 0);
  assert(game.snapshot().ballX < 221);
  assert(game.takeEvents() == sloth::PongPaddle);
  assert(game.takeEvents() == 0);

  // Fixed-step results are independent of a normal 16/64 ms render cadence.
  PongGame fast, slow;
  fast.press(0, 0); slow.press(0, 0);
  fast.press(0, 0); slow.press(0, 0);
  for (uint32_t t = 8; t <= 512; t += 8) fast.advance(t);
  for (uint32_t t = 64; t <= 512; t += 64) slow.advance(t);
  assert(near(fast.snapshot().ballX, slow.snapshot().ballX));
  assert(near(fast.snapshot().ballY, slow.snapshot().ballY));
  assert(near(fast.snapshot().paddleY[0], slow.snapshot().paddleY[0]));
  fast.reset(0); slow.reset(0); fast.press(0, 0); slow.press(0, 0);
  fast.advance(PongGame::kMaxCatchupMs); slow.advance(3600000);
  assert(near(fast.snapshot().ballX, slow.snapshot().ballX));
  assert(near(fast.snapshot().ballY, slow.snapshot().ballY));

  // Accumulated sub-step time and unsigned millis wrap preserve the same state.
  fast.reset(0); slow.reset(UINT32_MAX - 31);
  fast.press(0, 0); slow.press(0, UINT32_MAX - 31);
  for (uint32_t t = 1; t <= 100; ++t) {
    fast.advance(t); slow.advance((UINT32_MAX - 31) + t);
  }
  assert(near(fast.snapshot().ballX, slow.snapshot().ballX));
  assert(near(fast.snapshot().ballY, slow.snapshot().ballY));

  // Long rallies exercise both paddle faces, corners, top/bottom reflection and
  // growing ball speed using legal controls. Every frame remains finite/in bounds.
  now = 0; game.reset(now); game.setSpeeds(5, 5); game.press(0, now);
  unsigned paddleReturns = 0, wallReturns = 0, scored = 0;
  uint16_t rallyEvents = 0;
  for (unsigned frame = 0; frame < 30000; ++frame) {
    const PongSnapshot old = game.snapshot();
    if (old.phase == PongPhase::MatchOver) game.reset(now);
    if (old.phase != PongPhase::Playing) { ++scored; game.press(frame % 2, now); }
    for (unsigned player = 0; player < 2; ++player)
      controlTowards(game, player, game.snapshot().ballY, now);
    const PongSnapshot before = game.snapshot();
    tick(game, now, 8);
    rallyEvents |= game.takeEvents();
    const PongSnapshot& after = game.snapshot();
    checkBounds(after);
    if (after.phase == PongPhase::Playing) {
      if (before.ballVX * after.ballVX < 0) ++paddleReturns;
      if (before.ballVY * after.ballVY < 0 && before.ballVX * after.ballVX > 0) ++wallReturns;
    }
  }
  assert(paddleReturns > 20 && wallReturns > 20);
  assert((rallyEvents & (sloth::PongServe | sloth::PongPaddle | sloth::PongWall)) ==
         (sloth::PongServe | sloth::PongPaddle | sloth::PongWall));
  (void)scored;

  // Renderer preserves state and never touches the surrounding guard pixels.
  uint16_t guarded[240 * 240 + 2];
  guarded[0] = 0x1234; guarded[240 * 240 + 1] = 0x5678;
  const PongSnapshot beforeDraw = game.snapshot();
  sloth::drawPong(nullptr, game);
  sloth::drawPong(guarded + 1, game);
  assert(memcmp(&beforeDraw, &game.snapshot(), sizeof(beforeDraw)) == 0);
  assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0x5678);
  now = 0; game.reset(now);
  sloth::drawPong(guarded + 1, game);
  for (unsigned point = 0; point < PongGame::kWinningScore; ++point) {
    game.press(0, now); tick(game, now, 1000);
    sloth::drawPong(guarded + 1, game);
    assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0x5678);
  }
  assert(game.snapshot().phase == PongPhase::MatchOver);
  printf("Pong checks passed: serves, reversal, ends, independent speeds, scoring, first-to-7, stable final score, swept returns (%u), walls (%u), fixed-step timing, pause/resume, drained event bits, stalls, wraparound, 1200ms score-only cheer, animated HUD bounds, render bounds\n", paddleReturns, wallReturns);
}
