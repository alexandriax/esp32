#include "game_overlay.h"
#include "navigation_bar.h"
#include "ui_icons.h"
#include <stdio.h>
#include <string.h>

namespace sloth {
#if __cplusplus < 201703L
constexpr int GameOverlay::kKeyboardTop;
constexpr int GameOverlay::kKeyboardBottom;
constexpr int GameOverlay::kKeyWidth;
constexpr int GameOverlay::kKeyHeight;
constexpr int GameOverlay::kMaxScroll;
#endif
namespace {
bool inside(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}
void button(graphics::Canvas& c, int x, int y, int w, int h, const char* text, bool selected, UiIcon icon = UiIcon::None, int scale = 1) {
  using namespace graphics;
  c.roundRect(x, y, w, h, 3, selected ? mint : rgb(31, 63, 54));
  const bool hasIcon = icon != UiIcon::None;
  const int start = x + (w - static_cast<int>(strlen(text)) * 6 * scale - (hasIcon ? 20 : 0)) / 2;
  if (hasIcon) drawUiIcon(c, start, y + (h - 16) / 2, icon, selected ? ink : mint);
  c.text(start + (hasIcon ? 20 : 0), y + (h - 7 * scale) / 2, text, selected ? ink : cream, scale);
}
}

KeyboardSpec GameOverlay::keyboardSpec() const {
  KeyboardSpec s;s.text=name_;s.keys="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";s.title="NAME YOUR SCORE";s.placeholder=fallback_;
  s.capacity=sizeof(name_);s.targetCount=41;s.cleanSpaces=true;s.showCancel=true;s.actions[0]=37;s.actions[1]=38;s.actions[2]=40;s.actions[3]=39;
  s.labels[2]="SKIP";s.labels[3]="SAVE";s.navNext=kNext;s.navBack=kBack;s.navSelect=kSelect;return s;
}
GameOverlayEvent GameOverlay::keyboardEvent(KeyboardEvent event){
  if(event==KeyboardEvent::Back)return back();
  if(event==KeyboardEvent::Cancel)return GameOverlayEvent::Skip;
  if(event==KeyboardEvent::Done)return GameOverlayEvent::Save;
  return GameOverlayEvent::None;
}
void GameOverlay::confirmExit(GameKind game) {
  cancelTouch();
  game_ = game; page_ = GameOverlayPage::ConfirmExit; focus_ = 0;
}
void GameOverlay::enterName(GameKind game, uint32_t score, uint16_t detail, const char* fallback) {
  cancelTouch();
  game_ = game; page_ = GameOverlayPage::Name; focus_ = 39;keyboard_.reset(39);
  name_[0] = 0; result_ = GameScore(); result_.score = score; result_.detail = detail;
  unsigned n = 0;
  if (fallback) for (unsigned i = 0; fallback[i] && n < kScoreNameLength; ++i) {
    char ch = fallback[i];
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '\'' || (ch == ' ' && n)) fallback_[n++] = ch;
  }
  while (n && fallback_[n - 1] == ' ') --n;
  fallback_[n] = 0;
  if (!n) memcpy(fallback_, "MOSS", 5);
}
void GameOverlay::showScores(GameKind game) {
  cancelTouch();
  game_ = game; page_ = GameOverlayPage::Scores; focus_ = 0; scorePage_ = 0;
}
int GameOverlay::targetCount() const {
  if (page_ == GameOverlayPage::Name) return 41;
  return isOpen() ? 2 : 0;
}
void GameOverlay::next() {
  if(page_==GameOverlayPage::Name){keyboard_.next(keyboardSpec());return;}
  cancelTouch();
  if (targetCount()) focus_ = (focus_ + 1) % targetCount();

}
void GameOverlay::cancelTouch() {
  keyboard_.cancelTouch();
  if (touching_) releaseRequired_ = true;
  touching_ = dragged_ = false;
  touchTarget_ = -1;
}
GameOverlayEvent GameOverlay::touch(bool down, int x, int y) {
  if (releaseRequired_) {
    if (!down) releaseRequired_ = false;
    return GameOverlayEvent::None;
  }
  if (!isOpen()) return GameOverlayEvent::None;
  if(page_==GameOverlayPage::Name)return keyboardEvent(keyboard_.touch(keyboardSpec(),name_,down,x,y));
  if (down && !touching_) {
    touching_ = true; dragged_ = false;
    touchX_ = x; touchY_ = y; touchTarget_ = hitTest(x,y);
    if (touchTarget_ >= 0 && touchTarget_ < targetCount()) focus_ = touchTarget_;
  }
  if (!touching_) return GameOverlayEvent::None;
  const int64_t dx = static_cast<int64_t>(x) - touchX_, dy = static_cast<int64_t>(y) - touchY_;
  if (dx > 10 || dx < -10 || dy > 10 || dy < -10) dragged_ = true;
  if (down) return GameOverlayEvent::None;
  // Tap the original target if the finger drifted modestly into its gap, but
  // never activate a different adjacent key or anything outside the screen.
  const int releasedTarget = hitTest(x,y);
  const bool same = releasedTarget == touchTarget_ || releasedTarget < 0;
  const int target = !dragged_ && same && x >= 0 && x < 240 && y >= 0 && y < 240 ? touchTarget_ : -1;
  touching_ = dragged_ = false; touchTarget_ = -1;
  return target >= 0 ? activate(target) : GameOverlayEvent::None;
}
GameOverlayEvent GameOverlay::back() {
  const auto result = page_ == GameOverlayPage::ConfirmExit ? GameOverlayEvent::Resume : GameOverlayEvent::Closed;
  close(); return result;
}
const char* GameOverlay::submittedName() const {
  for (unsigned i = 0; name_[i]; ++i) if (name_[i] != ' ') return name_;
  return fallback_;
}
GameOverlayEvent GameOverlay::activate(int target) {
  if(page_==GameOverlayPage::Name){if(target==kBack)return back();return keyboardEvent(keyboard_.activate(keyboardSpec(),name_,target));}
  cancelTouch();
  if (!isOpen()) return GameOverlayEvent::None;
  if (target == kNext) { next(); return GameOverlayEvent::None; }
  if (target == kBack) return back();
  if (target == kSelect || target == -1) target = focus_;
  if (target < 0 || target >= targetCount()) return GameOverlayEvent::None;
  focus_ = target;

  if (page_ == GameOverlayPage::ConfirmExit) {
    close(); return target == 0 ? GameOverlayEvent::Resume : GameOverlayEvent::Leave;
  }
  if (page_ == GameOverlayPage::Scores) {
    if (target == 1) return back();
    scorePage_ ^= 1; return GameOverlayEvent::None;
  }
  return GameOverlayEvent::None;
}

