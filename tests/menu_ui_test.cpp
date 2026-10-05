#include "../firmware/sloth_pet/menu_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

using sloth::MenuEvent;
using sloth::MenuPage;
using sloth::MenuUi;

static void openPage(MenuUi& ui, MenuPage page) {
  ui.open();
  if (page == MenuPage::Main) return;
  if (page == MenuPage::Utilities) { ui.activate(MenuUi::kMainUtilities); return; }
  ui.activate(MenuUi::kMainGames);
  if (page == MenuPage::Games) return;
  const bool doom = page == MenuPage::Doom;
  const bool tetris = page == MenuPage::Tetris || page == MenuPage::TetrisSettings;
  const bool leaf = page == MenuPage::LeafSweep || page == MenuPage::LeafSweepSettings;
  ui.activate(doom ? MenuUi::kGamesDoom : leaf ? MenuUi::kGamesLeafSweep :
      tetris ? MenuUi::kGamesTetris : MenuUi::kGamesPong);
  if (page == MenuPage::PongSettings || page == MenuPage::TetrisSettings || page == MenuPage::LeafSweepSettings) ui.activate(1);
  assert(ui.page() == page);
}

static MenuEvent hardwareSelect(MenuUi& ui, int target) {
  assert(target >= 0 && target < ui.targetCount());
  for (int i = 0; ui.focus() != target && i < ui.targetCount(); ++i) ui.next();
  assert(ui.focus() == target);
  return ui.activate();
}

static void navigationAndExternalHandoffs() {
  MenuUi ui;
  assert(!ui.isOpen() && ui.rowCount() == 0 && ui.targetCount() == 0);
  assert(ui.activate() == MenuEvent::None && ui.back() == MenuEvent::None);
  ui.next(); ui.open();
  assert(ui.page() == MenuPage::Main && ui.focus() == MenuUi::kMainGames && ui.targetCount() == 6);
  assert(hardwareSelect(ui, MenuUi::kMainSettings) == MenuEvent::OpenSettings);
  assert(ui.isOpen() && ui.page() == MenuPage::Main && ui.focus() == MenuUi::kMainSettings);
  assert(hardwareSelect(ui, MenuUi::kMainBrowser) == MenuEvent::Browser);
  assert(ui.page() == MenuPage::Main && ui.focus() == MenuUi::kMainBrowser);
  assert(hardwareSelect(ui, MenuUi::kMainWifiNetworks) == MenuEvent::WifiNetworks);
  assert(ui.page() == MenuPage::Main && ui.focus() == MenuUi::kMainWifiNetworks);
  assert(hardwareSelect(ui, MenuUi::kMainUtilities) == MenuEvent::None);
  assert(ui.page() == MenuPage::Utilities && ui.rowCount() == 3);
  const MenuEvent utilities[] = {MenuEvent::RemoteDisplay,MenuEvent::WifiExplorer,MenuEvent::BluetoothExplorer};
  for(int target=0;target<3;++target){
    assert(hardwareSelect(ui,target)==utilities[target]);
    assert(ui.isOpen()&&ui.page()==MenuPage::Utilities&&ui.focus()==target);
    assert(ui.activate()==utilities[target]); // Return destination remains selected.
    assert(ui.hitTest(80,60+target*47)==target);
  }
  assert(hardwareSelect(ui,ui.rowCount())==MenuEvent::None);
  assert(ui.page()==MenuPage::Main&&ui.focus()==MenuUi::kMainUtilities);
  assert(hardwareSelect(ui, MenuUi::kMainGames) == MenuEvent::None);
  assert(ui.page() == MenuPage::Games && ui.rowCount() == 5 && ui.targetCount() == 6);
  for (unsigned game = 0; game < 3; ++game) {
    const MenuPage gamePages[] = {MenuPage::Pong, MenuPage::Tetris, MenuPage::LeafSweep};
    const MenuPage options[] = {MenuPage::PongSettings, MenuPage::TetrisSettings, MenuPage::LeafSweepSettings};
    const MenuEvent play[] = {MenuEvent::PlayPong, MenuEvent::PlayTetris, MenuEvent::PlayLeafSweep};
    const MenuEvent scores[] = {MenuEvent::ShowPongScores, MenuEvent::ShowTetrisScores, MenuEvent::ShowLeafSweepScores};
    assert(hardwareSelect(ui, static_cast<int>(game)) == MenuEvent::None);
    assert(ui.page() == gamePages[game] && ui.targetCount() == 4);
    assert(hardwareSelect(ui, 0) == play[game]);
    assert(ui.isOpen() && ui.focus() == 0);
    assert(hardwareSelect(ui, 2) == scores[game]);
    assert(ui.isOpen() && ui.focus() == 2); // Returning from score view retains its row.
    assert(hardwareSelect(ui, 1) == MenuEvent::None);
    assert(ui.page() == options[game]);
    assert(ui.targetCount() == (game ? 3 : 5));
    assert(hardwareSelect(ui, ui.rowCount()) == MenuEvent::None);
    assert(ui.page() == gamePages[game] && ui.focus() == 1);
    assert(hardwareSelect(ui, ui.rowCount()) == MenuEvent::None);
    assert(ui.page() == MenuPage::Games && ui.focus() == static_cast<int>(game));
  }
  assert(hardwareSelect(ui, MenuUi::kGamesForestFidget) == MenuEvent::PlayForestFidget);
  assert(ui.isOpen() && ui.page() == MenuPage::Games && ui.focus() == 3);
  assert(ui.activate() == MenuEvent::PlayForestFidget); // Return/relaunch keeps direct row focus.
  assert(ui.rowCount() == 5 && ui.targetCount() == 6);
  assert(hardwareSelect(ui, ui.rowCount()) == MenuEvent::None);
  assert(ui.page() == MenuPage::Main && ui.focus() == MenuUi::kMainGames);
  assert(hardwareSelect(ui, ui.rowCount()) == MenuEvent::Closed && !ui.isOpen());
}

