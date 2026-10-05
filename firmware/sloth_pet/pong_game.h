#pragma once

#include <stdint.h>

namespace sloth {

enum class PongPhase : uint8_t { Ready, Playing, MatchOver };
enum PongEvent : uint16_t {
  PongServe = 1u << 0, PongPaddle = 1u << 1, PongWall = 1u << 2,
  PongPoint = 1u << 3, PongGameOver = 1u << 4
};

struct PongSnapshot {
  PongPhase phase = PongPhase::Ready;
  float paddleY[2] = {127, 127};  // Paddle centers in logical 240 x 240 pixels.
  int8_t paddleDirection[2] = {0, 0};  // -1 up, +1 down, 0 stopped at an end.
  uint8_t speed[2] = {3, 3};
  uint8_t score[2] = {0, 0};
  float ballX = 120, ballY = 127, ballVX = 0, ballVY = 0;
  int8_t winner = -1;
  bool cheering = false;
  uint16_t cheerElapsedMs = 0;  // Zero at a point, advances independently of play.
};

// Portable, allocation-free two-button Pong. The front-left BOOT button is
// player 0; front-right KEY is player 1. Either player may serve, towards their
// opponent. During a rally, a press starts a stopped paddle towards the other
// end, or reverses a moving one. Paddles stop at the ends until pressed again.
class PongGame {
 public:
  static constexpr int kCourtTop = 35;
  static constexpr int kCourtBottom = 219;
  static constexpr int kPaddleHalfHeight = 19;
  static constexpr int kPaddleMinY = kCourtTop + kPaddleHalfHeight;
  static constexpr int kPaddleMaxY = kCourtBottom - kPaddleHalfHeight;
  static constexpr int kBallRadius = 3;
  static constexpr int kWinningScore = 7;
  static constexpr int kStepMs = 8;
  static constexpr int kMaxCatchupMs = 128;
  static constexpr int kCheerDurationMs = 1200;

  PongGame();
  void reset(uint32_t now);  // Fresh match; retains independently set speeds.
  void setSpeeds(uint8_t left, uint8_t right);  // Each clamps to 1..5.
  void press(unsigned player, uint32_t now);  // Invalid players are ignored.
  void advance(uint32_t now);
  void resume(uint32_t now);  // Rebase clocks after pause; preserve frozen state.
  uint16_t takeEvents();  // Drain accumulated sound/event bits, once per frame.
  const PongSnapshot& snapshot() const { return state_; }

 private:
  PongSnapshot state_;
  uint32_t lastMs_;
  uint32_t cheerSince_;
  uint16_t accumulatedMs_;
  uint16_t events_;
  float ballSpeed_;
  void prepareServe();
  void score(unsigned player);
  void step();
};

// Complete logical RGB565 frame, using the shared pixel font and forest palette.
// Does not allocate, mutate the game, or call hardware APIs.
void drawPong(uint16_t* pixels, const PongGame& game);

}  // namespace sloth
