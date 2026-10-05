#ifndef SLOTH_PET_SETTINGS_UI_H
#define SLOTH_PET_SETTINGS_UI_H

#include <stdint.h>
#include "keyboard.h"
#include "pet_settings.h"
#include "storage_status.h"

namespace sloth {

enum class SettingsPage : uint8_t { Main, Animal, Name, TimeZone, Scene, Motion, Storage, Rotation };
enum class SettingsEvent : uint8_t { None, Changed, SaveRequested, Cancelled, StorageRefresh };
enum class DisplayStatus : uint8_t { Waiting, PermissionNeeded, Disconnected };

// Read-only diagnostics supplied by the main loop, never sampled by the UI.
// ready means a fresh usable sample; available distinguishes a stale reading
// from an unavailable IMU. Tilt is in degrees. triggered is a caller-timed flash.
// Test counters are reset by the caller on entry, wake, and sensitivity changes.
struct MotionView {
  float x = 0, y = 0, z = 0;
  float magnitude = 0, motion = 0, peak = 0;
  float tiltX = 0, tiltY = 0;
  float threshold = 0.85f, sampleHz = 0;
  uint32_t samples = 0, triggers = 0;
  unsigned peaks = 0;
  bool available = false, ready = false, cooldown = false, triggered = false;
};

// Portable transactional settings editor. It owns a draft, never persistent
// storage or the live pet. SaveRequested leaves the editor open: the caller
// writes draft() atomically, applies it, then close() only on successful storage.
// Cancelled also leaves it open until the caller closes without applying.
class SettingsUi {
 public:
  enum {
    kMainAnimal = 0, kMainName = 1, kMainTimeZone = 2, kMainSubtitle = 3,
    kMainScene = 4, kMainShake = 5, kMainStorage = 6, kMainRotation = 7, kMainSave = 8, kMainCancel = 9,
    kStorageRefresh = 0, kStorageBack = 1,
    kMotionLess = 0, kMotionBack = 1, kMotionMore = 2,
    kNameSpace = 26, kNameDelete = 27, kNameSave = 28, kNameCancel = 29,
    kNameToggle = 30,
    kNavBack = 100, kNavNext = 101, kNavSelect = 102
  };

  SettingsUi();
  void open(const Settings& settings);
  void close();
  bool isOpen() const { return open_; }
  const Settings& draft() const { return draft_; }
  SettingsPage page() const { return page_; }
  int focus() const { return page_==SettingsPage::Name?keyboard_.focus():focus_; }
  int scrollOffset() const { return page_==SettingsPage::Name?keyboard_.scroll():scroll_; }
  int maxScroll() const;
  bool keyboardNumeric() const { return numeric_; }
  const char* nameBuffer() const { return name_; }
  const char* notice() const { return notice_; }

  // Notice is copied to bounded storage, so a temporary source is safe.
  void setNotice(const char* text);
  // Logical 240x240 touch coordinates; -1 for gaps/disabled controls/closed UI.
  int hitTest(int x, int y) const;
  // Feed every contact sample, then down=false on release. Taps activate only
  // on release; movement beyond 7 logical pixels cancels the tap and scrolls.
  SettingsEvent touch(bool down, int x, int y);
  void cancelTouch();
  void next();
  SettingsEvent activate(int target = -1);
  // Nested pages return to the corresponding main row without applying the
  // draft. Name editing discards its uncommitted buffer. Main returns Cancelled.
  SettingsEvent back();

  // Navigation/renderer inspection helpers; target identifiers are page-local.
  int targetCount() const;
  bool targetEnabled(int target) const;
  bool nameCanSave() const;
  char keyCharacter(int target) const;
  int visibleChoiceCount() const;
  int choiceCount() const;

  KeyboardSpec keyboardSpec() const;
  const Keyboard& keyboard() const { return keyboard_; }
 private:
  Keyboard keyboard_;
  SettingsEvent keyboardEvent(KeyboardEvent);
  bool open_;
  Settings draft_;
  SettingsPage page_;
  int focus_;
  int scroll_;
  bool touching_, dragged_, scrollGesture_;
  int touchX_, touchY_, touchScroll_, touchTarget_;
  bool numeric_;
  char name_[13];
  char notice_[37];

  void revealFocus();
  void enter(SettingsPage page);
  void mainPage(int row);
};

// Full-frame RGB565 rendering; no allocation, Arduino, touch, or storage calls.
void drawSettings(uint16_t* pixels, const SettingsUi& ui,
                  const MotionView& motion = MotionView(),
                  const storage_status::Snapshot& storage = storage_status::Snapshot());

// Desktop display waiting/error screen only; the host's frame replaces it once
// streaming. Input routing and the PWR-button exit belong to the caller.
// There are no touch controls or settings mutations on this screen.
void drawDisplayStatus(uint16_t* pixels, DisplayStatus status = DisplayStatus::Waiting,
                       bool wifi = false);

}  // namespace sloth

#endif  // SLOTH_PET_SETTINGS_UI_H
