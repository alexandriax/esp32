#include "../firmware/sloth_pet/settings_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits>

using sloth::Animal;
using sloth::Scene;
using sloth::Settings;
using sloth::SettingsEvent;
using sloth::SettingsPage;
using sloth::SettingsUi;
using sloth::MotionView;
using sloth::DisplayStatus;

static SettingsEvent hardwareSelect(SettingsUi& ui, int target) {
  assert(ui.targetEnabled(target));
  for (unsigned presses = 0; ui.focus() != target && presses < 64; ++presses) ui.next();
  assert(ui.focus() == target);
  return ui.activate();
}

static void clearName(SettingsUi& ui) {
  while (ui.nameBuffer()[0]) ui.activate(SettingsUi::kNameDelete);
}

static void transactionAndPersistenceHandoff() {
  Settings live;
  SettingsUi ui;
  assert(!ui.isOpen() && ui.hitTest(30, 60) == -1 && ui.targetCount() == 0);
  ui.next();
  assert(ui.activate() == SettingsEvent::None);
  ui.open(live);
  hardwareSelect(ui, SettingsUi::kMainAnimal);
  assert(ui.page() == SettingsPage::Animal);
  assert(hardwareSelect(ui, 2) == SettingsEvent::Changed);
  assert(ui.draft().animal == Animal::Frog && live.animal == Animal::Sloth);
  assert(hardwareSelect(ui, SettingsUi::kMainSubtitle) == SettingsEvent::Changed);
  hardwareSelect(ui, SettingsUi::kMainScene);
  assert(hardwareSelect(ui, 1) == SettingsEvent::Changed);
  assert(ui.draft().scene == Scene::Meadow && !ui.draft().showSubtitle);
  assert(hardwareSelect(ui, SettingsUi::kMainSave) == SettingsEvent::SaveRequested);
  assert(ui.isOpen()); // A failed write leaves the complete draft retryable.
  char warning[] = "Could not save. Try again.";
  ui.setNotice(warning);
  warning[0] = 'X';
  assert(strcmp(ui.notice(), "Could not save. Try again.") == 0);
  assert(ui.draft().animal == Animal::Frog && live.animal == Animal::Sloth);
  assert(ui.activate() == SettingsEvent::SaveRequested);
  uint8_t record[sloth::kSettingsRecordSize];
  assert(sloth::encodeSettings(ui.draft(), record));
  assert(sloth::decodeSettings(record, sizeof(record), live));
  ui.close();
  assert(live.animal == Animal::Frog && live.scene == Scene::Meadow && !live.showSubtitle);
  ui.open(live);
  ui.activate(SettingsUi::kMainSubtitle);
  assert(ui.activate(SettingsUi::kMainCancel) == SettingsEvent::Cancelled);
  assert(ui.isOpen() && ui.draft().showSubtitle && !live.showSubtitle);
  ui.close(); // Caller discards, without saving/applying this draft.
  ui.open(live);
  assert(!ui.draft().showSubtitle);
}

static void conureSelectionAndPersistence() {
  Settings live;
  SettingsUi ui; ui.open(live);
  ui.activate(SettingsUi::kMainAnimal);
  assert(ui.choiceCount() == 4 && ui.targetCount() == 5);
  assert(ui.hitTest(60, 170) == 3);
  assert(ui.touch(true, 60, 170) == SettingsEvent::None);
  assert(ui.touch(false, 60, 170) == SettingsEvent::Changed);
  assert(ui.draft().animal == Animal::SunConure && live.animal == Animal::Sloth);
  uint8_t record[sloth::kSettingsRecordSize];
  assert(sloth::encodeSettings(ui.draft(), record));
  assert(record[4] == 3 && record[5] == 3);
  assert(sloth::decodeSettings(record, sizeof(record), live));
  ui.open(live); ui.activate(SettingsUi::kMainAnimal);
  assert(ui.focus() == 3 && ui.maxScroll() == 0);
  uint16_t guarded[240*240+2];
  guarded[0] = guarded[240*240+1] = 0xABCD;
  sloth::drawSettings(guarded+1, ui);
  assert(guarded[0] == 0xABCD && guarded[240*240+1] == 0xABCD);
  assert(hardwareSelect(ui, 0) == SettingsEvent::Changed);
  ui.activate(SettingsUi::kMainAnimal);
  assert(hardwareSelect(ui, 3) == SettingsEvent::Changed);
  assert(ui.draft().animal == Animal::SunConure);
}

