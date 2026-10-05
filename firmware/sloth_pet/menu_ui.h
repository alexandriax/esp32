#pragma once

#include <stdint.h>

namespace sloth {

enum class MenuPage : uint8_t { Main, Games, Pong, PongSettings, Tetris, TetrisSettings, LeafSweep, LeafSweepSettings, Utilities };
enum class MenuEvent : uint8_t {
  None, OpenSettings, RemoteDisplay, PlayPong, SpeedsChanged, Closed,
  PlayTetris, ShowPongScores, ShowTetrisScores, GamePreferencesChanged,
  PlayLeafSweep, ShowLeafSweepScores, PlayForestFidget, WifiExplorer, BluetoothExplorer, WifiNetworks, Browser
};

// Portable menu navigation. External destinations leave the menu open so their
// caller can return to the same page and focus. Speeds and audio are immediate
// preferences; the caller persists their change events independently of the
// pet settings' transactional draft. Game 0 is Pong; game 1 is Tetris; game 2 is Leaf Sweep.
class MenuUi {
 public:
  enum {
    kMainGames = 0, kMainBrowser = 1, kMainWifiNetworks = 2, kMainUtilities = 3, kMainSettings = 4,
    kUtilitiesRemoteDisplay = 0, kUtilitiesWifi = 1, kUtilitiesBluetooth = 2,
    kGamesPong = 0, kGamesTetris = 1, kGamesLeafSweep = 2, kGamesForestFidget = 3,
    kPongPlay = 0, kPongSettings = 1, kPongScores = 2,
    kTetrisPlay = 0, kTetrisSettings = 1, kTetrisScores = 2,
    kLeafSweepPlay = 0, kLeafSweepSettings = 1, kLeafSweepScores = 2,
    kSpeedLeft = 0, kSpeedRight = 1, kPongMusic = 2, kPongEffects = 3,
    kTetrisMusic = 0, kTetrisEffects = 1,
    kLeafSweepMusic = 0, kLeafSweepEffects = 1,
    kBack = 100, kNext = 101, kSelect = 102
  };

  MenuUi();
  void open();
  void close();
  bool isOpen() const { return open_; }
  MenuPage page() const { return page_; }
  int focus() const { return focus_; }
  int scrollOffset() const { return scroll_; }
  int maxScroll() const;
  int rowCount() const;
  // Includes the Back footer as final numbered target, equal to rowCount().
  int targetCount() const;
  void next();
  // Row IDs activate a row; the top rail returns kNext/kBack/kSelect.
  MenuEvent activate(int target = -1);
  MenuEvent back();
  int hitTest(int x, int y) const;
  // Logical 240x240 coordinates. Activate on release; a drag cancels the tap.
  MenuEvent touch(bool down, int x, int y);
  void cancelTouch();
  void setSpeeds(uint8_t left, uint8_t right);
  uint8_t speed(unsigned player) const;
  void setAudio(unsigned game, bool music, bool effects);
  bool music(unsigned game) const;
  bool effects(unsigned game) const;

 private:
  bool open_;
  MenuPage page_;
  int focus_, scroll_;
  bool touching_, dragged_, scrollGesture_;
  int touchX_, touchY_, touchScroll_, touchTarget_;
  void revealFocus();
  uint8_t speeds_[2];
  bool music_[3], effects_[3];
};

// Complete 240 x 240 RGB565 frame. No allocation, hardware or storage access.
void drawMenu(uint16_t* pixels, const MenuUi& ui);

}  // namespace sloth