static void independentPreferences() {
  MenuUi ui;
  assert(ui.speed(0) == 3 && ui.speed(1) == 3 && ui.speed(99) == 3);
  assert(!ui.music(0) && !ui.music(1) && ui.effects(0) && ui.effects(1));
  assert(!ui.music(2) && ui.effects(2));
  assert(!ui.music(99) && !ui.effects(99));
  ui.setAudio(99, true, false);
  assert(!ui.music(0) && ui.effects(0));
  ui.setSpeeds(0, 255);
  assert(ui.speed(0) == 1 && ui.speed(1) == 5);
  openPage(ui, MenuPage::PongSettings);
  for (unsigned expected = 2; expected <= 5; ++expected) {
    assert(ui.activate(MenuUi::kSpeedLeft) == MenuEvent::SpeedsChanged);
    assert(ui.speed(0) == expected && ui.speed(1) == 5);
  }
  assert(ui.activate(MenuUi::kSpeedLeft) == MenuEvent::SpeedsChanged);
  assert(ui.activate(MenuUi::kSpeedRight) == MenuEvent::SpeedsChanged);
  assert(ui.speed(0) == 1 && ui.speed(1) == 1);
  assert(ui.activate(MenuUi::kPongMusic) == MenuEvent::GamePreferencesChanged);
  assert(ui.music(0) && ui.effects(0) && !ui.music(1) && ui.effects(1));
  assert(ui.activate(MenuUi::kPongEffects) == MenuEvent::GamePreferencesChanged);
  assert(ui.music(0) && !ui.effects(0) && !ui.music(1) && ui.effects(1));
  openPage(ui, MenuPage::TetrisSettings);
  assert(ui.activate(MenuUi::kTetrisMusic) == MenuEvent::GamePreferencesChanged);
  assert(ui.activate(MenuUi::kTetrisEffects) == MenuEvent::GamePreferencesChanged);
  assert(ui.music(1) && !ui.effects(1) && ui.music(0) && !ui.effects(0));
  assert(ui.speed(0) == 1 && ui.speed(1) == 1);
  ui.setAudio(0, false, true);
  assert(!ui.music(0) && ui.effects(0) && ui.music(1) && !ui.effects(1));
  ui.close(); ui.open();
  assert(ui.speed(0) == 1 && ui.speed(1) == 1);
  assert(!ui.music(0) && ui.effects(0) && ui.music(1) && !ui.effects(1));
  openPage(ui, MenuPage::TetrisSettings);
  assert(ui.activate(MenuUi::kTetrisMusic) == MenuEvent::GamePreferencesChanged);
  assert(ui.activate(MenuUi::kTetrisEffects) == MenuEvent::GamePreferencesChanged);
  assert(!ui.music(1) && ui.effects(1));
  openPage(ui, MenuPage::LeafSweepSettings);
  assert(ui.activate(MenuUi::kLeafSweepMusic) == MenuEvent::GamePreferencesChanged);
  assert(ui.activate(MenuUi::kLeafSweepEffects) == MenuEvent::GamePreferencesChanged);
  assert(ui.music(2) && !ui.effects(2));
  assert(!ui.music(0) && ui.effects(0) && !ui.music(1) && ui.effects(1));
  ui.setAudio(2, false, true);
  // Forest Fidget does not add an audio preference slot or alter existing games.
  ui.setAudio(3, true, false);
  assert(!ui.music(3) && !ui.effects(3));
  openPage(ui, MenuPage::Games);
  assert(ui.activate(MenuUi::kGamesForestFidget) == MenuEvent::PlayForestFidget);
  for(unsigned game = 0; game < 3; ++game) assert(!ui.music(game) && ui.effects(game));
  ui.close(); ui.open();
  assert(!ui.music(2) && ui.effects(2));
}

