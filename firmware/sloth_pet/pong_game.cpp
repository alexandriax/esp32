#include "pong_game.h"
#include "pet_canvas.h"

#include <math.h>
#include <stdio.h>

namespace sloth {
#if __cplusplus < 201703L
// C++11/14 require definitions if a caller binds one of these constants by reference.
constexpr int PongGame::kCourtTop;
constexpr int PongGame::kCourtBottom;
constexpr int PongGame::kPaddleHalfHeight;
constexpr int PongGame::kPaddleMinY;
constexpr int PongGame::kPaddleMaxY;
constexpr int PongGame::kBallRadius;
constexpr int PongGame::kWinningScore;
constexpr int PongGame::kStepMs;
constexpr int PongGame::kMaxCatchupMs;
constexpr int PongGame::kCheerDurationMs;
#endif
namespace {
const float kStepSeconds = PongGame::kStepMs / 1000.0f;
const float kLeftFace = 19, kRightFace = 221;
const float kPaddleSpeeds[5] = {52, 72, 96, 124, 160};

float clamp(float value, float low, float high) {
  return value < low ? low : value > high ? high : value;
}

uint8_t boundedSpeed(uint8_t value) { return value < 1 ? 1 : value > 5 ? 5 : value; }

void pomPom(graphics::Canvas& c, int x, int y, uint16_t color, bool shake) {
  using namespace graphics;
  c.oval(x, y, 3, 3, color);
  c.line(x - 4, y, x + 4, y, color);
  c.line(x, y - 4, x, y + 4, color);
  c.line(x - 3, y - 3, x + 3, y + 3, color);
  c.line(x - 3, y + 3, x + 3, y - 3, color);
  c.dot(x - 1, y - 1, cream); c.dot(x + 1, y + 1, cream);
  if (shake) { c.dot(x - 5, y - 2, color); c.dot(x + 5, y + 2, color); }
}

void mossFace(graphics::Canvas& c, int x, int y, bool cheer, uint16_t elapsedMs) {
  using namespace graphics;
  const unsigned beat = elapsedMs / 120u;
  const bool leftUp = (beat / 2u) % 2u == 0;
  if (cheer && beat % 2u) --y;
  const int leftHandY = cheer ? y - (leftUp ? 9 : 5) : y + 10;
  const int rightHandY = cheer ? y - (leftUp ? 5 : 9) : y + 10;
  const uint16_t coral = rgb(235, 150, 118);
  // A little body, tucked arms and lowered pom-poms form the resting pose.
  // The scoring animation raises the hands vertically beside the face, keeping
  // the mascot compact and clear of both score labels and the court boundary.
  c.oval(x, y + 9, 8, 7, brown);
  c.oval(x, y + 11, 5, 4, furLight);
  c.line(x - 8, y + 6, x - 14, leftHandY, brown, 2);
  c.line(x + 8, y + 6, x + 14, rightHandY, brown, 2);
  c.oval(x, y, 13, 11, brown);
  c.oval(x, y + 1, 10, 8, face);
  c.line(x - 8, y - 2, x - 3, y + 2, mask, 2);
  c.line(x + 8, y - 2, x + 3, y + 2, mask, 2);
  c.dot(x - 5, y, cream); c.dot(x + 5, y, cream);
  c.oval(x, y + 2, 2, 1, mask);
  c.line(x - 2, y + 5, x, y + 6, mask);
  c.line(x, y + 6, x + 2, y + 5, mask);
  pomPom(c, x - 15, leftHandY, mint, cheer && beat % 2u);
  pomPom(c, x + 15, rightHandY, coral, cheer && beat % 2u == 0);
}

void leafPaddle(graphics::Canvas& c, int x, int y, uint16_t color, bool right) {
  using namespace graphics;
  c.roundRect(x + 1, y - PongGame::kPaddleHalfHeight + 2, 6,
              PongGame::kPaddleHalfHeight * 2 + 1, 2, rgb(7, 20, 19));
  c.roundRect(x, y - PongGame::kPaddleHalfHeight, 6,
              PongGame::kPaddleHalfHeight * 2 + 1, 2, furDark);
  c.rect(x + 1, y - 17, 4, 35, color);
  c.line(x + 1, y - 16, x + 1, y + 15, cream);
  c.rect(x + 3, y - 15, 1, 31, brown);
  // Leaves point outwards, leaving the inner collision face unambiguous.
  const int tip = right ? x + 9 : x - 3;
  c.triangle(x + 2, y - 9, tip, y - 15, tip, y - 8, color);
  c.triangle(x + 3, y + 9, tip, y + 3, tip, y + 10, color);
  c.line(x + 2, y - 10, tip, y - 13, rgb(196, 225, 163));
  c.line(x + 3, y + 8, tip, y + 5, rgb(196, 225, 163));
}
}  // namespace

PongGame::PongGame() : lastMs_(0), cheerSince_(0), accumulatedMs_(0), ballSpeed_(0) { reset(0); }

void PongGame::reset(uint32_t now) {
  const uint8_t left = state_.speed[0], right = state_.speed[1];
  state_ = PongSnapshot();
  setSpeeds(left, right);
  lastMs_ = now;
  cheerSince_ = now;
  accumulatedMs_ = 0;
  events_ = 0;
  prepareServe();
}

void PongGame::resume(uint32_t now) {
  lastMs_ = now;
  if (state_.cheering) cheerSince_ = now - state_.cheerElapsedMs;
}

uint16_t PongGame::takeEvents() {
  const uint16_t result = events_;
  events_ = 0;
  return result;
}

void PongGame::setSpeeds(uint8_t left, uint8_t right) {
  state_.speed[0] = boundedSpeed(left);
  state_.speed[1] = boundedSpeed(right);
}

void PongGame::prepareServe() {
  state_.phase = PongPhase::Ready;
  for (unsigned player = 0; player < 2; ++player) {
    state_.paddleY[player] = (kCourtTop + kCourtBottom) / 2.0f;
    state_.paddleDirection[player] = 0;
  }
  state_.ballX = 120;
  state_.ballY = (kCourtTop + kCourtBottom) / 2.0f;
  state_.ballVX = state_.ballVY = 0;
  ballSpeed_ = 0;
  accumulatedMs_ = 0;
}

void PongGame::press(unsigned player, uint32_t now) {
  if (player > 1) return;
  advance(now);
  if (state_.phase == PongPhase::MatchOver) return;
  if (state_.phase == PongPhase::Ready) {
    events_ |= PongServe;
    state_.phase = PongPhase::Playing;
    state_.ballVX = player == 0 ? 125.0f : -125.0f;
    state_.ballVY = (state_.score[0] + state_.score[1]) % 2 ? 48.0f : -48.0f;
    ballSpeed_ = sqrtf(state_.ballVX * state_.ballVX + state_.ballVY * state_.ballVY);
    return;
  }
  int8_t& direction = state_.paddleDirection[player];
  direction = direction ? -direction : state_.paddleY[player] >= kPaddleMaxY ? -1 : 1;
}

void PongGame::advance(uint32_t now) {
  const uint32_t elapsed = now - lastMs_;  // Correct across millis() wraparound.
  lastMs_ = now;
  if (state_.cheering) {
    const uint32_t cheerElapsed = now - cheerSince_;
    state_.cheering = cheerElapsed < kCheerDurationMs;
    state_.cheerElapsedMs = state_.cheering ? static_cast<uint16_t>(cheerElapsed) : 0;
  }
  if (state_.phase != PongPhase::Playing) {
    accumulatedMs_ = 0;
    return;
  }
  // Rendering stalls never cause an unbounded simulation loop or skipped rally.
  // Discard excess wall time intentionally instead of teleporting after a pause.
  accumulatedMs_ += static_cast<uint16_t>(elapsed > kMaxCatchupMs ? kMaxCatchupMs : elapsed);
  while (accumulatedMs_ >= kStepMs && state_.phase == PongPhase::Playing) {
    accumulatedMs_ -= kStepMs;
    step();
  }
}

void PongGame::score(unsigned player) {
  events_ |= PongPoint;
  ++state_.score[player];
  prepareServe();
  state_.cheering = true;
  state_.cheerElapsedMs = 0;
  cheerSince_ = lastMs_;
  if (state_.score[player] >= kWinningScore) {
    state_.phase = PongPhase::MatchOver;
    state_.winner = static_cast<int8_t>(player);
    events_ |= PongGameOver;
  }
}

void PongGame::step() {
  const float previousPaddle[2] = {state_.paddleY[0], state_.paddleY[1]};
  for (unsigned player = 0; player < 2; ++player) {
    state_.paddleY[player] = clamp(state_.paddleY[player] +
        state_.paddleDirection[player] * kPaddleSpeeds[state_.speed[player] - 1] * kStepSeconds,
        kPaddleMinY, kPaddleMaxY);
    if (state_.paddleY[player] <= kPaddleMinY || state_.paddleY[player] >= kPaddleMaxY)
      state_.paddleDirection[player] = 0;
  }

  // Resolve earliest swept collisions. This handles a wall and paddle in one
  // step and avoids tunnelling even at the maximum rally speed. Paddle contact
  // uses its interpolated position at collision time, not the end-of-step pose.
  float remaining = kStepSeconds, consumed = 0;
  for (unsigned event = 0; event < 4 && remaining > 0; ++event) {
    enum Hit { None, Wall, LeftPaddle, RightPaddle, LeftGoal, RightGoal } hit = None;
    float earliest = remaining;
    const auto consider = [&](float time, Hit candidate) {
      if (time >= 0 && time <= earliest) { earliest = time; hit = candidate; }
    };
    if (state_.ballVY < 0)
      consider((kCourtTop + kBallRadius - state_.ballY) / state_.ballVY, Wall);
    else if (state_.ballVY > 0)
      consider((kCourtBottom - kBallRadius - state_.ballY) / state_.ballVY, Wall);

    const unsigned receiver = state_.ballVX < 0 ? 0 : 1;
    const float face = receiver == 0 ? kLeftFace : kRightFace;
    const float crossing = (face - state_.ballX) / state_.ballVX;
    if (crossing >= 0 && crossing <= remaining) {
      const float impactY = state_.ballY + state_.ballVY * crossing;
      const float paddleY = previousPaddle[receiver] +
          (state_.paddleY[receiver] - previousPaddle[receiver]) *
          ((consumed + crossing) / kStepSeconds);
      if (fabsf(impactY - paddleY) <= kPaddleHalfHeight + kBallRadius)
        consider(crossing, receiver == 0 ? LeftPaddle : RightPaddle);
    }
    consider(((receiver == 0 ? -kBallRadius : 240 + kBallRadius) - state_.ballX) /
             state_.ballVX, receiver == 0 ? LeftGoal : RightGoal);

    state_.ballX += state_.ballVX * earliest;
    state_.ballY += state_.ballVY * earliest;
    remaining -= earliest;
    consumed += earliest;
    if (hit == None) break;
    if (hit == LeftGoal || hit == RightGoal) {
      score(hit == LeftGoal ? 1 : 0);
      return;
    }
    if (hit == Wall) {
      events_ |= PongWall;
      state_.ballY = clamp(state_.ballY, kCourtTop + kBallRadius, kCourtBottom - kBallRadius);
      state_.ballVY = -state_.ballVY;
    } else {
      events_ |= PongPaddle;
      const unsigned player = hit == LeftPaddle ? 0 : 1;
      const float paddleY = previousPaddle[player] +
          (state_.paddleY[player] - previousPaddle[player]) * (consumed / kStepSeconds);
      const float offset = clamp((state_.ballY - paddleY) / kPaddleHalfHeight, -1, 1);
      ballSpeed_ = clamp(ballSpeed_ + 9, 0, 250);
      float vertical = offset * ballSpeed_ * 0.7f + state_.paddleDirection[player] * 10;
      vertical = clamp(vertical, -ballSpeed_ * 0.8f, ballSpeed_ * 0.8f);
      if (fabsf(vertical) < 20) vertical = state_.ballVY < 0 ? -20.0f : 20.0f;
      state_.ballVY = vertical;
      state_.ballVX = sqrtf(ballSpeed_ * ballSpeed_ - vertical * vertical) * (player == 0 ? 1 : -1);
    }
  }
}

void drawPong(uint16_t* pixels, const PongGame& game) {
  if (!pixels) return;
  using namespace graphics;
  const PongSnapshot& state = game.snapshot();
  Canvas c = {pixels};
  const uint16_t coral = rgb(235, 150, 118);
  c.rect(0, 0, 240, 240, ink);
  // Broad quiet bands add depth without reducing ball/paddle contrast.
  for (int row = 0; row < 8; ++row) {
    const int top = PongGame::kCourtTop + row * 23;
    c.rect(0, top, 240, 23, rgb(9 + row, 27 + row * 2, 25 + row));
  }
  for (int side = 0; side < 2; ++side) {
    const int x = side ? 200 : 40;
    for (int leaf = 0; leaf < 4; ++leaf) {
      const int y = 193 + leaf * 6;
      c.line(x, y + 4, x + (side ? -5 : 5), y - 1, rgb(30, 55, 42));
      c.line(x, y + 4, x + (side ? 4 : -4), y + 1, rgb(27, 50, 39));
    }
  }
  c.line(8, PongGame::kCourtTop, 232, PongGame::kCourtTop, rgb(64, 94, 68));
  c.line(8, PongGame::kCourtBottom, 232, PongGame::kCourtBottom, rgb(64, 94, 68));
  for (int y = 42; y < PongGame::kCourtBottom - 6; y += 12)
    c.rect(119, y, 2, 5, rgb(36, 59, 47));
  c.text(13, 14, "P1", mint); c.text(216, 14, "P2", coral);
  char number[4];
  snprintf(number, sizeof(number), "%u", static_cast<unsigned>(state.score[0]));
  c.text(50, 9, number, mint, 2);
  snprintf(number, sizeof(number), "%u", static_cast<unsigned>(state.score[1]));
  c.text(178, 9, number, coral, 2);
  // Seven small buds give the scores a visible path to the winning target.
  for (int i = 0; i < PongGame::kWinningScore; ++i) {
    c.rect(42 + i * 4, 27, 2, 2, i < state.score[0] ? mint : rgb(42, 61, 49));
    c.rect(171 + i * 4, 27, 2, 2, i < state.score[1] ? coral : rgb(42, 61, 49));
  }
  mossFace(c, 120, 15, state.cheering, state.cheerElapsedMs);
  leafPaddle(c, 10, static_cast<int>(state.paddleY[0] + 0.5f), mint, false);
  leafPaddle(c, 224, static_cast<int>(state.paddleY[1] + 0.5f), coral, true);

  if (state.phase == PongPhase::Playing) {
    const int x = static_cast<int>(state.ballX + 0.5f), y = static_cast<int>(state.ballY + 0.5f);
    for (int tail = 3; tail > 0; --tail) {
      const int tx = static_cast<int>(state.ballX - state.ballVX * tail * .025f);
      const int ty = static_cast<int>(state.ballY - state.ballVY * tail * .025f);
      if (ty > PongGame::kCourtTop + 3 && ty < PongGame::kCourtBottom - 3)
        c.oval(tx, ty, tail == 1 ? 2 : 1, tail == 1 ? 2 : 1,
               tail == 1 ? rgb(136, 125, 67) : rgb(72, 80, 48));
    }
    c.oval(x + 1, y + 1, 4, 4, rgb(8, 21, 19));
    c.oval(x, y, PongGame::kBallRadius, PongGame::kBallRadius, gold);
    c.dot(x + 2, y + 1, rgb(190, 119, 48));
    c.dot(x - 1, y - 1, cream);
    c.line(x, y - 3, x + 2, y - 5, mint);
    c.centered(228, "PRESS TO REVERSE  PWR: PAUSE", muted);
  } else {
    c.roundRect(31, 88, 180, 83, 7, rgb(6, 21, 20));
    c.roundRect(30, 85, 180, 83, 7, rgb(68, 94, 64));
    c.roundRect(31, 86, 178, 81, 6, rgb(21, 44, 36));
    c.centered(96, state.phase == PongPhase::MatchOver ?
        (state.winner == 0 ? "P1 WINS!" : "P2 WINS!") : "MOSS PONG", cream, 2);
    c.centered(119, state.phase == PongPhase::MatchOver ? "PRESS TO CONTINUE" : "PRESS EITHER BUTTON", mint);
    c.centered(133, state.phase == PongPhase::MatchOver ? "GOOD GAME, LITTLE SLOTHS" : "TO SERVE", mint);
    c.centered(151, "FIRST TO 7", muted);
    c.centered(228, "PWR: PAUSE", muted);
  }
  if (state.cheering) {
    // Deterministic leaf confetti belongs to the existing 1.2-second point cheer.
    for (int i = 0; i < 12; ++i) {
      const int x = 30 + (i * 37 + state.cheerElapsedMs / (17 + i % 3)) % 181;
      const int y = 39 + (i * 13 + state.cheerElapsedMs / 18) % 38;
      c.line(x, y, x + (i % 2 ? 2 : -2), y + 2, i % 3 ? mint : gold);
    }
  }
}

}  // namespace sloth
