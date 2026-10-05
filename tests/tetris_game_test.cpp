#include "../firmware/sloth_pet/tetris_game.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace sloth {
struct TetrisTestAccess {
  static TetrisSnapshot& state(TetrisGame& game) { return game.state_; }
  static void clearBoard(TetrisGame& game) { memset(game.state_.board, 0, sizeof(game.state_.board)); }
  static void position(TetrisGame& game, TetrisPiece piece, unsigned rotation, int x, int y, uint32_t now) {
    game.state_.phase = TetrisPhase::Playing;
    game.state_.piece = piece; game.state_.rotation = static_cast<uint8_t>(rotation);
    game.state_.x = static_cast<int8_t>(x); game.state_.y = static_cast<int8_t>(y);
    game.accumulatedMs_ = game.gravityMs_ = game.lockMs_ = 0;
    game.lockResets_ = game.events_ = 0; game.lastMs_ = now;
    assert(game.fits(piece, static_cast<uint8_t>(rotation), x, y));
    game.updateContact();
  }
  static bool fits(const TetrisGame& game, int x, int y) {
    return game.fits(game.state_.piece, game.state_.rotation, x, y);
  }
  static unsigned lockResets(const TetrisGame& game) { return game.lockResets_; }
  static void exhaustResets(TetrisGame& game) { game.lockResets_ = TetrisGame::kMaxLockResets; }
  static void setGeneration(TetrisGame& game, uint32_t value) { game.pieceSerial_ = value; }
};
}  // namespace sloth

using namespace sloth;
namespace {
void tick(TetrisGame& game, uint32_t& now, unsigned milliseconds) {
  assert(milliseconds % TetrisGame::kStepMs == 0);
  for (unsigned time = 0; time < milliseconds; time += TetrisGame::kStepMs) {
    now += TetrisGame::kStepMs; game.advance(now);
  }
}

unsigned blockCount(const TetrisGame& game) {
  unsigned count = 0;
  for (const auto& row : game.snapshot().board) for (uint8_t value : row) count += value != 0;
  return count;
}

bool sameState(const TetrisSnapshot& a, const TetrisSnapshot& b) {
  // Independent snapshots can have different unused struct padding.
  return memcmp(a.board, b.board, sizeof(a.board)) == 0 &&
      a.phase == b.phase && a.piece == b.piece && a.next == b.next &&
      a.rotation == b.rotation && a.x == b.x && a.y == b.y && a.ghostY == b.ghostY &&
      a.score == b.score && a.lines == b.lines && a.level == b.level &&
      a.lastClearedLines == b.lastClearedLines && a.grounded == b.grounded;
}

void checkBounds(const TetrisGame& game) {
  const auto& s = game.snapshot();
  for (const auto& row : s.board) for (uint8_t value : row) assert(value <= 7);
  assert(s.rotation < 4 && s.level >= 1 && s.level <= 99 && s.lastClearedLines <= 4);
  assert(static_cast<unsigned>(s.piece) < 7 && static_cast<unsigned>(s.next) < 7);
  if (s.phase == TetrisPhase::Playing) {
    assert(TetrisTestAccess::fits(game, s.x, s.y));
    assert(TetrisTestAccess::fits(game, s.x, s.ghostY));
    assert(!TetrisTestAccess::fits(game, s.x, s.ghostY + 1));
    assert(s.ghostY >= s.y);
    assert(s.grounded == !TetrisTestAccess::fits(game, s.x, s.y + 1));
  }
}

void checkBag() {
  TetrisGame a, b;
  uint32_t now = 0;
  a.reset(now, 42); b.reset(now, 42);
  unsigned counts[7] = {};
  for (unsigned draw = 0; draw < 700; ++draw) {
    const auto piece = a.snapshot().piece;
    assert(piece == b.snapshot().piece && a.snapshot().next == b.snapshot().next);
    ++counts[static_cast<unsigned>(piece)];
    if (draw % 7 == 6) {
      for (unsigned count : counts) assert(count == 1);
      memset(counts, 0, sizeof(counts));
    }
    // Drive real lock/spawn code, clearing only the old fixture stack between drops.
    TetrisTestAccess::clearBoard(a); TetrisTestAccess::clearBoard(b);
    TetrisTestAccess::position(a, piece, 0, 3, -1, now);
    TetrisTestAccess::position(b, piece, 0, 3, -1, now);
    TetrisTestAccess::position(a, piece, 0, 3, a.snapshot().ghostY, now);
    TetrisTestAccess::position(b, piece, 0, 3, b.snapshot().ghostY, now);
    uint32_t otherNow = now;
    tick(a, now, 350); tick(b, otherNow, 350);
    assert(a.takeEvents() == TetrisLock && b.takeEvents() == TetrisLock);
  }
  a.reset(0, 0); b.reset(0, 0);
  assert(a.snapshot().piece == b.snapshot().piece);
  bool different = false;
  for (uint32_t seed = 1; seed < 8; ++seed) {
    b.reset(0, seed);
    different |= b.snapshot().piece != a.snapshot().piece || b.snapshot().next != a.snapshot().next;
  }
  assert(different);
}

void checkMovement() {
  TetrisGame game;
  for (unsigned kind = 0; kind < 7; ++kind) {
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
      const uint16_t mask = tetrisPieceMask(static_cast<TetrisPiece>(kind), static_cast<uint8_t>(rotation));
      unsigned cells = 0;
      for (unsigned bit = 0; bit < 16; ++bit) cells += (mask >> bit) & 1u;
      assert(cells == 4);
    }
    game.reset(0, 12);
    TetrisTestAccess::position(game, static_cast<TetrisPiece>(kind), 0, 3, 5, 0);
    for (unsigned turn = 0; turn < 4; ++turn) assert(game.rotate(0));
    assert(game.snapshot().rotation == 0 && game.snapshot().x == 3 && game.snapshot().y == 5);
    assert(game.takeEvents() == TetrisRotate && game.takeEvents() == 0);
  }
  assert(tetrisPieceMask(static_cast<TetrisPiece>(255), 0) == 0);
  game.reset(0, 7);
  TetrisTestAccess::position(game, TetrisPiece::I, 0, 6, 5, 0);
  assert(game.moveRight(0) && game.snapshot().x == 0);
  assert(game.takeEvents() == TetrisMove);
  TetrisTestAccess::position(game, TetrisPiece::I, 0, 6, 5, 0);
  TetrisTestAccess::state(game).board[6][0] = 7;
  assert(game.moveRight(0) && game.snapshot().x == 1);  // Leftmost legal wrap destination.
  TetrisTestAccess::clearBoard(game);
  TetrisTestAccess::state(game).board[6][7] = 7;
  TetrisTestAccess::position(game, TetrisPiece::I, 0, 3, 5, 0);
  assert(!game.moveRight(0) && game.snapshot().x == 3 && game.takeEvents() == 0);