static void navigationRailTouchActions() {
  MenuUi ui; ui.open();
  assert(ui.hitTest(20, 8) == MenuUi::kNext && ui.hitTest(85, 23) == MenuUi::kNext);
  assert(ui.hitTest(86, 8) == MenuUi::kBack && ui.hitTest(153, 23) == MenuUi::kBack);
  assert(ui.hitTest(154, 8) == MenuUi::kSelect && ui.hitTest(219, 23) == MenuUi::kSelect);
  assert(ui.hitTest(19, 12) == -1 && ui.hitTest(220, 12) == -1);
  assert(ui.hitTest(120, 7) == -1 && ui.hitTest(120, 24) == -1);
  assert(ui.activate(MenuUi::kNext) == MenuEvent::None && ui.focus() == 1);
  assert(ui.activate(MenuUi::kSelect) == MenuEvent::Browser);
  assert(ui.activate(MenuUi::kNext) == MenuEvent::None && ui.focus() == 2);
  assert(ui.activate(MenuUi::kSelect) == MenuEvent::WifiNetworks);
  assert(ui.activate(MenuUi::kNext) == MenuEvent::None && ui.focus() == 3);
  assert(ui.activate(MenuUi::kSelect) == MenuEvent::None && ui.page() == MenuPage::Utilities);
  assert(ui.activate(MenuUi::kSelect) == MenuEvent::RemoteDisplay);
  assert(ui.activate(MenuUi::kBack) == MenuEvent::None && ui.page() == MenuPage::Main);
  assert(ui.activate(MenuUi::kNext) == MenuEvent::None && ui.focus() == 4);
  assert(ui.activate(MenuUi::kSelect) == MenuEvent::OpenSettings);
  assert(ui.activate(MenuUi::kNext) == MenuEvent::None && ui.focus() == ui.rowCount());
  assert(ui.activate(MenuUi::kSelect) == MenuEvent::Closed && !ui.isOpen());
  openPage(ui, MenuPage::TetrisSettings);
  assert(ui.activate(MenuUi::kBack) == MenuEvent::None && ui.page() == MenuPage::Tetris);
  assert(ui.focus() == MenuUi::kTetrisSettings);
  ui.close();
  assert(ui.hitTest(120, 12) == -1 && ui.activate(MenuUi::kSelect) == MenuEvent::None);
}

