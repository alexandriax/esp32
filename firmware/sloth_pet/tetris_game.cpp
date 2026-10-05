#include "tetris_game.h"

namespace sloth {
#if __cplusplus < 201703L
constexpr int TetrisGame::kLockDelayMs;
constexpr int TetrisGame::kMaxLockResets;
constexpr int TetrisGame::kStepMs;
constexpr int TetrisGame::kMaxCatchupMs;
#endif
namespace {
const uint16_t kMasks[7][4] = {
  {0x00f0, 0x4444, 0x0f00, 0x2222},
  {0x0066, 0x0066, 0x0066, 0x0066},
  {0x0072, 0x0262, 0x0270, 0x0232},
  {0x0036, 0x0462, 0x0360, 0x0231},
  {0x0063, 0x0264, 0x0630, 0x0132},
  {0x0071, 0x0226, 0x0470, 0x0322},
  {0x0074, 0x0622, 0x0170, 0x0223}
};
// Clockwise SRS-shaped kick candidates, with screen Y increasing downwards.
const int8_t kKicks[4][5][2] = {
  {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}},
  {{0,0},{1,0},{1,1},{0,-2},{1,-2}},
  {{0,0},{1,0},{1,-1},{0,2},{1,2}},
  {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}}
};
const int8_t kIKicks[4][5][2] = {
  {{0,0},{-2,0},{1,0},{-2,1},{1,-2}},
  {{0,0},{-1,0},{2,0},{-1,-2},{2,1}},
  {{0,0},{2,0},{-1,0},{2,-1},{-1,2}},
  {{0,0},{1,0},{-2,0},{1,2},{-2,-1}}
};

bool cell(uint16_t mask, int x, int y) { return (mask & (1u << (y * 4 + x))) != 0; }
uint32_t saturatedAdd(uint32_t a, uint32_t b) { return b > UINT32_MAX - a ? UINT32_MAX : a + b; }
unsigned gravityInterval(uint8_t level) {
  const unsigned reduction = (level - 1u) * 70u;
  return reduction >= 730 ? 120 : 850 - reduction;
}

}  // namespace

uint16_t tetrisPieceMask(TetrisPiece piece, uint8_t rotation) {
  const unsigned kind = static_cast<unsigned>(piece);
  return kind < 7 ? kMasks[kind][rotation % 4] : 0;
}

TetrisGame::TetrisGame() { reset(0, 0x51a7eaf1u); }

void TetrisGame::reset(uint32_t now, uint32_t seed) {
  state_ = TetrisSnapshot();
  random_ = seed ? seed : 0x51a7eaf1u;
  lastMs_ = now;
  accumulatedMs_ = gravityMs_ = lockMs_ = 0;
  bagIndex_ = 7;
  lockResets_ = events_ = 0;
  pieceSerial_ = 0;
  state_.next = drawPiece();
  spawn();
}

uint32_t TetrisGame::randomBelow(uint32_t limit) {
  random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
  return static_cast<uint32_t>((static_cast<uint64_t>(random_) * limit) >> 32);
}

TetrisPiece TetrisGame::drawPiece() {
  if (bagIndex_ == 7) {
    for (unsigned i = 0; i < 7; ++i) bag_[i] = static_cast<uint8_t>(i);
    for (unsigned i = 6; i > 0; --i) {
      const unsigned other = randomBelow(i + 1);
      const uint8_t value = bag_[i]; bag_[i] = bag_[other]; bag_[other] = value;
    }
    bagIndex_ = 0;
  }
  return static_cast<TetrisPiece>(bag_[bagIndex_++]);
}

bool TetrisGame::fits(TetrisPiece piece, uint8_t rotation, int x, int y) const {
  const uint16_t mask = tetrisPieceMask(piece, rotation);
  if (!mask) return false;
  for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) {
    if (!cell(mask, column, row)) continue;
    const int bx = x + column, by = y + row;
    if (bx < 0 || bx >= kTetrisColumns || by < -4 || by >= kTetrisRows) return false;
    if (by >= 0 && state_.board[by][bx]) return false;
  }
  return true;
}

void TetrisGame::updateContact() {
  state_.grounded = !fits(state_.piece, state_.rotation, state_.x, state_.y + 1);
  int ghost = state_.y;
  while (fits(state_.piece, state_.rotation, state_.x, ghost + 1)) ++ghost;
  state_.ghostY = static_cast<int8_t>(ghost);
}

void TetrisGame::spawn() {
  ++pieceSerial_;
  state_.piece = state_.next;
  state_.next = drawPiece();
  state_.rotation = 0;
  state_.x = 3; state_.y = -1;
  gravityMs_ = lockMs_ = 0; lockResets_ = 0;
  if (!fits(state_.piece, 0, state_.x, state_.y)) { finish(); return; }
  updateContact();
}

void TetrisGame::finish() {
  state_.phase = TetrisPhase::GameOver;
  events_ |= TetrisGameOver;
}

void TetrisGame::afterMove(bool wasGrounded) {
  if (wasGrounded && lockResets_ < kMaxLockResets) { lockMs_ = 0; ++lockResets_; }
  updateContact();
  // After the reset budget is exhausted, leaving a ledge cannot erase accrued
  // contact time. It pauses in the air and resumes on the next contact.
  if (!state_.grounded && lockResets_ < kMaxLockResets) lockMs_ = 0;
}