  TetrisTestAccess::clearBoard(game);
  TetrisTestAccess::position(game, TetrisPiece::I, 1, 7, 5, 0);
  assert(game.rotate(0) && game.snapshot().rotation == 2 && game.snapshot().x == 6);
  TetrisTestAccess::position(game, TetrisPiece::I, 1, -2, 5, 0);
  assert(game.rotate(0) && game.snapshot().x == 0);
  TetrisTestAccess::position(game, TetrisPiece::T, 1, -1, 5, 0);
  assert(game.rotate(0) && game.snapshot().x == 0);
  TetrisTestAccess::position(game, TetrisPiece::T, 0, 3, 18, 0);
  assert(game.snapshot().grounded);
  assert(game.rotate(0) && game.snapshot().rotation == 1 && game.snapshot().y == 17);
  checkBounds(game);

  // Surround a T with settled leaves: all five clockwise kick candidates fail.
  memset(TetrisTestAccess::state(game).board, 1, sizeof(game.snapshot().board));
  TetrisTestAccess::state(game).board[8][4] = 0;
  for (int x = 3; x <= 5; ++x) TetrisTestAccess::state(game).board[9][x] = 0;
  TetrisTestAccess::position(game, TetrisPiece::T, 0, 3, 8, 0);
  assert(!game.rotate(0) && game.snapshot().rotation == 0 && game.takeEvents() == 0);
}

