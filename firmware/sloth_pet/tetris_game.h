#pragma once

#include <stdint.h>

namespace sloth {

constexpr int kTetrisColumns = 10;
constexpr int kTetrisRows = 20;
enum class TetrisPhase : uint8_t { Playing, GameOver };
enum class TetrisPiece : uint8_t { I, O, T, S, Z, J, L };
enum TetrisEvent : uint8_t {
  TetrisMove = 1u << 0, TetrisRotate = 1u << 1, TetrisLock = 1u << 2,
  TetrisLineClear = 1u << 3, TetrisGameOver = 1u << 4
};

struct TetrisSnapshot {
  uint8_t board[kTetrisRows][kTetrisColumns] = {};  // 0 empty; piece kind + 1.
  TetrisPhase phase = TetrisPhase::Playing;
  TetrisPiece piece = TetrisPiece::I, next = TetrisPiece::O;
  uint8_t rotation = 0;
  int8_t x = 3, y = -1, ghostY = -1;  // Origin of the piece's 4 x 4 mask.
  uint32_t score = 0, lines = 0;
  uint8_t level = 1, lastClearedLines = 0;
  bool grounded = false;
};

// Four rows of four bits, bit 0 = top-left. Invalid pieces return an empty mask.
uint16_t tetrisPieceMask(TetrisPiece piece, uint8_t rotation);

// A compact button/touch game. BOOT moves right; only the physical right edge
// wraps to the leftmost legal position. Interior collisions never jump blocks.
// KEY rotates clockwise with standard-shaped wall/floor kicks. PWR/menu/pause
// policy belongs to the caller; resume() deliberately discards paused wall time.
class TetrisGame {
 public:
  static constexpr int kLockDelayMs = 350;
  static constexpr int kMaxLockResets = 15;
  static constexpr int kStepMs = 10;
  static constexpr int kMaxCatchupMs = 200;

  TetrisGame();
  void reset(uint32_t now, uint32_t seed);
  bool moveLeft(uint32_t now);  // One column left, without wrapping.
  bool moveRight(uint32_t now, bool wrap = true);  // Touch passes false; BOOT retains edge wrapping.
  bool rotate(uint32_t now);
  // Lock the current piece at its ghost with normal line scoring. If elapsed
  // time already locked that piece, consume the call without dropping the next.
  bool hardDrop(uint32_t now);
  uint32_t pieceGeneration() const { return pieceSerial_; }
  void advance(uint32_t now);
  void resume(uint32_t now);
  const TetrisSnapshot& snapshot() const { return state_; }
  uint8_t takeEvents();

 private:
  TetrisSnapshot state_;
  uint32_t random_, lastMs_, pieceSerial_;
  uint16_t accumulatedMs_, gravityMs_, lockMs_;
  uint8_t bag_[7], bagIndex_, lockResets_, events_;

  uint32_t randomBelow(uint32_t limit);
  TetrisPiece drawPiece();
  bool fits(TetrisPiece piece, uint8_t rotation, int x, int y) const;
  void updateContact();
  void afterMove(bool wasGrounded);
  void spawn();
  void step();
  void lock();
  void finish();
  // Fixture adapter grants deterministic edge-case tests access without a
  // runtime board-mutation API or any extra state in the firmware.
  friend struct TetrisTestAccess;
};

void drawTetris(uint16_t* pixels, const TetrisGame& game, int pressed = 0, uint32_t now = 0);

}  // namespace sloth