static void namesKeyboardValidationAndCancel() {
  Settings live;
  SettingsUi ui;
  ui.open(live);
  ui.activate(SettingsUi::kMainName);
  assert(ui.page() == SettingsPage::Name && ui.focus() == 0);
  clearName(ui);
  assert(!ui.nameCanSave() && ui.hitTest(180, 220) == -1);
  assert(ui.activate(SettingsUi::kNameSave) == SettingsEvent::None);
  assert(ui.page() == SettingsPage::Name);
  ui.activate(SettingsUi::kNameSpace);
  ui.activate(SettingsUi::kNameSpace);
  assert(!ui.nameCanSave());
  clearName(ui);
  ui.activate(0); // A
  ui.activate(SettingsUi::kNameToggle);
  assert(ui.keyboardNumeric() && ui.keyCharacter(0) == '1');
  assert(ui.keyCharacter(10) == '-' && ui.keyCharacter(11) == '\'');
  assert(!ui.targetEnabled(12));
  ui.activate(0); ui.activate(10); ui.activate(11);
  ui.activate(SettingsUi::kNameToggle);
  ui.activate(1); ui.activate(SettingsUi::kNameSpace); ui.activate(2);
  assert(strcmp(ui.nameBuffer(), "A1-'B C") == 0);
  assert(strcmp(ui.draft().name, "Moss") == 0); // Name editor has its own draft.
  assert(ui.activate(SettingsUi::kNavBack) == SettingsEvent::None);
  assert(ui.page() == SettingsPage::Main && strcmp(ui.draft().name, "Moss") == 0);

  ui.activate(SettingsUi::kMainName);
  clearName(ui);
  ui.activate(SettingsUi::kNameSpace); ui.activate(0); ui.activate(1);
  ui.activate(SettingsUi::kNameSpace);
  assert(ui.activate(SettingsUi::kNameSave) == SettingsEvent::Changed);
  assert(strcmp(ui.draft().name, "AB") == 0); // Outer spaces normalize on Use Name.

  ui.activate(SettingsUi::kMainName);
  clearName(ui);
  for (int i = 0; i < 12; ++i) ui.activate(i);
  assert(strcmp(ui.nameBuffer(), "ABCDEFGHIJKL") == 0);
  assert(!ui.targetEnabled(12) && !ui.targetEnabled(SettingsUi::kNameSpace));
  ui.activate(12); ui.activate(SettingsUi::kNameSpace);
  assert(strcmp(ui.nameBuffer(), "ABCDEFGHIJKL") == 0);
  assert(hardwareSelect(ui, SettingsUi::kNameSave) == SettingsEvent::Changed);
  assert(strcmp(ui.draft().name, "ABCDEFGHIJKL") == 0);
}

static void timezonePagingAndBack() {
  Settings settings;
  SettingsUi ui;
  for (uint8_t zone = 0; zone < sloth::zoneCount(); ++zone) {
    settings.timeZone = zone;
    ui.open(settings);
    ui.activate(SettingsUi::kMainTimeZone);
    assert(ui.focus() == zone && ui.targetCount() == sloth::zoneCount() + 1);
    bool visible = false;
    for (int y = 50; y < 198; ++y) visible |= ui.hitTest(50, y) == zone;
    assert(visible); // Every persisted selection is revealed on entry.
    assert(ui.activate() == SettingsEvent::None);
    assert(ui.page() == SettingsPage::Main && ui.draft().timeZone == zone);
  }
  ui.open(Settings()); ui.activate(SettingsUi::kMainTimeZone);
  assert(hardwareSelect(ui, 13) == SettingsEvent::Changed);
  assert(ui.draft().timeZone == 13);
  ui.activate(SettingsUi::kMainTimeZone);
  hardwareSelect(ui, ui.choiceCount());
  assert(ui.page() == SettingsPage::Main && ui.draft().timeZone == 13);
}

static void scenePagingAndPersistence() {
  Settings settings;
  SettingsUi ui;
  for (unsigned scene = 0; scene < static_cast<unsigned>(Scene::Count); ++scene) {
    settings.scene = static_cast<Scene>(scene);
    ui.open(settings);
    ui.activate(SettingsUi::kMainScene);
    assert(ui.focus() == static_cast<int>(scene));
    assert(ui.activate() == SettingsEvent::None);
    assert(ui.page() == SettingsPage::Main && ui.draft().scene == settings.scene);
  }
  ui.open(Settings()); ui.activate(SettingsUi::kMainScene);
  assert(hardwareSelect(ui, 6) == SettingsEvent::Changed);
  assert(ui.draft().scene == Scene::UnderSea);
  ui.activate(SettingsUi::kMainScene);
  hardwareSelect(ui, ui.choiceCount());
  uint8_t record[sloth::kSettingsRecordSize];
  assert(sloth::encodeSettings(ui.draft(), record));
  assert(sloth::decodeSettings(record, sizeof(record), settings));
  assert(settings.scene == Scene::UnderSea);
}