void checkLockAndLines() {
  TetrisGame game;
  uint32_t now = 0;
  TetrisTestAccess::position(game, TetrisPiece::O, 0, 3, 18, now);
  tick(game, now, 340);
  assert(blockCount(game) == 0 && game.takeEvents() == 0);
  tick(game, now, 10);
  assert(blockCount(game) == 4 && game.takeEvents() == TetrisLock);

  game.reset(now, 4);
  TetrisTestAccess::position(game, TetrisPiece::O, 0, 3, 18, now);
  tick(game, now, 300);
  assert(game.moveRight(now));  // A successful grounded move grants a fresh delay.
  tick(game, now, 340);
  assert(blockCount(game) == 0);
  tick(game, now, 10);
  assert(blockCount(game) == 4);

  game.reset(now, 5);
  TetrisTestAccess::position(game, TetrisPiece::O, 0, 3, 18, now);
  for (unsigned reset = 0; reset < TetrisGame::kMaxLockResets; ++reset) {
    tick(game, now, 100); assert(game.moveRight(now));
    assert(blockCount(game) == 0);
  }
  assert(TetrisTestAccess::lockResets(game) == TetrisGame::kMaxLockResets);
  tick(game, now, 300); assert(game.moveRight(now));
  tick(game, now, 50);
  assert(blockCount(game) == 4);  // Continuous shuffling cannot prevent lock forever.

  game.reset(now, 5);
  TetrisTestAccess::state(game).board[16][1] = 7;
  TetrisTestAccess::position(game, TetrisPiece::O, 0, 0, 14, now);
  TetrisTestAccess::exhaustResets(game);
  assert(game.snapshot().grounded);
  tick(game, now, 300);
  assert(game.moveRight(now) && !game.snapshot().grounded);
  tick(game, now, 20);  // Airborne time does not erase accrued contact after the cap.
  do { assert(game.moveRight(now)); } while (game.snapshot().x != 0);
  assert(game.snapshot().grounded);
  tick(game, now, 50);
  assert(blockCount(game) == 5);  // Wrapping off and back onto a ledge cannot bypass the cap.

  const uint32_t expectedScores[] = {0, 100, 300, 500, 800};
  for (unsigned lines = 1; lines <= 4; ++lines) {
    game.reset(now, 1);
    auto& s = TetrisTestAccess::state(game);
    if (lines == 1) {
      for (unsigned x = 0; x < 6; ++x) s.board[19][x] = 3;
      TetrisTestAccess::position(game, TetrisPiece::I, 0, 6, 18, now);
    } else if (lines == 2) {
      for (unsigned y = 18; y < 20; ++y) for (unsigned x = 0; x < 8; ++x) s.board[y][x] = 3;
      TetrisTestAccess::position(game, TetrisPiece::O, 0, 7, 18, now);
    } else {
      for (unsigned y = 20 - lines; y < 20; ++y) for (unsigned x = 0; x < 9; ++x) s.board[y][x] = 3;
      TetrisTestAccess::position(game, TetrisPiece::I, 1, 7, 16, now);
    }
    tick(game, now, 350);
    assert(s.lastClearedLines == lines && s.lines == lines && s.score == expectedScores[lines]);
    assert((game.takeEvents() & (TetrisLock | TetrisLineClear)) == (TetrisLock | TetrisLineClear));
    assert(blockCount(game) == (lines == 3 ? 1u : 0u));
    if (lines == 3) assert(s.board[19][9] == 1);  // Uncleared cell falls with compacted rows.
    checkBounds(game);
  }

  game.reset(now, 1);
  auto& s = TetrisTestAccess::state(game);
  for (unsigned x = 0; x < 6; ++x) s.board[19][x] = 2;
  s.lines = 9; s.level = 1; s.score = 1000;
  TetrisTestAccess::position(game, TetrisPiece::I, 0, 6, 18, now);
  tick(game, now, 350);
  assert(s.lines == 10 && s.level == 2 && s.score == 1100);
  for (unsigned x = 0; x < 6; ++x) s.board[19][x] = 2;
  TetrisTestAccess::position(game, TetrisPiece::I, 0, 6, 18, now);
  tick(game, now, 350);
  assert(s.lines == 11 && s.level == 2 && s.score == 1300);
  TetrisTestAccess::clearBoard(game);
  for (unsigned y = 16; y < 20; ++y) for (unsigned x = 0; x < 9; ++x) s.board[y][x] = 2;
  s.lines = UINT32_MAX - 1; s.score = UINT32_MAX - 10; s.level = 99;
  TetrisTestAccess::position(game, TetrisPiece::I, 1, 7, 16, now);
  tick(game, now, 350);
  assert(s.lines == UINT32_MAX && s.score == UINT32_MAX && s.level == 99);
}

