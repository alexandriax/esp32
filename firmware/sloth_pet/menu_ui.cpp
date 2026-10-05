#include "menu_ui.h"
#include "pet_canvas.h"
#include "navigation_bar.h"
#include "ui_icons.h"

#include <stdio.h>

namespace sloth {
namespace {

const int kLeft = 12, kWidth = 216, kFirstRow = 51, kRowPitch = 47, kRowHeight = 40;
const int kBackY = 205, kBackHeight = 22;
const int kCompactPitch = 36, kCompactHeight = 30;
const int kViewportTop = 50, kViewportBottom = 187;

int bounded(int value) { return value < -480 ? -480 : value > 480 ? 480 : value; }

uint8_t validSpeed(uint8_t value) { return value < 1 ? 1 : value > 5 ? 5 : value; }

bool inside(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

using graphics::Canvas;
using graphics::cream;
using graphics::gold;
using graphics::ink;
using graphics::mint;
using graphics::muted;
using graphics::rgb;

void row(Canvas& c, int y, const char* label, const char* detail, bool focused, UiIcon icon, bool compact = false) {
  const int height = compact ? kCompactHeight : kRowHeight;
  if (focused) c.roundRect(kLeft - 1, y - 1, kWidth + 2, height + 2, 6, gold);
  c.roundRect(kLeft, y, kWidth, height, 5, rgb(29, 53, 47));
  drawUiIcon(c, 21, y + (height - 16) / 2, icon, mint);
  c.text(44, y + (compact ? 5 : 8), label, cream);
  c.text(44, y + (compact ? 18 : 25), detail, muted);
  c.line(214, y + height / 2 - 4, 218, y + height / 2, gold);
  c.line(218, y + height / 2, 214, y + height / 2 + 4, gold);
}

const char* speedName(uint8_t speed) {
  static const char* const names[] = {"SNOOZY", "EASY", "STEADY", "QUICK", "ZOOM"};
  return names[validSpeed(speed) - 1];
}

void speedRow(Canvas& c, int y, unsigned player, uint8_t speed, bool focused) {
  if (focused) c.roundRect(11, y - 1, 218, kCompactHeight + 2, 5, gold);
  c.roundRect(12, y, 216, kCompactHeight, 4, rgb(29, 53, 47));
  drawUiIcon(c, 21, y + 7, UiIcon::Speed, mint);
  c.text(44, y + 5, player ? "PLAYER 2 / RIGHT" : "PLAYER 1 / LEFT", cream);
  char value[22];
  snprintf(value, sizeof(value), "%u/5  %s", speed, speedName(speed));
  c.text(44, y + 18, value, mint);
  for (int index = 0; index < 5; ++index)
    c.roundRect(153 + index * 13, y + 18, 9, 6, 2,
                index < speed ? mint : rgb(59, 80, 66));
}

void audioRow(Canvas& c, int y, const char* label, bool on, bool focused, bool compact, UiIcon icon) {
  const int height = compact ? kCompactHeight : kRowHeight;
  if (focused) c.roundRect(11, y - 1, 218, height + 2, 5, gold);
  c.roundRect(12, y, 216, height, 4, rgb(29, 53, 47));
  drawUiIcon(c, 21, y + (height - 16) / 2, icon, mint);
  c.text(44, y + (height - 7) / 2, label, cream);
  c.roundRect(164, y + (height - 18) / 2, 52, 18, 4, on ? mint : rgb(59, 80, 66));
  c.text(on ? 184 : 181, y + (height - 7) / 2, on ? "ON" : "OFF", on ? ink : cream);
}
}  // namespace

MenuUi::MenuUi()
    : open_(false), page_(MenuPage::Main), focus_(0), scroll_(0),
      touching_(false), dragged_(false), scrollGesture_(false),
      touchX_(0), touchY_(0), touchScroll_(0), touchTarget_(-1), speeds_{3, 3},
      music_{false, false, false}, effects_{true, true, true} {}

void MenuUi::open() { open_ = true; page_ = MenuPage::Main; focus_ = scroll_ = 0; cancelTouch(); }
void MenuUi::close() { open_ = false; scroll_ = 0; cancelTouch(); }

int MenuUi::rowCount() const {
  if (!open_) return 0;
  switch (page_) {
    case MenuPage::Main: return 5;
    case MenuPage::Utilities: return 3;
    case MenuPage::Games: return 5;
    case MenuPage::Doom: return 1;
    case MenuPage::Pong: return 3;
    case MenuPage::PongSettings: return 4;
    case MenuPage::Tetris: return 3;
    case MenuPage::TetrisSettings: return 2;
    case MenuPage::LeafSweep: return 3;
    case MenuPage::LeafSweepSettings: return 2;
  }
  return 0;
}

int MenuUi::targetCount() const { return open_ ? rowCount() + 1 : 0; }

int MenuUi::maxScroll() const {
  if (!open_) return 0;
  if (page_ == MenuPage::Main)
    return kFirstRow + (rowCount() - 1) * kRowPitch + kRowHeight + 1 - kViewportBottom;
  if (page_ == MenuPage::Games)
    return kFirstRow + (rowCount() - 1) * kCompactPitch + kCompactHeight + 1 - kViewportBottom;
  return 0;
}

void MenuUi::cancelTouch() { touching_ = dragged_ = scrollGesture_ = false; touchTarget_ = -1; }

void MenuUi::revealFocus() {
  if (page_ != MenuPage::Main && page_ != MenuPage::Games) { scroll_ = 0; return; }
  if (focus_ >= rowCount()) return; // Back is always visible in the fixed footer.
  const int pitch = page_ == MenuPage::Games ? kCompactPitch : kRowPitch;
  const int height = page_ == MenuPage::Games ? kCompactHeight : kRowHeight;
  const int top = focus_ * pitch, viewport = kViewportBottom - kViewportTop;
  if (top < scroll_) scroll_ = top;
  if (top + height + 2 > scroll_ + viewport) scroll_ = top + height + 2 - viewport;
  if (scroll_ > maxScroll()) scroll_ = maxScroll();
}

void MenuUi::next() {
  const int count = targetCount();
  if (!count) return;
  cancelTouch();
  focus_ = (focus_ + 1) % count;
  revealFocus();
}

MenuEvent MenuUi::touch(bool down, int x, int y) {
  if (!open_) { cancelTouch(); return MenuEvent::None; }
  if (down && !touching_) {
    if (x < 0 || x >= 240 || y < 0 || y >= 240) return MenuEvent::None;
    touching_ = true; dragged_ = false;
    touchX_ = x; touchY_ = y; touchScroll_ = scroll_; touchTarget_ = hitTest(x, y);
    scrollGesture_ = maxScroll() > 0 && y >= kViewportTop && y < kViewportBottom;
    return MenuEvent::None;
  }
  if (!touching_) return MenuEvent::None;
  const int dx = bounded(x) - touchX_, dy = bounded(y) - touchY_;
  if (dx > 7 || dx < -7 || dy > 7 || dy < -7) dragged_ = true;
  if (down) {
    if (dragged_ && scrollGesture_) {
      const int offset = touchScroll_ - dy;
      scroll_ = offset < 0 ? 0 : offset > maxScroll() ? maxScroll() : offset;
    }
    return MenuEvent::None;
  }
  const int target = !dragged_ && touchTarget_ == hitTest(x, y) ? touchTarget_ : -1;
  cancelTouch();
  return target >= 0 ? activate(target) : MenuEvent::None;
}

MenuEvent MenuUi::activate(int target) {
  if (!open_) return MenuEvent::None;
  if (target == kBack) return back();
  if (target == kNext) { next(); return MenuEvent::None; }
  if (target == -1 || target == kSelect) target = focus_;
  if (target < 0 || target >= targetCount()) return MenuEvent::None;
  if (target == rowCount()) return back();
  cancelTouch();
  focus_ = target;
  revealFocus();
  switch (page_) {
    case MenuPage::Main:
      if (target == kMainSettings) return MenuEvent::OpenSettings;
      if (target == kMainWifiNetworks) return MenuEvent::WifiNetworks;
      if (target == kMainBrowser) return MenuEvent::Browser;
      page_ = target == kMainUtilities ? MenuPage::Utilities : MenuPage::Games; focus_ = 0;
      break;
    case MenuPage::Utilities:
      return target == kUtilitiesRemoteDisplay ? MenuEvent::RemoteDisplay :
             target == kUtilitiesWifi ? MenuEvent::WifiExplorer : MenuEvent::BluetoothExplorer;
    case MenuPage::Games:
      if (target == kGamesForestFidget) return MenuEvent::PlayForestFidget;
      page_ = target == kGamesPong ? MenuPage::Pong : target == kGamesTetris ? MenuPage::Tetris :
              target == kGamesLeafSweep ? MenuPage::LeafSweep : MenuPage::Doom; focus_ = 0;
      break;
    case MenuPage::Doom:
      return MenuEvent::PlayDoom;
    case MenuPage::Pong:
      if (target == kPongPlay) return MenuEvent::PlayPong;
      if (target == kPongScores) return MenuEvent::ShowPongScores;
      page_ = MenuPage::PongSettings; focus_ = 0;
      break;
    case MenuPage::PongSettings:
      if (target <= kSpeedRight) {
        speeds_[target] = speeds_[target] == 5 ? 1 : speeds_[target] + 1;
        return MenuEvent::SpeedsChanged;
      }
      if (target == kPongMusic) music_[0] = !music_[0];
      else effects_[0] = !effects_[0];
      return MenuEvent::GamePreferencesChanged;
    case MenuPage::Tetris:
      if (target == kTetrisPlay) return MenuEvent::PlayTetris;
      if (target == kTetrisScores) return MenuEvent::ShowTetrisScores;
      page_ = MenuPage::TetrisSettings; focus_ = 0;
      break;
    case MenuPage::LeafSweep:
      if (target == kLeafSweepPlay) return MenuEvent::PlayLeafSweep;
      if (target == kLeafSweepScores) return MenuEvent::ShowLeafSweepScores;
      page_ = MenuPage::LeafSweepSettings; focus_ = 0;
      break;
    case MenuPage::LeafSweepSettings:
      if (target == kLeafSweepMusic) music_[2] = !music_[2];
      else effects_[2] = !effects_[2];
      return MenuEvent::GamePreferencesChanged;
    case MenuPage::TetrisSettings:
      if (target == kTetrisMusic) music_[1] = !music_[1];
      else effects_[1] = !effects_[1];
      return MenuEvent::GamePreferencesChanged;
  }
  scroll_ = 0;
  return MenuEvent::None;
}

MenuEvent MenuUi::back() {
  if (!open_) return MenuEvent::None;
  cancelTouch();
  switch (page_) {
    case MenuPage::Main: close(); return MenuEvent::Closed;
    case MenuPage::Utilities: page_ = MenuPage::Main; focus_ = kMainUtilities; break;
    case MenuPage::Games: page_ = MenuPage::Main; focus_ = kMainGames; break;
    case MenuPage::Pong: page_ = MenuPage::Games; focus_ = kGamesPong; break;
    case MenuPage::PongSettings: page_ = MenuPage::Pong; focus_ = kPongSettings; break;
    case MenuPage::Tetris: page_ = MenuPage::Games; focus_ = kGamesTetris; break;
    case MenuPage::TetrisSettings: page_ = MenuPage::Tetris; focus_ = kTetrisSettings; break;
    case MenuPage::LeafSweep: page_ = MenuPage::Games; focus_ = kGamesLeafSweep; break;
    case MenuPage::LeafSweepSettings: page_ = MenuPage::LeafSweep; focus_ = kLeafSweepSettings; break;
    case MenuPage::Doom: page_ = MenuPage::Games; focus_ = kGamesDoom; break;
  }
  scroll_ = 0;
  revealFocus();
  return MenuEvent::None;
}

int MenuUi::hitTest(int x, int y) const {
  if (!open_ || x < 0 || x >= 240 || y < 0 || y >= 240) return -1;
  if (inside(x, y, 20, 8, 200, 16)) return x < 86 ? kNext : x < 154 ? kBack : kSelect;
  if (inside(x, y, kLeft, kBackY, kWidth, kBackHeight)) return kBack;
  if ((page_ == MenuPage::Main || page_ == MenuPage::Games) && (y < kViewportTop || y >= kViewportBottom)) return -1;
  const bool compact = page_ == MenuPage::PongSettings || page_ == MenuPage::Games;
  for (int target = 0; target < rowCount(); ++target) {
    const int top = kFirstRow + target * (compact ? kCompactPitch : kRowPitch) - scroll_;
    const int height = compact ? kCompactHeight : kRowHeight;
    if (inside(x, y, kLeft, top, kWidth, height)) return target;
  }
  return -1;
}

void MenuUi::setSpeeds(uint8_t left, uint8_t right) {
  speeds_[0] = validSpeed(left); speeds_[1] = validSpeed(right);
}

uint8_t MenuUi::speed(unsigned player) const { return player < 2 ? speeds_[player] : 3; }

void MenuUi::setAudio(unsigned game, bool music, bool effects) {
  if (game >= 3) return;
  music_[game] = music; effects_[game] = effects;
}
bool MenuUi::music(unsigned game) const { return game < 3 && music_[game]; }
bool MenuUi::effects(unsigned game) const { return game < 3 && effects_[game]; }

void drawMenu(uint16_t* pixels, const MenuUi& ui) {
  if (!pixels) return;
  Canvas c = {pixels};
  c.rect(0, 0, 240, 240, ink);
  if (!ui.isOpen()) return;
  const MenuPage page = ui.page();
  if (page == MenuPage::Main) {
    const char* labels[] = {"GAMES", "BROWSER", "WI-FI NETWORKS", "UTILITIES", "SETTINGS"};
    const char* details[] = {"A LITTLE COMPETITION", "OPEN THE WEB OVER WI-FI",
                            "SELECT / CONNECT / REMEMBER", "DISPLAY / RADIO EXPLORERS", "PERSONALIZE YOUR PET"};
    const UiIcon icons[] = {UiIcon::Games, UiIcon::Remote, UiIcon::Wifi, UiIcon::Utilities, UiIcon::Settings};
    for (int index = 0; index < ui.rowCount(); ++index) {
      const int y = kFirstRow + index * kRowPitch - ui.scrollOffset();
      if (y + kRowHeight + 1 <= kViewportTop || y - 1 >= kViewportBottom) continue;
      row(c, y, labels[index], details[index], ui.focus() == index, icons[index]);
    }
    // Mask every scrolling primitive behind the fixed chrome, without another buffer.
    c.rect(0, 0, 240, kViewportTop, ink);
    c.rect(0, kViewportBottom, 240, 240 - kViewportBottom, ink);
    const int track = kViewportBottom - kViewportTop;
    const int thumb = track * track / (track + ui.maxScroll());
    const int y = kViewportTop + ui.scrollOffset() * (track - thumb) / ui.maxScroll();
    c.roundRect(232, kViewportTop, 3, track, 1, rgb(37, 60, 49));
    c.roundRect(232, y, 3, thumb, 1, mint);
    c.centered(193, "SWIPE TO SCROLL", muted);
  } else if (page == MenuPage::Utilities) {
    row(c, 51, "REMOTE DISPLAY", "CONNECT BY WI-FI OR USB", ui.focus() == MenuUi::kUtilitiesRemoteDisplay, UiIcon::Remote);
    row(c, 98, "WI-FI EXPLORER", "NEARBY 2.4GHZ NETWORKS", ui.focus() == MenuUi::kUtilitiesWifi, UiIcon::Wifi);
    row(c, 145, "BLUETOOTH EXPLORER", "NEARBY BLE ADVERTISEMENTS", ui.focus() == MenuUi::kUtilitiesBluetooth, UiIcon::Bluetooth);
  } else if (page == MenuPage::Games) {
    const char* labels[] = {"MOSS PONG", "3-TOED TETRIS", "LEAF SWEEP", "FOREST FIDGET", "DOOM"};
    const char* details[] = {"TWO PLAYERS / TWO BUTTONS", "STACK BLOCKS / CLEAR LINES",
                             "SWEEP LEAVES / DODGE CANS", "TOUCH / TILT / UNWIND", "THE ORIGINAL SHAREWARE EPISODE"};
    const UiIcon icons[] = {UiIcon::Pong, UiIcon::Tetris, UiIcon::LeafSweep, UiIcon::ForestFidget, UiIcon::Games};
    for (int index = 0; index < ui.rowCount(); ++index) {
      const int y = kFirstRow + index * kCompactPitch - ui.scrollOffset();
      if (y + kCompactHeight + 1 <= kViewportTop || y - 1 >= kViewportBottom) continue;
      row(c, y, labels[index], details[index], ui.focus() == index, icons[index], true);
    }
    c.rect(0, 0, 240, kViewportTop, ink);
    c.rect(0, kViewportBottom, 240, 240 - kViewportBottom, ink);
    const int track = kViewportBottom - kViewportTop;
    const int thumb = track * track / (track + ui.maxScroll());
    const int y = kViewportTop + ui.scrollOffset() * (track - thumb) / ui.maxScroll();
    c.roundRect(232, kViewportTop, 3, track, 1, rgb(37, 60, 49));
    c.roundRect(232, y, 3, thumb, 1, mint);
    c.centered(193, "SWIPE TO SCROLL", muted);
  } else if (page == MenuPage::Doom) {
    row(c, 51, "PLAY DOOM", "TOUCH + SIDE BUTTONS", ui.focus() == 0, UiIcon::Play);
  } else if (page == MenuPage::Pong || page == MenuPage::Tetris || page == MenuPage::LeafSweep) {
    row(c, 51, "PLAY", page == MenuPage::Pong ? "LET THE RALLY BEGIN" : page == MenuPage::Tetris ? "A LITTLE BLOCK THERAPY" : "A LITTLE LEAF DETECTIVE", ui.focus() == 0, UiIcon::Play);
    row(c, 98, "SOUND / OPTIONS", "MAKE THE GAME YOUR OWN", ui.focus() == 1, UiIcon::Settings);
    row(c, 145, "HIGH SCORES", "YOUR BEST GAMES", ui.focus() == 2, UiIcon::Trophy);
  } else if (page == MenuPage::PongSettings) {
    speedRow(c, 51, 0, ui.speed(0), ui.focus() == MenuUi::kSpeedLeft);
    speedRow(c, 87, 1, ui.speed(1), ui.focus() == MenuUi::kSpeedRight);
    audioRow(c, 123, "MUSIC", ui.music(0), ui.focus() == MenuUi::kPongMusic, true, UiIcon::Music);
    audioRow(c, 159, "EFFECTS", ui.effects(0), ui.focus() == MenuUi::kPongEffects, true, UiIcon::Effects);
  } else {
    const unsigned game = page == MenuPage::LeafSweepSettings ? 2 : 1;
    audioRow(c, 51, "MUSIC", ui.music(game), ui.focus() == 0, false, UiIcon::Music);
    audioRow(c, 98, "EFFECTS", ui.effects(game), ui.focus() == 1, false, UiIcon::Effects);
  }
  drawNavigationBar(c);
  c.centered(30, page == MenuPage::Main ? "MENU" : page == MenuPage::Games ? "GAMES" :
             page == MenuPage::Utilities ? "UTILITIES" :
             page == MenuPage::Pong ? "MOSS PONG" : page == MenuPage::PongSettings ? "PONG OPTIONS" :
             page == MenuPage::Tetris ? "3-TOED TETRIS" : page == MenuPage::TetrisSettings ? "3-TOED OPTIONS" :
             page == MenuPage::LeafSweep ? "LEAF SWEEP" : page == MenuPage::Doom ? "DOOM" : "SWEEP OPTIONS", cream, 2);
  if (ui.focus() == ui.rowCount()) c.roundRect(kLeft - 1, kBackY - 1, kWidth + 2, kBackHeight + 2, 5, gold);
  c.roundRect(kLeft, kBackY, kWidth, kBackHeight, 4, rgb(29, 53, 47));
  const char* back = page == MenuPage::Main ? "BACK TO PET" : "BACK";
  const int backX = (240 - (static_cast<int>(strlen(back)) * 6 - 1 + 20)) / 2;
  drawUiIcon(c, backX, 208, UiIcon::Back, mint);
  c.text(backX + 20, 213, back, cream);
}

}  // namespace sloth