static void motionControlsRemainTransactional() {
  Settings live;
  SettingsUi ui;
  ui.open(live);
  assert(ui.targetCount() == 10);
  hardwareSelect(ui, SettingsUi::kMainShake);
  assert(ui.page() == SettingsPage::Motion && ui.focus() == SettingsUi::kMotionBack);
  assert(ui.targetCount() == 3 && ui.draft().shakeSensitivity == 2);
  assert(ui.hitTest(12, 205) == SettingsUi::kMotionLess);
  assert(ui.hitTest(79, 227) == SettingsUi::kMotionLess);
  assert(ui.hitTest(80, 215) == -1 && ui.hitTest(85, 215) == -1);
  assert(ui.hitTest(86, 205) == SettingsUi::kMotionBack);
  assert(ui.hitTest(154, 215) == -1 && ui.hitTest(159, 215) == -1);
  assert(ui.hitTest(160, 205) == SettingsUi::kMotionMore);
  assert(ui.hitTest(227, 227) == SettingsUi::kMotionMore);
  assert(ui.hitTest(228, 215) == -1 && ui.hitTest(190, 228) == -1);
  assert(ui.hitTest(30, 170) == -1); // Telemetry is not a setting or action.
  assert(hardwareSelect(ui, SettingsUi::kMotionLess) == SettingsEvent::Changed);
  assert(hardwareSelect(ui, SettingsUi::kMotionLess) == SettingsEvent::Changed);
  assert(ui.draft().shakeSensitivity == 0 && live.shakeSensitivity == 2);
  assert(!ui.targetEnabled(SettingsUi::kMotionLess) && ui.hitTest(30, 215) == -1);
  assert(ui.activate(SettingsUi::kMotionLess) == SettingsEvent::None);
  ui.next();
  assert(ui.focus() == SettingsUi::kMotionBack);
  for (unsigned level = 1; level < sloth::kShakeSensitivityCount; ++level) {
    assert(hardwareSelect(ui, SettingsUi::kMotionMore) == SettingsEvent::Changed);
    assert(ui.draft().shakeSensitivity == level);
  }
  assert(!ui.targetEnabled(SettingsUi::kMotionMore) && ui.hitTest(190, 215) == -1);
  assert(ui.activate(SettingsUi::kMotionMore) == SettingsEvent::None);
  ui.next();
  assert(ui.focus() == SettingsUi::kMotionLess); // Hardware skips the disabled endpoint.
  assert(hardwareSelect(ui, SettingsUi::kMotionBack) == SettingsEvent::None);
  assert(ui.page() == SettingsPage::Main && ui.focus() == SettingsUi::kMainShake);
  assert(ui.draft().shakeSensitivity == 4 && live.shakeSensitivity == 2);
  assert(hardwareSelect(ui, SettingsUi::kMainCancel) == SettingsEvent::Cancelled);
  ui.close(); ui.open(live);
  assert(ui.draft().shakeSensitivity == 2); // Global Cancel discards test changes.
  ui.activate(SettingsUi::kMainShake);
  ui.activate(SettingsUi::kMotionMore);
  ui.activate(SettingsUi::kMotionBack);
  assert(hardwareSelect(ui, SettingsUi::kMainSave) == SettingsEvent::SaveRequested);
  uint8_t record[sloth::kSettingsRecordSize];
  assert(sloth::encodeSettings(ui.draft(), record));
  assert(sloth::decodeSettings(record, sizeof(record), live));
  assert(live.shakeSensitivity == 3);
}

static bool regionEqual(const uint16_t* first, const uint16_t* second, int y0, int y1) {
  return memcmp(first + y0 * 240, second + y0 * 240,
                static_cast<size_t>(y1 - y0) * 240 * sizeof(uint16_t)) == 0;
}