void checkTimingAndGameOver() {
  TetrisGame a, b;
  a.reset(0, 23); b.reset(0, 23);
  for (uint32_t t = 10; t <= 4000; t += 10) a.advance(t);
  for (uint32_t t = 200; t <= 4000; t += 200) b.advance(t);
  assert(a.snapshot().y == b.snapshot().y);
  a.reset(0, 23); b.reset(UINT32_MAX - 31, 23);
  for (uint32_t t = 1; t <= 1000; ++t) { a.advance(t); b.advance(UINT32_MAX - 31 + t); }
  assert(a.snapshot().y == b.snapshot().y && a.snapshot().ghostY == b.snapshot().ghostY);
  a.reset(0, 23); b.reset(0, 23);
  a.advance(200); b.advance(3600000);
  assert(a.snapshot().y == b.snapshot().y);
  uint32_t now = 0;
  a.reset(now, 3);
  TetrisTestAccess::position(a, TetrisPiece::O, 0, 3, 18, now);
  tick(a, now, 200);
  a.resume(now += 3600000);
  tick(a, now, 140);
  assert(blockCount(a) == 0);
  tick(a, now, 10);
  assert(blockCount(a) == 4);  // Resume preserves prior progress, not paused wall time.

  a.reset(now, 4);
  auto& s = TetrisTestAccess::state(a);
  s.board[0][4] = 7; s.next = TetrisPiece::T;
  TetrisTestAccess::position(a, TetrisPiece::O, 0, 0, 18, now);
  tick(a, now, 350);
  assert(s.phase == TetrisPhase::GameOver);
  assert(a.takeEvents() == (TetrisLock | TetrisGameOver));
  const TetrisSnapshot frozen = s;
  assert(!a.moveRight(now) && !a.rotate(now));
  a.advance(now += 60000);
  assert(memcmp(&s, &frozen, sizeof(s)) == 0 && a.takeEvents() == 0);
  a.reset(now, 4);
  for (unsigned x = 3; x < 6; ++x) s.board[1][x] = 7;
  TetrisTestAccess::position(a, TetrisPiece::T, 0, 3, -1, now);
  assert(s.grounded);
  tick(a, now, 350);
  assert(s.phase == TetrisPhase::GameOver && (a.takeEvents() & TetrisGameOver));
}

void checkLeftMovement() {
  TetrisGame game;
  uint32_t now = 0;
  for (unsigned piece = 0; piece < 7; ++piece) for (unsigned rotation = 0; rotation < 4; ++rotation) {
    game.reset(now,42);
    TetrisTestAccess::position(game,static_cast<TetrisPiece>(piece),rotation,3,6,now);
    const uint32_t generation = game.pieceGeneration();
    unsigned moved = 0;
    while (game.moveLeft(now)) { ++moved; checkBounds(game); assert(moved <= 5); }
    assert(moved >= 3 && game.pieceGeneration() == generation);
    assert(!TetrisTestAccess::fits(game,game.snapshot().x-1,game.snapshot().y));
    assert(game.takeEvents() == TetrisMove);
    const auto atWall = game.snapshot();
    assert(!game.moveLeft(now) && game.takeEvents() == 0 && sameState(atWall,game.snapshot()));
    moved = 0;
    while (game.moveRight(now,false)) { ++moved; checkBounds(game); assert(moved <= 9); }
    assert(moved >= 6 && game.pieceGeneration() == generation);
    assert(!TetrisTestAccess::fits(game,game.snapshot().x+1,game.snapshot().y));
    assert(game.takeEvents() == TetrisMove);
    const auto rightWall = game.snapshot();
    assert(!game.moveRight(now,false) && game.takeEvents() == 0 && sameState(rightWall,game.snapshot()));
  }
  game.reset(now,42);
  TetrisTestAccess::state(game).board[5][3] = 7;
  TetrisTestAccess::position(game,TetrisPiece::O,0,3,5,now);
  assert(!game.moveLeft(now) && game.snapshot().x == 3 && game.takeEvents() == 0);
  assert(TetrisTestAccess::fits(game,0,5));  // An empty space beyond the blocker is never jumped to.
  TetrisTestAccess::state(game).board[5][6] = 7;
  assert(!game.moveRight(now,false) && game.snapshot().x == 3 && game.takeEvents() == 0);
  assert(TetrisTestAccess::fits(game,6,5));

  game.reset(now,42);
  TetrisTestAccess::position(game,TetrisPiece::O,0,3,18,now);
  tick(game,now,300); assert(game.moveLeft(now));
  assert(TetrisTestAccess::lockResets(game) == 1);
  tick(game,now,340); assert(blockCount(game) == 0);
  tick(game,now,10); assert(blockCount(game) == 4);

  game.reset(now,42);
  TetrisTestAccess::position(game,TetrisPiece::O,0,-1,18,now);
  tick(game,now,300); assert(!game.moveLeft(now));
  assert(TetrisTestAccess::lockResets(game) == 0);
  tick(game,now,50); assert(blockCount(game) == 4);  // A failed wall move grants no delay.

  game.reset(now,42);
  TetrisTestAccess::position(game,TetrisPiece::O,0,3,18,now);
  for (unsigned reset = 0; reset < TetrisGame::kMaxLockResets; ++reset) {
    tick(game,now,100);
    assert(reset % 2 ? game.moveRight(now) : game.moveLeft(now));
  }
  assert(TetrisTestAccess::lockResets(game) == TetrisGame::kMaxLockResets);
  tick(game,now,300); assert(game.moveLeft(now));
  tick(game,now,50); assert(blockCount(game) == 4);
}