static void touchHitTargetsAndRendering() {
  MenuUi ui;
  uint16_t frame[240 * 240 + 2], before[240 * 240];
  frame[0] = frame[240 * 240 + 1] = 0xA55A;
  assert(ui.hitTest(30, 60) == -1);
  sloth::drawMenu(frame + 1, ui);
  ui.open();
  assert(ui.hitTest(12, 51) == MenuUi::kMainGames && ui.hitTest(227, 90) == MenuUi::kMainGames);
  assert(ui.hitTest(11, 60) == -1 && ui.hitTest(228, 60) == -1);
  assert(ui.hitTest(120, 91) == -1 && ui.hitTest(120, 97) == -1);
  assert(ui.hitTest(120, 98) == MenuUi::kMainBrowser && ui.hitTest(120, 137) == MenuUi::kMainBrowser);
  assert(ui.hitTest(120, 138) == -1 && ui.hitTest(120, 144) == -1);
  assert(ui.hitTest(120, 145) == MenuUi::kMainWifiNetworks && ui.hitTest(120, 184) == MenuUi::kMainWifiNetworks);
  assert(ui.hitTest(120, 192) == -1 && ui.hitTest(120, 239) == -1);
  assert(ui.hitTest(120, 205) == MenuUi::kBack && ui.hitTest(120, 226) == MenuUi::kBack);
  assert(ui.hitTest(120, 227) == -1 && ui.hitTest(120, 240) == -1);
  assert(ui.activate(-2) == MenuEvent::None && ui.activate(6) == MenuEvent::None);
  const MenuPage pages[] = {MenuPage::Main, MenuPage::Utilities, MenuPage::Games, MenuPage::Pong, MenuPage::PongSettings,
                            MenuPage::Tetris, MenuPage::TetrisSettings, MenuPage::LeafSweep, MenuPage::LeafSweepSettings,
                            MenuPage::Doom};
  for (MenuPage page : pages) {
    openPage(ui, page);
    for (int focus = 0; focus < ui.targetCount(); ++focus) {
      assert(ui.focus() == focus);
      sloth::drawMenu(frame + 1, ui);
      assert(ui.page() == page && ui.focus() == focus);
      assert(frame[0] == 0xA55A && frame[240 * 240 + 1] == 0xA55A);
      for (int y = -1; y <= 240; ++y)
        for (int x = -1; x <= 240; ++x) {
          const int hit = ui.hitTest(x, y);
          assert(hit == -1 || hit == MenuUi::kBack || hit == MenuUi::kNext || hit == MenuUi::kSelect ||
                 (hit >= 0 && hit < ui.rowCount()));
        }
      if (focus == 0) memcpy(before, frame + 1, sizeof(before));
      if (focus == ui.rowCount())
        assert(memcmp(before + 203 * 240, frame + 1 + 203 * 240, 25 * 240 * sizeof(uint16_t)) != 0);
      ui.next();
    }
    assert(ui.focus() == 0); // Every hardware cycle includes Back and wraps once.
  }
  openPage(ui, MenuPage::Games);
  for(int target = 0; target < 4; ++target) {
    const int top = 51 + target * 36;
    for(int y = top; y < top + 30 && y < 187; ++y)
      for(int x = 12; x < 228; ++x) assert(ui.hitTest(x,y) == target);
    assert(ui.hitTest(11,top) == -1 && ui.hitTest(228,top) == -1);
    assert(ui.hitTest(120,top+30) == -1 && ui.hitTest(120,top+35) == -1);
  }
  assert(ui.activate(ui.hitTest(120, 170)) == MenuEvent::PlayForestFidget);
  assert(ui.page() == MenuPage::Games && ui.focus() == MenuUi::kGamesForestFidget);
  assert(ui.activate(ui.hitTest(120, 215)) == MenuEvent::None && ui.page() == MenuPage::Main);
  const MenuPage games[] = {MenuPage::Pong, MenuPage::Tetris, MenuPage::LeafSweep};
  for(int target = 0; target < 3; ++target) {
    openPage(ui, MenuPage::Games);
    assert(ui.activate(ui.hitTest(120, 60 + target * 36)) == MenuEvent::None);
    assert(ui.page() == games[target]);
  }
  assert(ui.activate(ui.hitTest(120, 60)) == MenuEvent::PlayLeafSweep);
  assert(ui.activate(ui.hitTest(120, 160)) == MenuEvent::ShowLeafSweepScores);
  openPage(ui, MenuPage::PongSettings);
  assert(ui.hitTest(12, 51) == MenuUi::kSpeedLeft);
  assert(ui.hitTest(120, 81) == -1 && ui.hitTest(120, 86) == -1);
  assert(ui.hitTest(12, 87) == MenuUi::kSpeedRight);
  assert(ui.hitTest(12, 123) == MenuUi::kPongMusic && ui.hitTest(227, 188) == MenuUi::kPongEffects);
  ui.setSpeeds(1, 1); sloth::drawMenu(before, ui);
  ui.setSpeeds(5, 5); sloth::drawMenu(frame + 1, ui);
  assert(memcmp(before, frame + 1, sizeof(before)) != 0);
  sloth::drawMenu(before, ui); ui.activate(ui.hitTest(190, 130)); sloth::drawMenu(frame + 1, ui);
  assert(ui.music(0) && memcmp(before, frame + 1, sizeof(before)) != 0);
  assert(ui.activate(ui.hitTest(120, 210)) == MenuEvent::None && ui.page() == MenuPage::Pong);
  ui.close();
  assert(ui.hitTest(30, 60) == -1 && ui.activate(MenuUi::kBack) == MenuEvent::None);
  sloth::drawMenu(NULL, ui);
}