static void powerBackPreservesDraftAndDiscardsUncommittedName() {
  Settings live;
  SettingsUi ui;
  assert(ui.back() == SettingsEvent::None);
  ui.open(live);
  ui.activate(SettingsUi::kMainSubtitle);
  ui.activate(SettingsUi::kMainAnimal);
  ui.activate(2);
  const int entries[] = {SettingsUi::kMainAnimal, SettingsUi::kMainTimeZone,
                         SettingsUi::kMainScene, SettingsUi::kMainShake, SettingsUi::kMainStorage, SettingsUi::kMainRotation};
  uint8_t before[sloth::kSettingsRecordSize], after[sloth::kSettingsRecordSize];
  assert(sloth::encodeSettings(ui.draft(), before));
  for (int entry : entries) {
    ui.activate(entry);
    assert(ui.page() != SettingsPage::Main);
    ui.touch(true, 100, 80); // Back cancels a pending finger press too.
    assert(ui.back() == SettingsEvent::None);
    assert(ui.page() == SettingsPage::Main && ui.focus() == entry);
    assert(ui.touch(false, 100, 80) == SettingsEvent::None);
    assert(ui.page() == SettingsPage::Main);
    assert(sloth::encodeSettings(ui.draft(), after));
    assert(memcmp(before, after, sizeof(before)) == 0);
  }
  ui.activate(SettingsUi::kMainName);
  clearName(ui); ui.activate(0);
  assert(strcmp(ui.nameBuffer(), "A") == 0);
  assert(ui.back() == SettingsEvent::None);
  assert(ui.page() == SettingsPage::Main && ui.focus() == SettingsUi::kMainName);
  assert(ui.nameBuffer()[0] == '\0' && strcmp(ui.draft().name, "Moss") == 0);
  ui.activate(SettingsUi::kMainName);
  assert(strcmp(ui.nameBuffer(), "Moss") == 0);
  ui.back();
  assert(ui.back() == SettingsEvent::Cancelled);
  assert(ui.isOpen()); // Caller closes after deciding not to apply the draft.
  assert(!ui.draft().showSubtitle && ui.draft().animal == Animal::Frog);
  assert(live.showSubtitle && live.animal == Animal::Sloth);
  ui.close(); ui.open(live);
  assert(ui.draft().showSubtitle && ui.draft().animal == Animal::Sloth);
}

static void displayStatusRenderBoundsAndStates() {
  uint16_t waiting[240 * 240], other[240 * 240];
  uint16_t guarded[240 * 240 + 2];
  guarded[0] = guarded[240 * 240 + 1] = 0x1357;
  sloth::drawDisplayStatus(waiting);
  const DisplayStatus states[] = {DisplayStatus::Waiting, DisplayStatus::PermissionNeeded,
                                  DisplayStatus::Disconnected};
  for (DisplayStatus status : states) {
    sloth::drawDisplayStatus(guarded + 1, status);
    assert(guarded[0] == 0x1357 && guarded[240 * 240 + 1] == 0x1357);
    assert(regionEqual(waiting, guarded + 1, 0, 150)); // Stable screen title/icon.
    assert(regionEqual(waiting, guarded + 1, 205, 240)); // Exit guidance always present.
    if (status != DisplayStatus::Waiting)
      assert(!regionEqual(waiting, guarded + 1, 150, 205));
  }
  sloth::drawDisplayStatus(other, DisplayStatus::PermissionNeeded);
  assert(!regionEqual(other, guarded + 1, 150, 205)); // Permission and disconnect differ.
  sloth::drawDisplayStatus(other, DisplayStatus::Waiting, true);
  assert(!regionEqual(waiting, other, 0, 150));
  assert(regionEqual(waiting, other, 205, 240));
  for (DisplayStatus status : states) {
    sloth::drawDisplayStatus(guarded + 1, status, true);
    assert(guarded[0] == 0x1357 && guarded[240 * 240 + 1] == 0x1357);
    assert(regionEqual(other, guarded + 1, 0, 150));
    assert(regionEqual(other, guarded + 1, 205, 240));
    if (status != DisplayStatus::Waiting)
      assert(!regionEqual(other, guarded + 1, 150, 205));
  }
  sloth::drawDisplayStatus(NULL, DisplayStatus::Waiting);
}