void checkHardDrop() {
  TetrisGame game;
  uint32_t now = 0;
  for (unsigned piece = 0; piece < 7; ++piece) for (unsigned rotation = 0; rotation < 4; ++rotation) {
    game.reset(now,42);
    assert(game.pieceGeneration() == 1);
    TetrisTestAccess::position(game,static_cast<TetrisPiece>(piece),rotation,4,3,now);
    const auto before = game.snapshot();
    assert(game.hardDrop(now));
    const auto& after = game.snapshot();
    assert(game.pieceGeneration() == 2 && blockCount(game) == 4);
    assert(after.piece == before.next && after.y == -1 && after.rotation == 0);
    assert(after.score == 0 && after.lines == 0 && game.takeEvents() == TetrisLock);
    const uint16_t mask = tetrisPieceMask(before.piece,before.rotation);
    for (int y = 0; y < kTetrisRows; ++y) for (int x = 0; x < kTetrisColumns; ++x) {
      const int px = x-before.x, py = y-before.ghostY;
      const bool occupied = px >= 0 && px < 4 && py >= 0 && py < 4 && ((mask>>(py*4+px))&1);
      assert(after.board[y][x] == (occupied ? piece+1 : 0));
    }
    game.advance(now);
    assert(blockCount(game) == 4 && game.takeEvents() == 0);  // No delayed second lock.
    checkBounds(game);
  }

  const uint32_t expected[] = {0,100,300,500,800};
  for (unsigned lines = 1; lines <= 4; ++lines) {
    game.reset(now,42);
    auto& s = TetrisTestAccess::state(game);
    if (lines == 1) {
      for (unsigned x = 0; x < 6; ++x) s.board[19][x] = 3;
      TetrisTestAccess::position(game,TetrisPiece::I,0,6,0,now);
    } else if (lines == 2) {
      for (unsigned y = 18; y < 20; ++y) for (unsigned x = 0; x < 8; ++x) s.board[y][x] = 3;
      TetrisTestAccess::position(game,TetrisPiece::O,0,7,0,now);
    } else {
      for (unsigned y = 20-lines; y < 20; ++y) for (unsigned x = 0; x < 9; ++x) s.board[y][x] = 3;
      TetrisTestAccess::position(game,TetrisPiece::I,1,7,0,now);
    }
    assert(game.hardDrop(now) && s.score == expected[lines] && s.lines == lines);
    assert(game.takeEvents() == (TetrisLock|TetrisLineClear));
    assert(blockCount(game) == (lines == 3 ? 1u : 0u));
  }

  game.reset(now,42);
  TetrisTestAccess::position(game,TetrisPiece::O,0,3,18,now);
  tick(game,now,340);
  assert(!game.hardDrop(now+=10));  // Elapsed-time lock consumes this late drop.
  assert(blockCount(game) == 4 && game.pieceGeneration() == 2 && game.snapshot().y == -1);
  assert(game.takeEvents() == TetrisLock);

  game.reset(now,42);
  auto& s = TetrisTestAccess::state(game);
  for (unsigned x = 3; x < 6; ++x) s.board[1][x] = 7;
  TetrisTestAccess::position(game,TetrisPiece::T,0,3,-1,now);
  assert(game.hardDrop(now) && s.phase == TetrisPhase::GameOver);
  assert(game.takeEvents() == (TetrisLock|TetrisGameOver));
  const auto frozen = s;
  assert(!game.hardDrop(now+=10000) && !game.moveLeft(now));
  assert(sameState(frozen,s) && game.takeEvents() == 0);

  game.reset(now,42);
  s.board[0][4] = 7; s.next = TetrisPiece::T;
  TetrisTestAccess::position(game,TetrisPiece::O,0,0,3,now);
  assert(game.hardDrop(now) && s.phase == TetrisPhase::GameOver && game.pieceGeneration() == 2);
  assert(blockCount(game) == 5 && game.takeEvents() == (TetrisLock|TetrisGameOver));

  TetrisGame ordinary,wrapped;
  ordinary.reset(0,23); wrapped.reset(UINT32_MAX-31,23);
  for (uint32_t elapsed = 10; elapsed <= 900; elapsed += 10) {
    ordinary.advance(elapsed); wrapped.advance(UINT32_MAX-31+elapsed);
  }
  assert(ordinary.hardDrop(910) && wrapped.hardDrop(UINT32_MAX-31+910));
  assert(sameState(ordinary.snapshot(),wrapped.snapshot()));
  ordinary.reset(0,23); wrapped.reset(0,23);
  ordinary.advance(200); wrapped.advance(200);
  ordinary.resume(60000); assert(ordinary.hardDrop(60000) && wrapped.hardDrop(200));
  assert(sameState(ordinary.snapshot(),wrapped.snapshot()));
  ordinary.reset(0,23); wrapped.reset(0,23);
  assert(ordinary.hardDrop(200) && wrapped.hardDrop(3600000));
  assert(sameState(ordinary.snapshot(),wrapped.snapshot()));
  ordinary.reset(0,23);
  TetrisTestAccess::setGeneration(ordinary,UINT32_MAX);
  assert(ordinary.hardDrop(0) && ordinary.pieceGeneration() == 0);
}

