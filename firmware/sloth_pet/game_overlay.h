#pragma once
#include "game_records.h"
#include "keyboard.h"

namespace sloth {
enum class GameOverlayPage : uint8_t { Closed, ConfirmExit, Name, Scores };
enum class GameOverlayEvent : uint8_t { None, Closed, Resume, Leave, Save, Skip };

// Modal game UI, driven by the same Next/Back/Select rail as ordinary menus.
// No game clocks or persistent storage are touched by this portable component.
class GameOverlay {
 public:
  enum { kNext = 100, kBack = 101, kSelect = 102 };
  void confirmExit(GameKind game);
  void enterName(GameKind game, uint32_t score, uint16_t detail, const char* fallback);
  void showScores(GameKind game);
  void close() { page_ = GameOverlayPage::Closed; cancelTouch(); }
  bool isOpen() const { return page_ != GameOverlayPage::Closed; }
  GameOverlayPage page() const { return page_; }
  GameKind game() const { return game_; }
  int focus() const { return page_==GameOverlayPage::Name?keyboard_.focus():focus_; }
  unsigned scorePage() const { return scorePage_; }
  int scrollOffset() const { return page_==GameOverlayPage::Name?keyboard_.scroll():0; }
  static constexpr int kKeyboardTop = Keyboard::kTop, kKeyboardBottom = Keyboard::kBottom;
  static constexpr int kKeyWidth = Keyboard::kWidth, kKeyHeight = Keyboard::kHeight;
  static constexpr int kMaxScroll = ((37+Keyboard::kColumns-1)/Keyboard::kColumns)*Keyboard::kPitchY -
      (Keyboard::kPitchY-Keyboard::kHeight) - (Keyboard::kBottom-Keyboard::kTop);
  int targetCount() const;
  void next();
  GameOverlayEvent back();
  GameOverlayEvent activate(int target = -1);
  // Release coordinates must be the last valid contact position. In-place keys,
  // focus and scrolling return None; redraw while down and on the first release.
  GameOverlayEvent touch(bool down, int x, int y);
  void cancelTouch(); // A cancelled held contact must release before starting anew.
  int hitTest(int x, int y) const;
  const char* name() const { return name_; }
  const char* submittedName() const;
  const GameScore& result() const { return result_; }

  KeyboardSpec keyboardSpec() const;
  const Keyboard& keyboard() const { return keyboard_; }
 private:
  Keyboard keyboard_;
  GameOverlayEvent keyboardEvent(KeyboardEvent);
  GameOverlayPage page_ = GameOverlayPage::Closed;
  GameKind game_ = GameKind::Pong;
  int focus_ = 0;
  unsigned scorePage_ = 0;
  int touchX_ = 0, touchY_ = 0, touchTarget_ = -1;
  bool touching_ = false, dragged_ = false, releaseRequired_ = false;
  char name_[kScoreNameLength + 1] = {};
  char fallback_[kScoreNameLength + 1] = "MOSS";
  GameScore result_;
};

// ConfirmExit composites over the current game; other pages replace the frame.
void drawGameOverlay(uint16_t* pixels, const GameOverlay& ui,
                     const GameRecords& records, bool savePending = false, bool saveFailed = false);
}  // namespace sloth