static void motionDiagnosticsFreshnessAndBounds() {
  SettingsUi ui;
  ui.open(Settings()); ui.activate(SettingsUi::kMainShake);
  MotionView view;
  view.available = view.ready = true;
  view.x = 0.18f; view.y = -0.12f; view.z = 0.98f;
  view.magnitude = 1.01f; view.motion = 0.24f; view.peak = 1.48f;
  view.tiltX = 12; view.tiltY = -8;
  view.samples = 1234; view.triggers = 2; view.peaks = 1; view.sampleHz = 48;
  uint16_t fresh[240 * 240], stale[240 * 240], changed[240 * 240];
  uint16_t guarded[240 * 240 + 2];
  guarded[0] = guarded[240 * 240 + 1] = 0x5AA5;
  sloth::drawSettings(fresh, ui, view);
  view.ready = false;
  sloth::drawSettings(stale, ui, view);
  assert(!regionEqual(fresh, stale, 49, 150)); // Old live numbers/tilt disappear.
  view.x = -3.4f; view.y = 2.7f; view.z = 1.9f;
  view.magnitude = 4.6f; view.motion = 3.2f; view.peak = 3.9f;
  view.tiltX = -40; view.tiltY = 40; view.peaks = 3; view.sampleHz = 20;
  view.triggered = true;
  sloth::drawSettings(changed, ui, view);
  assert(regionEqual(stale, changed, 0, 240)); // Stale values cannot look live or flash.
  view.available = false;
  sloth::drawSettings(changed, ui, view);
  assert(regionEqual(stale, changed, 49, 150));
  assert(!regionEqual(stale, changed, 150, 170)); // Unavailable differs from stale.
  view.available = view.ready = true;
  view.triggered = false;
  sloth::drawSettings(fresh, ui, view);
  view.triggered = true;
  sloth::drawSettings(changed, ui, view);
  assert(regionEqual(fresh, changed, 49, 150));
  assert(!regionEqual(fresh, changed, 150, 170)); // Trigger feedback is visibly distinct.
  view.x = std::numeric_limits<float>::quiet_NaN();
  sloth::drawSettings(changed, ui, view);
  assert(regionEqual(stale, changed, 0, 240)); // Invalid sensor payload is never live.

  view.x = view.y = view.z = std::numeric_limits<float>::max();
  view.magnitude = view.motion = view.peak = std::numeric_limits<float>::max();
  view.tiltX = std::numeric_limits<float>::max();
  view.tiltY = -std::numeric_limits<float>::max();
  view.sampleHz = std::numeric_limits<float>::max();
  view.samples = view.triggers = UINT32_MAX;
  view.peaks = std::numeric_limits<unsigned>::max();
  view.threshold = std::numeric_limits<float>::infinity();
  sloth::drawSettings(guarded + 1, ui, view);
  assert(guarded[0] == 0x5AA5 && guarded[240 * 240 + 1] == 0x5AA5);
  assert(ui.draft().shakeSensitivity == 2 && ui.page() == SettingsPage::Motion);
  assert(ui.focus() == SettingsUi::kMotionBack); // Rendering never edits or moves focus.
}

static void hitTargetsAndRenderBounds() {
  SettingsUi ui;
  ui.open(Settings());
  assert(ui.hitTest(12, 50) == SettingsUi::kMainAnimal && ui.hitTest(221, 89) == SettingsUi::kMainAnimal);
  assert(ui.hitTest(222, 60) == -1 && ui.hitTest(30, 90) == -1);
  assert(ui.hitTest(30, 94) == SettingsUi::kMainName && ui.hitTest(-1, 94) == -1);
  assert(ui.hitTest(20, 208) == SettingsUi::kMainCancel);
  assert(ui.hitTest(128, 208) == SettingsUi::kMainSave);
  assert(ui.hitTest(120, 220) == -1 && ui.hitTest(30, 240) == -1);
  assert(ui.activate(999) == SettingsEvent::None);
  assert(ui.activate(-2) == SettingsEvent::None && ui.page() == SettingsPage::Main);
  uint16_t guarded[240 * 240 + 2];
  guarded[0] = guarded[240 * 240 + 1] = 0xA55A;
  const int pages[] = {-1, SettingsUi::kMainAnimal, SettingsUi::kMainName,
                       SettingsUi::kMainTimeZone, SettingsUi::kMainScene, SettingsUi::kMainShake, SettingsUi::kMainStorage, SettingsUi::kMainRotation};
  for (int page : pages) {
    ui.open(Settings());
    if (page >= 0) ui.activate(page);
    for (int offset = 0; offset < 4; ++offset) {
      ui.touch(true, 120, 180); ui.touch(true, 120, 100); ui.touch(false, 120, 100);
      sloth::drawSettings(guarded + 1, ui);
      assert(guarded[0] == 0xA55A && guarded[240 * 240 + 1] == 0xA55A);
      for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 240; ++x) {
          const int target = ui.hitTest(x, y);
          assert(target == -1 || ui.targetEnabled(target));
        }
    }
  }
  ui.open(Settings()); ui.activate(SettingsUi::kMainName);
  assert(ui.hitTest(8, 54) == 0 && ui.hitTest(41, 81) == 0);
  assert(ui.hitTest(42, 65) == -1 && ui.hitTest(12, 82) == -1);
  assert(ui.hitTest(120, 218) == SettingsUi::kNameToggle);
  ui.activate(SettingsUi::kNameToggle);
  sloth::drawSettings(guarded + 1, ui);
  assert(guarded[0] == 0xA55A && guarded[240 * 240 + 1] == 0xA55A);
  sloth::drawSettings(NULL, ui);
  ui.close();
  assert(ui.hitTest(30, 220) == -1 && ui.activate(0) == SettingsEvent::None);
}

