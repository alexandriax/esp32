#pragma once
#include <stdint.h>

namespace sloth {
enum class ForestFidgetAction : uint8_t { None, Previous, Back, Next };

// Navigation needs one valid release, not repeated idle sensor packets.
// Continuous field strokes never become navigation after crossing the rail.
class ForestFidgetControls {
 public:
  static ForestFidgetAction hitTest(int x, int y) {
    if (x < 20 || x >= 220 || y < 8 || y >= 24) return ForestFidgetAction::None;
    return x < 86 ? ForestFidgetAction::Previous :
        x < 154 ? ForestFidgetAction::Back : ForestFidgetAction::Next;
  }
  void reset() {
    tracking_ = false;
    needsRelease_ = true;
    candidate_ = ForestFidgetAction::None;
  }
  ForestFidgetAction touch(bool down, int x, int y) {
    if (needsRelease_) {
      if (!down) needsRelease_ = false;
      return ForestFidgetAction::None;
    }
    if (!down) {
      const auto result = tracking_ ? candidate_ : ForestFidgetAction::None;
      tracking_ = false;
      candidate_ = ForestFidgetAction::None;
      return result;
    }
    if (x < 0 || x >= 240 || y < 0 || y >= 240) {
      tracking_ = true;
      candidate_ = ForestFidgetAction::None;
      return ForestFidgetAction::None;
    }
    const auto target = hitTest(x, y);
    if (!tracking_) {
      tracking_ = true;
      candidate_ = target;
      startX_ = x; startY_ = y;
    }
    const int dx = x - startX_, dy = y - startY_;
    if (target != candidate_ || dx * dx + dy * dy > 100)
      candidate_ = ForestFidgetAction::None;
    return ForestFidgetAction::None;
  }
 private:
  bool tracking_ = false, needsRelease_ = true;
  int startX_ = 0, startY_ = 0;
  ForestFidgetAction candidate_ = ForestFidgetAction::None;
};
}  // namespace sloth
