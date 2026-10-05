#pragma once

#include <stdint.h>

namespace sloth {
enum class TetrisAction : uint8_t { None = 0, Left = 1, Right = 2, Rotate = 3, Drop = 4, Pause = 5 };

// The physical left button keeps its original rightward/wrapping move, but
// waits for release to distinguish it from a hold. A hold drops once; release
// after a drop cannot act on the next piece. Screen/piece changes cancel input.
class TetrisMoveButton {
 public:
  enum { kHoldMs = 650 };
  void reset() { needsRelease_ = true; tracking_ = false; consumed_ = false; }
  bool tracking() const { return tracking_ && !consumed_; }
  void press(uint32_t now, uint32_t piece) {
    if (needsRelease_ || tracking_) return;
    tracking_ = true; consumed_ = false; since_ = now; piece_ = piece;
  }
  TetrisAction update(bool down, uint32_t now, uint32_t piece, bool enabled = true) {
    if (!enabled) { reset(); return TetrisAction::None; }
    if (needsRelease_) {
      if (!down) needsRelease_ = false;
      return TetrisAction::None;
    }
    if (tracking_ && piece != piece_) consumed_ = true;
    if (!down) {
      const auto result = tracking_ && !consumed_
          ? (now - since_ >= kHoldMs ? TetrisAction::Drop : TetrisAction::Right)
          : TetrisAction::None;
      tracking_ = consumed_ = false;
      return result;
    }
    if (!tracking_) press(now, piece);
    if (!consumed_ && now - since_ >= kHoldMs) {
      consumed_ = true;
      return TetrisAction::Drop;
    }
    return TetrisAction::None;
  }
 private:
  bool needsRelease_ = true, tracking_ = false, consumed_ = false;
  uint32_t since_ = 0, piece_ = 0;
};

struct TetrisControlRect {
  int x, y, width, height;
  bool contains(int px, int py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};
namespace tetris_layout {
constexpr int kBoardLeft = 24, kBoardTop = 56, kCell = 8;
constexpr int kButtonWidth = 48, kButtonHeight = 42, kButtonGap = 12;
}

// One source of geometry for the renderer, touch routing and host tests.
inline TetrisControlRect tetrisButtonRect(TetrisAction action) {
  using namespace tetris_layout;
  const unsigned index = static_cast<unsigned>(action);
  if (index < 1 || index > 4) return TetrisControlRect{0, 0, 0, 0};
  return TetrisControlRect{112 + static_cast<int>((index - 1) % 2) * (kButtonWidth + kButtonGap),
      126 + static_cast<int>((index - 1) / 2) * (kButtonHeight + kButtonGap), kButtonWidth, kButtonHeight};
}
inline TetrisAction tetrisHitTest(int x, int y) {
  if (x < 0 || x >= 240 || y < 0 || y >= 240) return TetrisAction::None;
  if (x >= 20 && x < 220 && y >= 8 && y < 24)
    return x < 86 ? TetrisAction::Right : x < 154 ? TetrisAction::Pause : TetrisAction::Rotate;
  for (unsigned action = 1; action <= 4; ++action) {
    const auto target = static_cast<TetrisAction>(action);
    if (tetrisButtonRect(target).contains(x, y)) return target;
  }
  return TetrisAction::None;
}

// One action on the first release of a valid contact. Release packets have no
// meaningful coordinates on the controller, so only held samples are tracked.
// Any drag, wrong target or gap start cancels the whole contact permanently.
// reset() requires one physical release before arming again: a menu/game switch
// or a newly spawned piece can never inherit a held DROP gesture.
class TetrisControls {
 public:
  static TetrisAction hitTest(int x, int y) { return tetrisHitTest(x, y); }
  void reset() { tracking_ = false; needsRelease_ = true; candidate_ = TetrisAction::None; }
  TetrisAction pressed() const { return tracking_ ? candidate_ : TetrisAction::None; }
  TetrisAction touch(bool down, int x, int y) {
    if (needsRelease_) {
      if (!down) needsRelease_ = false;
      return TetrisAction::None;
    }
    if (!down) {
      const auto action = pressed();
      tracking_ = false; candidate_ = TetrisAction::None;
      return action;
    }
    if (x < 0 || x >= 240 || y < 0 || y >= 240) {
      tracking_ = true; candidate_ = TetrisAction::None;
      return TetrisAction::None;
    }
    const auto target = tetrisHitTest(x, y);
    if (!tracking_) {
      tracking_ = true; candidate_ = target; startX_ = x; startY_ = y;
    }
    const int dx = x - startX_, dy = y - startY_;
    if (target != candidate_ || dx * dx + dy * dy > 100) candidate_ = TetrisAction::None;
    return TetrisAction::None;
  }
 private:
  bool tracking_ = false, needsRelease_ = true;
  int startX_ = 0, startY_ = 0;
  TetrisAction candidate_ = TetrisAction::None;
};
}  // namespace sloth