static void gesturesAndHardwareReveal() {
  SettingsUi ui;
  ui.open(Settings());
  assert(ui.touch(true, 30, 60) == SettingsEvent::None);
  assert(ui.page() == SettingsPage::Main); // Never select on initial contact.
  ui.touch(true, 33, 63);
  ui.touch(false, 33, 63);
  assert(ui.page() == SettingsPage::Animal); // Small finger jitter still taps.
  ui.activate(ui.choiceCount());
  ui.touch(true, 100, 180); ui.touch(true, 100, -100); ui.touch(false, 100, -100);
  assert(ui.page() == SettingsPage::Main && ui.scrollOffset() == ui.maxScroll());
  const Settings original = ui.draft();
  ui.touch(true, 100, 180); ui.touch(true, 100, -200); ui.touch(false, 100, -200);
  assert(ui.scrollOffset() == ui.maxScroll());
  assert(ui.hitTest(100, 92) == SettingsUi::kMainShake);
  assert(ui.touch(true, 100, 92) == SettingsEvent::None);
  assert(ui.touch(false, 100, 92) == SettingsEvent::None);
  assert(ui.isOpen() && ui.page() == SettingsPage::Motion);
  assert(ui.back() == SettingsEvent::None);
  assert(ui.page() == SettingsPage::Main && ui.focus() == SettingsUi::kMainShake);
  assert(memcmp(&original, &ui.draft(), sizeof(Settings)) == 0);
  ui.touch(true, 100, 60); ui.touch(true, 100, 400); ui.touch(false, 100, 400);
  assert(ui.scrollOffset() == 0);
  // A horizontal swipe must not toggle or select anything.
  ui.touch(true, 30, 60); ui.touch(true, 120, 60); ui.touch(false, 120, 60);
  assert(ui.page() == SettingsPage::Main && ui.scrollOffset() == 0);
  // Starting on fixed controls does not scroll or activate after a drag.
  ui.touch(true, 180, 220); ui.touch(true, 180, 60);
  assert(ui.touch(false, 180, 60) == SettingsEvent::None && ui.scrollOffset() == 0);
  ui.touch(true, 30, 60); ui.cancelTouch(); ui.touch(false, 30, 60);
  assert(ui.page() == SettingsPage::Main);
  // Every hardware-focused row is visible, including after wrapping.
  for (int i = 0; i < 2 * ui.targetCount(); ++i) {
    ui.next();
    bool visible = false;
    for (int y = 0; y < 240; ++y) visible |= ui.hitTest(30, y) == ui.focus() || ui.hitTest(180, y) == ui.focus();
    assert(visible);
  }
  ui.activate(SettingsUi::kMainName);
  char before[13]; strcpy(before, ui.nameBuffer());
  ui.touch(true, 30, 150); ui.touch(true, 30, -1000); ui.touch(false, 30, -1000);
  assert(ui.scrollOffset() == ui.maxScroll() && strcmp(before, ui.nameBuffer()) == 0);
  assert(ui.hitTest(100, 188) == SettingsUi::kNameSpace);
  assert(ui.hitTest(44, 218) == SettingsUi::kNameDelete);
  ui.touch(true, 44, 218); ui.touch(false, 44, 218);
  assert(strlen(ui.nameBuffer()) + 1 == strlen(before));
  ui.activate(SettingsUi::kNameToggle);
  assert(ui.keyboardNumeric() && ui.scrollOffset() == 0);
  hardwareSelect(ui, SettingsUi::kNameDelete); // Backspace stays reachable in both layouts.
  assert(ui.scrollOffset() == ui.maxScroll());
  ui.close(); ui.touch(true, 30, 60); ui.touch(false, 30, 60);
  assert(!ui.isOpen());
}