static void assertMainFocusVisible(const MenuUi& ui) {
  assert(ui.isOpen() && ui.page() == MenuPage::Main);
  assert(ui.scrollOffset() >= 0 && ui.scrollOffset() <= ui.maxScroll());
  if (ui.focus() == ui.rowCount()) {
    assert(ui.hitTest(120, 205) == MenuUi::kBack && ui.hitTest(120, 226) == MenuUi::kBack);
    return;
  }
  const int top = 51 + 47 * ui.focus() - ui.scrollOffset();
  assert(top - 1 >= 50 && top + 40 < 187); // Whole row and focus outline are visible.
  for (int y = top; y < top + 40; ++y) assert(ui.hitTest(120, y) == ui.focus());
}

static void mainScrollAndFocus() {
  MenuUi ui;
  assert(ui.maxScroll() == 0);
  ui.open();
  assert(ui.maxScroll() == 93 && ui.scrollOffset() == 0);
  for (int target = 0; target < ui.targetCount(); ++target) {
    assert(ui.focus() == target);
    assertMainFocusVisible(ui);
    ui.next();
  }
  assert(ui.focus() == 0 && ui.scrollOffset() == 0);
  assertMainFocusVisible(ui);

  // External screens return to the same fully visible menu row.
  const int targets[] = {MenuUi::kMainBrowser, MenuUi::kMainWifiNetworks, MenuUi::kMainSettings};
  const MenuEvent events[] = {MenuEvent::Browser, MenuEvent::WifiNetworks, MenuEvent::OpenSettings};
  for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
    assert(hardwareSelect(ui, targets[i]) == events[i]);
    assertMainFocusVisible(ui);
    const int offset = ui.scrollOffset();
    assert(ui.activate() == events[i]);
    assert(ui.focus() == targets[i] && ui.scrollOffset() == offset);
    assertMainFocusVisible(ui);
  }
  assert(hardwareSelect(ui, MenuUi::kMainUtilities) == MenuEvent::None);
  assert(ui.page() == MenuPage::Utilities && ui.scrollOffset() == 0 && ui.maxScroll() == 0);
  assert(ui.back() == MenuEvent::None && ui.focus() == MenuUi::kMainUtilities);
  assertMainFocusVisible(ui);
  assert(hardwareSelect(ui, MenuUi::kMainGames) == MenuEvent::None);
  assert(ui.page() == MenuPage::Games && ui.scrollOffset() == 0 && ui.maxScroll() == 39);
  assert(ui.back() == MenuEvent::None && ui.focus() == MenuUi::kMainGames);
  assertMainFocusVisible(ui);
  ui.close(); ui.open();
  assert(ui.focus() == 0 && ui.scrollOffset() == 0);
}