void checkRandomPlayAndRenderer() {
  TetrisGame game;
  uint16_t guarded[240 * 240 + 2];
  guarded[0] = 0x1234; guarded[240 * 240 + 1] = 0xabcd;
  uint32_t now = 0, rng = 991;
  unsigned finished = 0, lockEvents = 0;
  sloth::drawTetris(nullptr, game);
  for (unsigned frame = 0; frame < 40000; ++frame) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    if (game.snapshot().phase == TetrisPhase::GameOver) {
      drawTetris(guarded + 1, game);
      assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0xabcd);
      ++finished; game.reset(now, rng);
    }
    if (rng % 11 == 0) game.moveRight(now);
    if (rng % 17 == 0) game.rotate(now);
    if (rng % 13 == 0) game.moveLeft(now);
    if (rng % 43 == 0) game.hardDrop(now);
    now += 100; game.advance(now);
    lockEvents += (game.takeEvents() & TetrisLock) != 0;
    checkBounds(game);
    if (frame % 97 == 0) {
      const TetrisSnapshot before = game.snapshot();
      drawTetris(guarded + 1, game);
      assert(memcmp(&before, &game.snapshot(), sizeof(before)) == 0);
      assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0xabcd);
    }
  }
  assert(finished > 0 && lockEvents > 100);
  printf("Tetris randomized play: %u settled pieces, %u complete games\n", lockEvents, finished);
}
}  // namespace

int main() {
  static_assert(sizeof(TetrisGame) < 320, "Tetris must remain a small fixed-state game");
  checkBag(); checkMovement(); checkLockAndLines(); checkTimingAndGameOver();
  checkLeftMovement(); checkHardDrop(); checkRandomPlayAndRenderer();
  puts("Tetris checks passed: seeded 7-bag, four-cell masks, clockwise rotations/kicks, physical-edge wrap, collision rejection, ghost, lock delay/reset cap, 1-4 lines, scores/levels, saturation, fixed-step/wrap/pause, left movement, hard-drop position/scoring/lifecycle, both game-over paths, no auto-restart, events and render bounds");
}