static void navigationRailTouchAndDrafts() {
  SettingsUi ui; ui.open(Settings());
  assert(ui.hitTest(40,15)==SettingsUi::kNavNext);
  assert(ui.hitTest(120,15)==SettingsUi::kNavBack);
  assert(ui.hitTest(190,15)==SettingsUi::kNavSelect);
  assert(ui.hitTest(19,15)==-1 && ui.hitTest(220,15)==-1 && ui.hitTest(40,24)==-1);
  ui.touch(true,40,15);assert(ui.touch(false,40,15)==SettingsEvent::Changed);
  assert(ui.focus()==SettingsUi::kMainName);
  ui.touch(true,190,15);ui.touch(false,190,15);
  assert(ui.page()==SettingsPage::Name && ui.focus()==0);
  clearName(ui);
  for(unsigned i=0;ui.focus()!=0 && i<40;++i)ui.next();
  ui.activate(SettingsUi::kNavNext);
  assert(ui.focus()==1);
  ui.activate(SettingsUi::kNavSelect);assert(strcmp(ui.nameBuffer(),"B")==0);
  ui.touch(true,120,15);ui.touch(true,120,90);ui.touch(false,120,15);
  assert(ui.page()==SettingsPage::Name && strcmp(ui.nameBuffer(),"B")==0);
  ui.touch(true,120,15);ui.touch(false,120,15);
  assert(ui.page()==SettingsPage::Main && strcmp(ui.draft().name,"Moss")==0);
  assert(ui.activate(SettingsUi::kNavBack)==SettingsEvent::Cancelled);
  ui.close();assert(!ui.targetEnabled(SettingsUi::kNavNext) && ui.hitTest(40,15)==-1);
  assert(ui.activate(SettingsUi::kNavNext)==SettingsEvent::None);
  ui.open(Settings());ui.activate(SettingsUi::kMainShake);
  uint16_t pixels[240*240];sloth::drawSettings(pixels,ui);
  for(int i=233*240;i<240*240;++i)assert(pixels[i]==pixels[0]); // No clipped bottom legend.
}

static void storageNavigationAndReadOnlySnapshots() {
  Settings live;
  SettingsUi ui; ui.open(live);
  ui.activate(SettingsUi::kMainSubtitle);
  uint8_t before[sloth::kSettingsRecordSize], after[sloth::kSettingsRecordSize];
  assert(sloth::encodeSettings(ui.draft(), before));
  assert(hardwareSelect(ui, SettingsUi::kMainStorage) == SettingsEvent::StorageRefresh);
  assert(ui.page() == SettingsPage::Storage && ui.focus() == SettingsUi::kStorageBack);
  assert(ui.targetCount() == 2 && ui.maxScroll() == 0);
  assert(ui.hitTest(24,204) == SettingsUi::kStorageRefresh);
  assert(ui.hitTest(117,227) == SettingsUi::kStorageRefresh);
  assert(ui.hitTest(118,215) == -1 && ui.hitTest(123,215) == -1);
  assert(ui.hitTest(124,204) == SettingsUi::kStorageBack);
  assert(ui.hitTest(215,227) == SettingsUi::kStorageBack);
  assert(ui.hitTest(216,215) == -1 && ui.hitTest(30,228) == -1);
  assert(ui.hitTest(100,150) == -1); // Status cards are never buttons.
  assert(ui.touch(true,60,214) == SettingsEvent::None);
  assert(ui.touch(false,62,215) == SettingsEvent::StorageRefresh);
  assert(ui.touch(false,62,215) == SettingsEvent::None); // One refresh per tap.
  ui.touch(true,60,214); ui.touch(true,60,180);
  assert(ui.touch(false,60,214) == SettingsEvent::None);
  ui.touch(true,100,180); ui.touch(true,100,50); ui.touch(false,100,50);
  assert(ui.scrollOffset() == 0 && ui.page() == SettingsPage::Storage);
  assert(hardwareSelect(ui, SettingsUi::kStorageRefresh) == SettingsEvent::StorageRefresh);
  ui.next(); assert(ui.focus() == SettingsUi::kStorageBack);
  assert(ui.activate() == SettingsEvent::None);
  assert(ui.page() == SettingsPage::Main && ui.focus() == SettingsUi::kMainStorage);
  assert(ui.hitTest(60,180) == SettingsUi::kMainStorage); // Returned row is revealed.
  assert(ui.touch(true,60,180) == SettingsEvent::None);
  assert(ui.touch(false,60,180) == SettingsEvent::StorageRefresh);
  ui.touch(true,170,214); assert(ui.touch(false,170,214) == SettingsEvent::None);
  assert(ui.page() == SettingsPage::Main && ui.focus() == SettingsUi::kMainStorage);
  ui.activate(SettingsUi::kMainStorage); ui.back(); // Physical power uses back().
  assert(ui.page() == SettingsPage::Main && ui.focus() == SettingsUi::kMainStorage);
  assert(sloth::encodeSettings(ui.draft(), after));
  assert(memcmp(before,after,sizeof(before)) == 0 && live.showSubtitle);

  ui.activate(SettingsUi::kMainStorage);
  storage_status::Snapshot view;
  view.flashValid = view.firmwareValid = view.nvsValid = true;
  view.flashBytes = 16u*1024*1024; view.firmwareBytes = 1600000; view.firmwareCapacityBytes = 3u*1024*1024;
  view.nvsUsedEntries = 90; view.nvsTotalEntries = 600;
  view.cardSpaceKnown = true;
  view.cardTotalBytes = uint64_t(32)*1024*1024*1024;
  view.cardUsedBytes = uint64_t(7)*1024*1024*1024; view.cardFreeBytes = view.cardTotalBytes-view.cardUsedBytes;
  uint16_t prior[240*240], guarded[240*240+2];
  guarded[0] = guarded[240*240+1] = 0xA55A;
  using storage_status::CardState;
  const CardState states[] = {CardState::Unchecked,CardState::Checking,CardState::Ready,
      CardState::NoCard,CardState::Unavailable,CardState::Error};
  sloth::drawSettings(prior,ui,MotionView(),view);
  for (CardState state : states) {
    view.cardState = state;
    sloth::drawSettings(guarded+1,ui,MotionView(),view);
    assert(guarded[0] == 0xA55A && guarded[240*240+1] == 0xA55A);
    assert(regionEqual(prior,guarded+1,0,129));
    assert(regionEqual(prior,guarded+1,198,240));
    if (state != CardState::Unchecked) assert(!regionEqual(prior,guarded+1,129,198));
    for (int y=233;y<240;++y) for(int x=0;x<240;++x) assert(guarded[1+y*240+x] == guarded[1]);
  }
  view.cardState=CardState::Ready;
  view.flashBytes=view.firmwareBytes=view.firmwareCapacityBytes=UINT64_MAX;
  view.nvsUsedEntries=view.nvsTotalEntries=UINT64_MAX;
  view.cardTotalBytes=view.cardUsedBytes=UINT64_MAX;view.cardFreeBytes=0;
  sloth::drawSettings(guarded+1,ui,MotionView(),view); // Compact upper-bound units and checked NVS multiplication.
  assert(guarded[0] == 0xA55A && guarded[240*240+1] == 0xA55A);
  view.cardFreeBytes=UINT64_MAX;
  sloth::drawSettings(prior,ui,MotionView(),view); // Inconsistent card data cannot appear ready.
  assert(!regionEqual(prior,guarded+1,129,198));
  view.cardState=CardState::Error;
  view.cardCapacityBytes=uint64_t(32)*1024*1024*1024;
  sloth::drawSettings(guarded+1,ui,MotionView(),view);
  assert(!regionEqual(prior,guarded+1,129,198)); // Known capacity survives unreadable filesystem.
  assert(regionEqual(prior,guarded+1,198,240)); // Error details leave buttons intact.
  assert(ui.page()==SettingsPage::Storage && ui.focus()==SettingsUi::kStorageBack);
  assert(sloth::encodeSettings(ui.draft(), after) && memcmp(before,after,sizeof(before))==0);
}