int GameOverlay::hitTest(int x, int y) const {
  if (!isOpen()) return -1;
  if (inside(x, y, 20, 8, 200, 16)) return x < 86 ? kNext : x < 154 ? kBack : kSelect;
  if (page_ == GameOverlayPage::ConfirmExit) {
    if (inside(x, y, 42, 128, 156, 25)) return 0;
    if (inside(x, y, 42, 162, 156, 25)) return 1;
  } else if (page_ == GameOverlayPage::Scores) {
    if (inside(x, y, 24, 204, 93, 23)) return 0;
    if (inside(x, y, 123, 204, 93, 23)) return 1;
  } else if (page_ == GameOverlayPage::Name) return keyboard_.hitTest(keyboardSpec(),x,y);
  return -1;
}

void drawGameOverlay(uint16_t* pixels, const GameOverlay& ui, const GameRecords& records,
                     bool savePending, bool saveFailed) {
  if (!pixels || !ui.isOpen()) return;
  using namespace graphics;
  Canvas c{pixels};
  if (ui.page() == GameOverlayPage::ConfirmExit) {
    // Keep the paused field visible, with a clearly bounded confirmation card.
    c.roundRect(26, 65, 188, 134, 9, cream);
    c.centered(80, "GAME PAUSED", ink, 2);
    c.centered(102, "LEAVE THIS GAME?", ink);
    c.centered(114, "UNFINISHED SCORE IS NOT SAVED", mask);
    button(c, 42, 128, 156, 25, "CONTINUE", ui.focus() == 0, UiIcon::Continue);
    button(c, 42, 162, 156, 25, "LEAVE GAME", ui.focus() == 1, UiIcon::Back);
    drawNavigationBar(c);
    return;
  }
  c.rect(0, 0, 240, 240, ink); drawNavigationBar(c);
  if(ui.page()==GameOverlayPage::Name){drawKeyboard(pixels,ui.keyboardSpec(),ui.keyboard());return;}
  c.centered(32, gameTitle(ui.game()), gold);
  const auto& table = records.tables[static_cast<unsigned>(ui.game())];
  char line[48], score[24];
  if (table.count) {
    formatGameScore(ui.game(), table.entries[0], score, sizeof(score));
    snprintf(line, sizeof(line), "BEST %s", score);
  } else snprintf(line, sizeof(line), "YOUR FIRST SCORE GOES HERE");
  c.centered(47, line, cream);
  const unsigned start = ui.scorePage() * 5;
  for (unsigned row = 0; row < 5; ++row) {
    const unsigned index = start + row;
    const int y = 66 + row * 24;
    c.roundRect(22, y, 196, 21, 3, rgb(24, 48, 43));
    snprintf(line, sizeof(line), "%u", index + 1); c.text(28, y + 7, line, muted);
    if (index < table.count) {
      c.text(45, y + 7, table.entries[index].name, cream);
      formatGameScore(ui.game(), table.entries[index], score, sizeof(score));
      c.text(210 - static_cast<int>(strlen(score)) * 6, y + 7, score, gold);
    } else c.text(45, y + 7, "-", muted);
  }
  c.centered(192, saveFailed ? "SAVE FAILED - RETRYING" : savePending ? "SAVING..." :
      ui.game() == GameKind::Pong ? "RANKED BY WINNING MARGIN" : "POINTS / TOP TEN", muted);
  button(c, 24, 204, 93, 23, ui.scorePage() ? "1-5" : "6-10", ui.focus() == 0, UiIcon::Trophy);
  button(c, 123, 204, 93, 23, "BACK", ui.focus() == 1, UiIcon::Back);
}
}  // namespace sloth
