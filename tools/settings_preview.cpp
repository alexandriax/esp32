#include "../firmware/sloth_pet/settings_ui.h"
#include "../firmware/sloth_pet/menu_ui.h"
#include "../firmware/sloth_pet/volume_overlay.h"
#include "../firmware/sloth_pet/ui_icons.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: settings_preview output.ppm [menu|menu-bottom|menu-back|games|games-fidget|games-back|pong|pong-settings|tetris|tetris-settings|leaf-sweep|leaf-sweep-settings|icons|icons-selected|main|main-bottom|storage|storage-checking|storage-empty|storage-unavailable|storage-error|animal|name|name-last|numbers|numbers-last|zone|zone-last|scene|scene-last|undersea|notice|maxname|motion|motion-triggered|motion-offline|motion-stale|display|display-waiting|display-permission|display-lost|wifi-display|wifi-lost|volume-min|volume-mid|volume-max]\n");
    return 1;
  }
  const char* page = argc > 2 ? argv[2] : "main";
  sloth::Settings settings;
  sloth::SettingsUi ui;
  sloth::MotionView motion;
  storage_status::Snapshot storage;
  if (strcmp(page, "maxname") == 0 && !sloth::setName(settings, "SLEEPY MOSS1")) return 4;
  ui.open(settings);
  if (strcmp(page, "animal") == 0) ui.activate(sloth::SettingsUi::kMainAnimal);
  if (strcmp(page, "scene") == 0 || strcmp(page, "scene-last") == 0 || strcmp(page, "undersea") == 0) {
    ui.activate(sloth::SettingsUi::kMainScene);
    if (strcmp(page, "scene") != 0) {
      ui.touch(true, 100, 180); ui.touch(true, 100, -200); ui.touch(false, 100, -200);
    }
    if (strcmp(page, "undersea") == 0) ui.activate(6);
  }
  if (strcmp(page, "zone") == 0 || strcmp(page, "zone-last") == 0) {
    ui.activate(sloth::SettingsUi::kMainTimeZone);
    if (strcmp(page, "zone-last") == 0) {
      ui.touch(true, 100, 180); ui.touch(true, 100, -600); ui.touch(false, 100, -600);
    }
  }
  if (strcmp(page, "name") == 0 || strcmp(page, "numbers") == 0 || strcmp(page, "maxname") == 0 || strcmp(page, "name-last") == 0 || strcmp(page, "numbers-last") == 0) {
    ui.activate(sloth::SettingsUi::kMainName);
    if (strcmp(page, "numbers") == 0 || strcmp(page, "numbers-last") == 0) ui.activate(sloth::SettingsUi::kNameToggle);
  }
  if (strcmp(page, "main-bottom") == 0 || strcmp(page, "name-last") == 0 || strcmp(page, "numbers-last") == 0) {
    ui.touch(true, 100, 180); ui.touch(true, 100, -600); ui.touch(false, 100, -600);
  }
  if (strcmp(page, "notice") == 0) ui.setNotice("Couldn't save. Try again.");
  if (strcmp(page, "motion") == 0 || strcmp(page, "motion-triggered") == 0 ||
      strcmp(page, "motion-offline") == 0 || strcmp(page, "motion-stale") == 0) {
    ui.activate(sloth::SettingsUi::kMainShake);
    motion.available = strcmp(page, "motion-offline") != 0;
    motion.ready = motion.available && strcmp(page, "motion-stale") != 0;
    motion.x = 0.18f; motion.y = -0.12f; motion.z = 0.98f;
    motion.magnitude = 1.01f; motion.motion = 0.24f; motion.peak = 1.48f;
    motion.tiltX = 12; motion.tiltY = -8;
    motion.sampleHz = 48; motion.samples = 1234; motion.triggers = 2; motion.peaks = 1;
    if (strcmp(page, "motion-triggered") == 0) {
      motion.triggered = motion.cooldown = true;
      motion.triggers = 3; motion.peaks = 3;
      motion.motion = 1.36f; motion.peak = 1.75f;
    }
  }
  if (strcmp(page, "rotation") == 0) ui.activate(sloth::SettingsUi::kMainRotation);
  if (strncmp(page, "storage", 7) == 0) {
    ui.activate(sloth::SettingsUi::kMainStorage);
    storage.flashValid = storage.firmwareValid = storage.nvsValid = true;
    storage.flashBytes = 16u * 1024 * 1024;
    storage.firmwareBytes = 1623489; storage.firmwareCapacityBytes = 3u * 1024 * 1024;
    storage.nvsUsedEntries = 94; storage.nvsTotalEntries = 762;
    storage.cardState = storage_status::CardState::Ready;
    storage.cardSpaceKnown = true;
    storage.cardTotalBytes = uint64_t(32) * 1024 * 1024 * 1024;
    storage.cardUsedBytes = uint64_t(7) * 1024 * 1024 * 1024;
    storage.cardFreeBytes = storage.cardTotalBytes - storage.cardUsedBytes;
    if (strcmp(page,"storage-checking") == 0) storage.cardState = storage_status::CardState::Checking;
    if (strcmp(page,"storage-empty") == 0) storage.cardState = storage_status::CardState::NoCard;
    if (strcmp(page,"storage-unavailable") == 0) storage.cardState = storage_status::CardState::Unavailable;
    if (strcmp(page,"storage-error") == 0) {
      storage.cardState = storage_status::CardState::Error;
      storage.cardCapacityBytes = uint64_t(32) * 1024 * 1024 * 1024;
    }
  }
  uint16_t pixels[240 * 240];
  sloth::MenuUi menu;
  menu.open();
  const bool pongPage = strcmp(page, "pong") == 0 || strcmp(page, "pong-settings") == 0;
  const bool tetrisPage = strcmp(page, "tetris") == 0 || strcmp(page, "tetris-settings") == 0;
  const bool leafPage = strcmp(page, "leaf-sweep") == 0 || strcmp(page, "leaf-sweep-settings") == 0;
  const bool gamesPage = strcmp(page, "games") == 0 || strcmp(page, "games-fidget") == 0 || strcmp(page, "games-back") == 0;
  if (gamesPage || pongPage || tetrisPage || leafPage)
    menu.activate(sloth::MenuUi::kMainGames);
  if (pongPage) menu.activate(sloth::MenuUi::kGamesPong);
  if (tetrisPage) menu.activate(sloth::MenuUi::kGamesTetris);
  if (leafPage) menu.activate(sloth::MenuUi::kGamesLeafSweep);
  if (strcmp(page, "pong-settings") == 0) {
    menu.activate(sloth::MenuUi::kPongSettings);
    menu.setSpeeds(2, 4);
  }
  if (strcmp(page, "tetris-settings") == 0) menu.activate(sloth::MenuUi::kTetrisSettings);
  if (strcmp(page, "leaf-sweep-settings") == 0) menu.activate(sloth::MenuUi::kLeafSweepSettings);
  if (strcmp(page, "games-fidget") == 0)
    while (menu.focus() != sloth::MenuUi::kGamesForestFidget) menu.next();
  if (strcmp(page, "menu-bottom") == 0) {
    menu.touch(true, 120, 170); menu.touch(true, 120, 60); menu.touch(false, 120, 60);
  }
  if (strcmp(page, "menu-back") == 0 || strcmp(page, "games-back") == 0)
    while (menu.focus() != menu.rowCount()) menu.next();
  if (strcmp(page, "menu") == 0 || strcmp(page, "menu-bottom") == 0 || strcmp(page, "menu-back") == 0 || gamesPage ||
      pongPage || tetrisPage || leafPage)
    sloth::drawMenu(pixels, menu);
  else if (strcmp(page, "display-waiting") == 0 || strcmp(page, "display") == 0)
    sloth::drawDisplayStatus(pixels, sloth::DisplayStatus::Waiting);
  else if (strcmp(page, "display-permission") == 0)
    sloth::drawDisplayStatus(pixels, sloth::DisplayStatus::PermissionNeeded);
  else if (strcmp(page, "wifi-display") == 0)
    sloth::drawDisplayStatus(pixels, sloth::DisplayStatus::Waiting, true);
  else if (strcmp(page, "wifi-lost") == 0)
    sloth::drawDisplayStatus(pixels, sloth::DisplayStatus::Disconnected, true);
  else if (strcmp(page, "display-lost") == 0)
    sloth::drawDisplayStatus(pixels, sloth::DisplayStatus::Disconnected);
  else sloth::drawSettings(pixels, ui, motion, storage);
  if (strcmp(page, "icons") == 0 || strcmp(page, "icons-selected") == 0) {
    const bool selectedIcons = strcmp(page, "icons-selected") == 0;
    sloth::graphics::Canvas c{pixels};
    c.rect(0, 0, 240, 240, selectedIcons ? sloth::graphics::mint : sloth::graphics::ink);
    for (unsigned index = 1; index < static_cast<unsigned>(sloth::UiIcon::Count); ++index) {
      const int x = 18 + static_cast<int>((index - 1) % 7) * 30;
      const int y = 16 + static_cast<int>((index - 1) / 7) * 32;
      sloth::drawUiIcon(c, x, y, static_cast<sloth::UiIcon>(index), selectedIcons ? sloth::graphics::ink : sloth::graphics::mint);
      char label[4]; snprintf(label, sizeof(label), "%u", index);
      c.text(x + 2, y + 18, label, selectedIcons ? sloth::graphics::ink : sloth::graphics::muted);
    }
  }
  if (strncmp(page, "volume-", 7) == 0) {
    sloth::drawDisplayStatus(pixels, sloth::DisplayStatus::Waiting, true);
    sloth::drawVolumeOverlay(pixels, strcmp(page, "volume-min") == 0 ? 0 : strcmp(page, "volume-max") == 0 ? 60 : 35);
  }
  FILE* output = fopen(argv[1], "wb");
  if (!output) return 2;
  fprintf(output, "P6\n480 480\n255\n");
  for (int y = 0; y < 480; ++y) {
    for (int x = 0; x < 480; ++x) {
      const uint16_t pixel = pixels[(y / 2) * 240 + x / 2];
      const unsigned r = (pixel >> 11) & 31, g = (pixel >> 5) & 63, b = pixel & 31;
      const unsigned char color[] = {static_cast<unsigned char>((r << 3) | (r >> 2)),
                                    static_cast<unsigned char>((g << 2) | (g >> 4)),
                                    static_cast<unsigned char>((b << 3) | (b >> 2))};
      fwrite(color, 1, sizeof(color), output);
    }
  }
  return fclose(output) == 0 ? 0 : 3;
}