static void rotationIsTransactionalAndTouchable() {
  Settings live;SettingsUi ui;
  for(unsigned turn=0;turn<4;++turn) {
    live.screenRotation=(turn+1)%4;ui.open(live);hardwareSelect(ui,SettingsUi::kMainRotation);
    assert(ui.page()==SettingsPage::Rotation && ui.choiceCount()==4 && ui.maxScroll()==0);
    assert(ui.focus()==live.screenRotation);
    const int y=50+turn*36+16;assert(ui.hitTest(100,y)==int(turn));
    ui.touch(true,100,y);assert(ui.touch(false,100,y)==SettingsEvent::Changed);
    assert(ui.draft().screenRotation==turn && live.screenRotation==(turn+1)%4);
    assert(ui.page()==SettingsPage::Main && ui.focus()==SettingsUi::kMainRotation);
    assert(hardwareSelect(ui,SettingsUi::kMainSave)==SettingsEvent::SaveRequested);
    uint8_t record[sloth::kSettingsRecordSize];assert(sloth::encodeSettings(ui.draft(),record));
    assert(sloth::decodeSettings(record,sizeof(record),live) && live.screenRotation==turn);
  }
}

int main() {
  rotationIsTransactionalAndTouchable();
  conureSelectionAndPersistence();
  storageNavigationAndReadOnlySnapshots();
  transactionAndPersistenceHandoff();
  namesKeyboardValidationAndCancel();
  timezonePagingAndBack();
  scenePagingAndPersistence();
  motionControlsRemainTransactional();
  motionDiagnosticsFreshnessAndBounds();
  powerBackPreservesDraftAndDiscardsUncommittedName();
  displayStatusRenderBoundsAndStates();
  hitTargetsAndRenderBounds();
  gesturesAndHardwareReveal();
  navigationRailTouchAndDrafts();
  puts("settings_ui: transactions, scrolling keyboard, swipe cancellation, focus reveal, motion diagnostics, nested PWR back, hardware fallback and render bounds passed");
}