bool TetrisGame::moveLeft(uint32_t now) {
  advance(now);
  if (state_.phase != TetrisPhase::Playing ||
      !fits(state_.piece, state_.rotation, state_.x-1, state_.y)) return false;
  const bool grounded = state_.grounded;
  --state_.x;
  afterMove(grounded);
  events_ |= TetrisMove;
  return true;
}

bool TetrisGame::moveRight(uint32_t now, bool wrap) {
  advance(now);
  if (state_.phase != TetrisPhase::Playing) return false;
  int destination = state_.x + 1;
  if (!fits(state_.piece, state_.rotation, destination, state_.y)) {
    if (!wrap) return false;
    const uint16_t mask = tetrisPieceMask(state_.piece, state_.rotation);
    int minX = 4, maxX = 0;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) if (cell(mask, x, y)) {
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
    }
    if (state_.x + maxX != kTetrisColumns - 1) return false;
    destination = -minX;
    while (destination <= kTetrisColumns - 1 - maxX &&
           !fits(state_.piece, state_.rotation, destination, state_.y)) ++destination;
    if (destination > kTetrisColumns - 1 - maxX || destination == state_.x) return false;
  }
  const bool grounded = state_.grounded;
  state_.x = static_cast<int8_t>(destination);
  afterMove(grounded);
  events_ |= TetrisMove;
  return true;
}

bool TetrisGame::rotate(uint32_t now) {
  advance(now);
  if (state_.phase != TetrisPhase::Playing) return false;
  const uint8_t rotation = (state_.rotation + 1) % 4;
  const int8_t (*kicks)[2] = state_.piece == TetrisPiece::I ? kIKicks[state_.rotation] : kKicks[state_.rotation];
  const unsigned candidates = state_.piece == TetrisPiece::O ? 1 : 5;
  for (unsigned attempt = 0; attempt < candidates; ++attempt) {
    const int x = state_.x + kicks[attempt][0], y = state_.y + kicks[attempt][1];
    if (!fits(state_.piece, rotation, x, y)) continue;
    const bool grounded = state_.grounded;
    state_.rotation = rotation;
    state_.x = static_cast<int8_t>(x); state_.y = static_cast<int8_t>(y);
    afterMove(grounded);
    events_ |= TetrisRotate;
    return true;
  }
  return false;
}

bool TetrisGame::hardDrop(uint32_t now) {
  const uint32_t originalPiece = pieceSerial_;
  advance(now);
  if (state_.phase != TetrisPhase::Playing || pieceSerial_ != originalPiece) return false;
  state_.y = state_.ghostY;
  lock();
  return true;
}

void TetrisGame::advance(uint32_t now) {
  const uint32_t elapsed = now - lastMs_;
  lastMs_ = now;
  if (state_.phase != TetrisPhase::Playing) { accumulatedMs_ = 0; return; }
  accumulatedMs_ += static_cast<uint16_t>(elapsed > kMaxCatchupMs ? kMaxCatchupMs : elapsed);
  while (accumulatedMs_ >= kStepMs && state_.phase == TetrisPhase::Playing) {
    accumulatedMs_ -= kStepMs;
    step();
  }
}

void TetrisGame::resume(uint32_t now) { lastMs_ = now; accumulatedMs_ = 0; }
uint8_t TetrisGame::takeEvents() { const uint8_t result = events_; events_ = 0; return result; }

void TetrisGame::step() {
  gravityMs_ += kStepMs;
  const unsigned interval = gravityInterval(state_.level);
  if (gravityMs_ >= interval) {
    gravityMs_ -= static_cast<uint16_t>(interval);
    if (fits(state_.piece, state_.rotation, state_.x, state_.y + 1)) ++state_.y;
    updateContact();
  }
  if (state_.grounded) {
    lockMs_ += kStepMs;
    if (lockMs_ >= kLockDelayMs) lock();
  } else if (lockResets_ < kMaxLockResets) lockMs_ = 0;
}

void TetrisGame::lock() {
  const uint16_t mask = tetrisPieceMask(state_.piece, state_.rotation);
  bool aboveTop = false;
  for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) {
    if (!cell(mask, column, row)) continue;
    const int x = state_.x + column, y = state_.y + row;
    if (y < 0) aboveTop = true;
    else state_.board[y][x] = static_cast<uint8_t>(state_.piece) + 1;
  }
  events_ |= TetrisLock;
  state_.lastClearedLines = 0;
  if (aboveTop) { finish(); return; }
  int destination = kTetrisRows - 1;
  for (int row = kTetrisRows - 1; row >= 0; --row) {
    bool full = true;
    for (int x = 0; x < kTetrisColumns; ++x) if (!state_.board[row][x]) { full = false; break; }
    if (full) { ++state_.lastClearedLines; continue; }
    if (destination != row) for (int x = 0; x < kTetrisColumns; ++x) state_.board[destination][x] = state_.board[row][x];
    --destination;
  }
  for (; destination >= 0; --destination)
    for (int x = 0; x < kTetrisColumns; ++x) state_.board[destination][x] = 0;
  if (state_.lastClearedLines) {
    const uint32_t points[] = {0, 100, 300, 500, 800};
    const unsigned count = state_.lastClearedLines > 4 ? 4 : state_.lastClearedLines;
    state_.score = saturatedAdd(state_.score, points[count] * state_.level);
    state_.lines = saturatedAdd(state_.lines, state_.lastClearedLines);
    state_.level = state_.lines >= 980 ? 99 : static_cast<uint8_t>(state_.lines / 10 + 1);
    events_ |= TetrisLineClear;
  }
  spawn();
}

}  // namespace sloth