static void mainTouchGestures() {
  MenuUi ui; ui.open();
  // A press alone never launches a screen, and only a matching release activates.
  assert(ui.touch(true, 120, 110) == MenuEvent::None);
  assert(ui.focus() == MenuUi::kMainGames && ui.page() == MenuPage::Main);
  assert(ui.touch(false, 123, 113) == MenuEvent::Browser);
  assert(ui.focus() == MenuUi::kMainBrowser && ui.page() == MenuPage::Main);
  assert(ui.touch(false, 123, 113) == MenuEvent::None);
  assert(ui.touch(true, 120, 137) == MenuEvent::None);
  assert(ui.touch(false, 120, 138) == MenuEvent::None); // Gap, within tap movement tolerance.
  assert(ui.focus() == MenuUi::kMainBrowser);
  assert(ui.touch(true, 11, 110) == MenuEvent::None);
  assert(ui.touch(false, 12, 110) == MenuEvent::None); // Entering a row does not count as a tap.

  ui.open();
  assert(ui.touch(true, 120, 165) == MenuEvent::None);
  assert(ui.touch(true, 120, 65) == MenuEvent::None);
  assert(ui.scrollOffset() == ui.maxScroll());
  assert(ui.touch(false, 120, 65) == MenuEvent::None);
  assert(ui.focus() == 0 && ui.page() == MenuPage::Main);
  assert(ui.hitTest(120, 49) == -1 && ui.hitTest(120, 50) == -1);
  assert(ui.hitTest(120, 52) == MenuUi::kMainWifiNetworks);
  assert(ui.hitTest(120, 99) == MenuUi::kMainUtilities);
  assert(ui.hitTest(120, 146) == MenuUi::kMainSettings);
  assert(ui.hitTest(120, 185) == MenuUi::kMainSettings && ui.hitTest(120, 187) == -1);
  assert(ui.touch(true, 120, 160) == MenuEvent::None);
  assert(ui.touch(false, 120, 160) == MenuEvent::OpenSettings);
  assertMainFocusVisible(ui);
  assert(ui.touch(true, 120, 80) == MenuEvent::None);
  assert(ui.touch(true, 120, 10000) == MenuEvent::None);
  assert(ui.scrollOffset() == 0);
  assert(ui.touch(false, 120, 10000) == MenuEvent::None);
  assert(ui.page() == MenuPage::Main); // A large downward swipe cannot close the menu.
  assert(ui.touch(true, 120, 80) == MenuEvent::None);
  assert(ui.touch(true, 120, -10000) == MenuEvent::None);
  assert(ui.scrollOffset() == ui.maxScroll());
  assert(ui.touch(false, 120, -10000) == MenuEvent::None);

  // Once a drag is recognized, returning to its origin never becomes a tap.
  ui.open();
  assert(ui.touch(true, 120, 110) == MenuEvent::None);
  assert(ui.touch(true, 120, 60) == MenuEvent::None && ui.scrollOffset() == 50);
  assert(ui.touch(true, 120, 110) == MenuEvent::None && ui.scrollOffset() == 0);
  assert(ui.touch(false, 120, 110) == MenuEvent::None);
  assert(ui.focus() == 0 && ui.page() == MenuPage::Main);
  assert(ui.touch(true, 120, 110) == MenuEvent::None);
  assert(ui.touch(false, 120, 125) == MenuEvent::None); // Motion seen only at release still cancels.
  assert(ui.focus() == 0 && ui.scrollOffset() == 0);
  assert(ui.touch(true, 120, 110) == MenuEvent::None);
  assert(ui.touch(true, 150, 110) == MenuEvent::None);
  assert(ui.touch(false, 150, 110) == MenuEvent::None); // Horizontal swipe cannot open Browser.
  assert(ui.focus() == 0 && ui.scrollOffset() == 0);

  // Fixed controls cannot start a scroll or activate after being dragged.
  const int fixedX[] = {120, 45, 120, 185};
  const int fixedY[] = {215, 15, 15, 15};
  for (unsigned i = 0; i < sizeof(fixedX) / sizeof(fixedX[0]); ++i) {
    assert(ui.touch(true, fixedX[i], fixedY[i]) == MenuEvent::None);
    assert(ui.touch(true, fixedX[i], 110) == MenuEvent::None);
    assert(ui.touch(true, fixedX[i], fixedY[i]) == MenuEvent::None);
    assert(ui.touch(false, fixedX[i], fixedY[i]) == MenuEvent::None);
    assert(ui.isOpen() && ui.focus() == 0 && ui.scrollOffset() == 0 && ui.page() == MenuPage::Main);
  }
  assert(ui.touch(true, 45, 15) == MenuEvent::None);
  assert(ui.touch(false, 45, 15) == MenuEvent::None && ui.focus() == MenuUi::kMainBrowser);
  assert(ui.touch(true, 185, 15) == MenuEvent::None);
  assert(ui.touch(false, 185, 15) == MenuEvent::Browser);
  assert(ui.touch(true, 120, 215) == MenuEvent::None);
  assert(ui.touch(false, 120, 215) == MenuEvent::Closed && !ui.isOpen());

  // Cancellation, hardware navigation and page transitions all clear pending taps.
  ui.open(); ui.touch(true, 120, 110); ui.cancelTouch();
  assert(ui.touch(false, 120, 110) == MenuEvent::None && ui.focus() == 0);
  ui.touch(true, 120, 110); ui.next();
  assert(ui.touch(false, 120, 110) == MenuEvent::None && ui.focus() == MenuUi::kMainBrowser);
  ui.touch(true, 120, 110); ui.close(); ui.open();
  assert(ui.touch(false, 120, 110) == MenuEvent::None && ui.focus() == 0);
  ui.touch(true, 120, 110); ui.open();
  assert(ui.touch(false, 120, 110) == MenuEvent::None && ui.focus() == 0);
  ui.touch(true, 120, 60); ui.activate(MenuUi::kMainUtilities);
  assert(ui.touch(false, 120, 60) == MenuEvent::None && ui.page() == MenuPage::Utilities);
  ui.touch(true, 120, 60); ui.back();
  assert(ui.touch(false, 120, 60) == MenuEvent::None && ui.page() == MenuPage::Main);

  // Other menu pages retain their fixed rows, with the same release-only activation.
  openPage(ui, MenuPage::Utilities);
  assert(ui.touch(true, 120, 60) == MenuEvent::None);
  assert(ui.touch(true, 120, 170) == MenuEvent::None);
  assert(ui.touch(false, 120, 170) == MenuEvent::None && ui.scrollOffset() == 0);
  assert(ui.touch(true, 120, 60) == MenuEvent::None);
  assert(ui.touch(false, 120, 60) == MenuEvent::RemoteDisplay);
}

static void scrollingRenderClipsBehindFixedControls() {
  MenuUi ui;
  uint16_t reference[240 * 240], frame[240 * 240 + 2];
  frame[0] = frame[240 * 240 + 1] = 0xA55A;
  ui.open(); sloth::drawMenu(reference, ui);
  // Exercise partial text, icons, rows and outlines at every scroll position.
  for (int offset = 0; offset <= ui.maxScroll(); ++offset) {
    ui.open();
    ui.touch(true, 120, 180);
    if (offset) ui.touch(true, 120, 180 - offset - 8); // Latch drag even for offsets below the tap tolerance.
    ui.touch(true, 120, 180 - offset);
    assert(ui.scrollOffset() == offset && ui.focus() == 0);
    sloth::drawMenu(frame + 1, ui);
    assert(frame[0] == 0xA55A && frame[240 * 240 + 1] == 0xA55A);
    assert(memcmp(reference, frame + 1, 50 * 240 * sizeof(uint16_t)) == 0);
    assert(memcmp(reference + 187 * 240, frame + 1 + 187 * 240, 53 * 240 * sizeof(uint16_t)) == 0);
    if (offset) assert(memcmp(reference + 50 * 240, frame + 1 + 50 * 240, 137 * 240 * sizeof(uint16_t)) != 0);
    for (int y = 24; y < 50; ++y) assert(ui.hitTest(120, y) == -1);
    for (int y = 187; y < 205; ++y) assert(ui.hitTest(120, y) == -1);
    assert(ui.hitTest(120, 215) == MenuUi::kBack);
    ui.cancelTouch();
  }
}

static void doomScrollTouchAndHardware() {
  MenuUi ui;
  openPage(ui, MenuPage::Games);
  assert(ui.rowCount() == 5 && ui.targetCount() == 6 && ui.maxScroll() == 39);
  assert(ui.hitTest(120, 170) == MenuUi::kGamesForestFidget);
  assert(ui.hitTest(120, 186) == MenuUi::kGamesForestFidget);
  assert(ui.hitTest(120, 187) == -1); // The fixed footer masks the fifth row.

  uint16_t top[50 * 240], bottom[53 * 240], frame[240 * 240 + 2];
  frame[0] = frame[240 * 240 + 1] = 0xA55A;
  sloth::drawMenu(frame + 1, ui);
  memcpy(top, frame + 1, sizeof(top));
  memcpy(bottom, frame + 1 + 187 * 240, sizeof(bottom));

  // A swipe reveals Doom but never activates the row under the lifted finger.
  assert(ui.touch(true, 120, 160) == MenuEvent::None);
  assert(ui.touch(true, 120, 100) == MenuEvent::None);
  assert(ui.scrollOffset() == ui.maxScroll());
  assert(ui.touch(false, 120, 100) == MenuEvent::None);
  assert(ui.page() == MenuPage::Games && ui.focus() == 0);
  assert(ui.hitTest(120, 156) == MenuUi::kGamesDoom);
  assert(ui.hitTest(120, 185) == MenuUi::kGamesDoom);
  assert(ui.hitTest(120, 186) == -1 && ui.hitTest(120, 195) == -1);
  sloth::drawMenu(frame + 1, ui);
  assert(frame[0] == 0xA55A && frame[240 * 240 + 1] == 0xA55A);
  assert(memcmp(top, frame + 1, sizeof(top)) == 0);
  assert(memcmp(bottom, frame + 1 + 187 * 240, sizeof(bottom)) == 0);

  assert(ui.touch(true, 120, 170) == MenuEvent::None);
  assert(ui.page() == MenuPage::Games);
  assert(ui.touch(false, 120, 170) == MenuEvent::None);
  assert(ui.page() == MenuPage::Doom && ui.focus() == 0 && ui.rowCount() == 1);
  assert(ui.hitTest(120, 60) == 0);
  assert(ui.activate() == MenuEvent::PlayDoom); // The caller owns the handoff.
  assert(ui.page() == MenuPage::Doom);
  assert(ui.touch(true, 120, 215) == MenuEvent::None);
  assert(ui.touch(false, 120, 215) == MenuEvent::None);
  assert(ui.page() == MenuPage::Games && ui.focus() == MenuUi::kGamesDoom);
  assert(ui.scrollOffset() == ui.maxScroll() && ui.hitTest(120, 170) == MenuUi::kGamesDoom);

  // Hardware Next exposes hidden rows, Select enters Doom, and Back returns
  // with Doom still focused; another Next reaches the fixed Back footer.
  openPage(ui, MenuPage::Games);
  for (int target = 0; target < MenuUi::kGamesDoom; ++target) ui.next();
  assert(ui.focus() == MenuUi::kGamesDoom && ui.scrollOffset() == ui.maxScroll());
  assert(ui.activate() == MenuEvent::None && ui.page() == MenuPage::Doom);
  assert(ui.activate() == MenuEvent::PlayDoom);
  assert(ui.back() == MenuEvent::None && ui.page() == MenuPage::Games);
  assert(ui.focus() == MenuUi::kGamesDoom && ui.scrollOffset() == ui.maxScroll());
  ui.next();
  assert(ui.focus() == ui.rowCount() && ui.hitTest(120, 215) == MenuUi::kBack);
  assert(ui.activate() == MenuEvent::None && ui.page() == MenuPage::Main);
}

int main() {
  navigationAndExternalHandoffs(); independentPreferences(); navigationRailTouchActions(); touchHitTargetsAndRendering();
  mainScrollAndFocus(); mainTouchGestures(); scrollingRenderClipsBehindFixedControls(); doomScrollTouchAndHardware();
  puts("menu_ui: ordered main menu, five games including Doom, scrolling games and main menus, score/audio preferences, hardware Back cycle, release-only taps, drag cancellation, fixed chrome and render bounds passed");
}
