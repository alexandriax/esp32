#include "settings_ui.h"
#include "local_time.h"
#include "pet_canvas.h"
#include "navigation_bar.h"
#include "ui_icons.h"

#include <stdio.h>
#include <string.h>
#include <cmath>

namespace sloth {
namespace {
const int kLeft = 12, kWidth = 210, kRowTop = 50, kRowStep = 36, kRowHeight = 32;
const int kViewportBottom = 198;
const int kFooterY = 208, kFooterHeight = 24;
const int kMainRowStep = 44, kMainRowHeight = 40;
const int kMotionFooterY = 205, kMotionFooterHeight = 23;

bool inside(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

void copyText(char* out, size_t capacity, const char* text) {
  size_t length = 0;
  if (text) {
    while (length + 1 < capacity && text[length]) {
      out[length] = text[length];
      ++length;
    }
  }
  out[length] = '\0';
}

using graphics::Canvas;
using graphics::cream;
using graphics::gold;
using graphics::ink;
using graphics::mint;
using graphics::muted;
using graphics::rgb;

void button(Canvas& c, int x, int y, int width, int height, const char* text,
            bool focused, bool enabled = true, bool primary = false, int scale = 1, UiIcon icon = UiIcon::None) {
  const uint16_t fill = primary && enabled ? mint : rgb(29, 53, 47);
  if (focused && enabled) c.roundRect(x - 1, y - 1, width + 2, height + 2, 5, gold);
  c.roundRect(x, y, width, height, 4, fill);
  const int textWidth = (static_cast<int>(strlen(text)) * 6 - 1) * scale;
  const uint16_t color = !enabled ? rgb(84, 108, 95) : primary ? ink : cream;
  const bool hasIcon = icon != UiIcon::None;
  if (hasIcon && textWidth + 24 > width && height >= 30) {
    drawUiIcon(c, x + (width - 16) / 2, y + 2, icon, color);
    c.text(x + (width - textWidth) / 2, y + height - 10, text, color, scale);
  } else {
    const int start = x + (width - textWidth - (hasIcon ? 20 : 0)) / 2;
    if (hasIcon) drawUiIcon(c, start, y + (height - 16) / 2, icon, color);
    c.text(start + (hasIcon ? 20 : 0), y + (height - 7 * scale) / 2, text, color, scale);
  }
}

const char* pageTitle(SettingsPage page) {
  switch (page) {
    case SettingsPage::Main: return "SETTINGS";
    case SettingsPage::Animal: return "YOUR ANIMAL";
    case SettingsPage::Name: return "PET NAME";
    case SettingsPage::TimeZone: return "TIME ZONE";
    case SettingsPage::Scene: return "YOUR SCENE";
    case SettingsPage::Motion: return "SHAKE TEST";
    case SettingsPage::Storage: return "STORAGE";
    case SettingsPage::Rotation: return "SCREEN ROTATION";
  }
  return "SETTINGS";
}

const char* rotationLabel(unsigned turns) {
  static const char* const labels[] = {"0 DEGREES", "90 DEGREES CW", "180 DEGREES", "270 DEGREES CW"};
  return labels[turns & 3u];
}

const char* choiceLabel(const SettingsUi& ui, int choice) {
  if (ui.page() == SettingsPage::Rotation) return rotationLabel(choice);
  if (ui.page() == SettingsPage::Animal) return animalName(static_cast<Animal>(choice));
  if (ui.page() == SettingsPage::Scene) return sceneName(static_cast<Scene>(choice));
  return zoneLabel(static_cast<uint8_t>(choice));
}

UiIcon animalIcon(Animal animal) {
  return animal == Animal::Cat ? UiIcon::Cat : animal == Animal::Frog ? UiIcon::Frog :
      animal == Animal::SunConure ? UiIcon::SunConure : UiIcon::Sloth;
}

UiIcon sceneIcon(Scene scene) {
  const UiIcon icons[] = {UiIcon::Jungle, UiIcon::Meadow, UiIcon::Night, UiIcon::NYC,
                         UiIcon::Space, UiIcon::Island, UiIcon::Sea};
  const unsigned index = static_cast<unsigned>(scene);
  return index < static_cast<unsigned>(Scene::Count) ? icons[index] : UiIcon::Jungle;
}

UiIcon choiceIcon(const SettingsUi& ui, int choice) {
  if (ui.page() == SettingsPage::Rotation) return UiIcon::Settings;
  if (ui.page() == SettingsPage::Animal) return animalIcon(static_cast<Animal>(choice));
  if (ui.page() == SettingsPage::Scene) return sceneIcon(static_cast<Scene>(choice));
  return UiIcon::Clock;
}

int selectedChoice(const SettingsUi& ui) {
  if (ui.page() == SettingsPage::Rotation) return ui.draft().screenRotation;
  if (ui.page() == SettingsPage::Animal) return static_cast<int>(ui.draft().animal);
  if (ui.page() == SettingsPage::Scene) return static_cast<int>(ui.draft().scene);
  return ui.draft().timeZone;
}

float bounded(float value, float low, float high) {
  return value < low ? low : value > high ? high : value;
}

void metric(Canvas& c, int y, const char* label, float value, bool fresh, bool signedValue) {
  char number[16];
  if (!fresh || !std::isfinite(value)) copyText(number, sizeof(number), "--.--G");
  else snprintf(number, sizeof(number), signedValue ? "%+.2fG" : "%.2fG",
                static_cast<double>(bounded(value, signedValue ? -99.99f : 0.0f, 99.99f)));
  c.text(20, y, label, muted);
  const int width = static_cast<int>(strlen(number)) * 6 - 1;
  c.text(138 - width, y, number, fresh ? cream : muted);
}

// Binary units make the physical flash and filesystem figures comparable.
// Integer arithmetic remains bounded even for malformed UINT64_MAX telemetry.
void storageSize(char* text, size_t capacity, uint64_t bytes) {
  static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
  unsigned unit = 0;
  uint64_t divisor = 1;
  while (unit < 6 && bytes / divisor >= 1024) { divisor *= 1024; ++unit; }
  const uint64_t whole = bytes / divisor;
  const unsigned tenth = static_cast<unsigned>((bytes % divisor) * 10 / divisor);
  if (!unit) snprintf(text, capacity, "%lluB", static_cast<unsigned long long>(whole));
  else snprintf(text, capacity, "%llu.%u%s", static_cast<unsigned long long>(whole),
                tenth > 9 ? 9 : tenth, units[unit]);
}

void storagePair(Canvas& c, int y, const char* label, bool valid, uint64_t used, uint64_t total) {
  char first[20], second[20], line[64];
  if (!valid || used > total) snprintf(line, sizeof(line), "%s UNAVAILABLE", label);
  else {
    storageSize(first, sizeof(first), used); storageSize(second, sizeof(second), total);
    snprintf(line, sizeof(line), "%s %s / %s", label, first, second);
  }
  c.text(24, y, line, valid && used <= total ? cream : muted);
}

void drawStorage(Canvas& c, const SettingsUi& ui, const storage_status::Snapshot& view) {
  c.roundRect(16, 50, 208, 72, 5, rgb(24, 45, 42));
  drawUiIcon(c, 23, 54, UiIcon::Storage, mint);
  c.text(45, 58, "ONBOARD FLASH", mint);
  char size[20], line[48];
  storageSize(size, sizeof(size), view.flashBytes);
  snprintf(line, sizeof(line), "CAPACITY %s", view.flashValid ? size : "UNAVAILABLE");
  c.text(24, 75, line, cream);
  storagePair(c, 90, "FIRMWARE", view.firmwareValid, view.firmwareBytes, view.firmwareCapacityBytes);
  // NVS uses 32-byte slots. Occupied/available slot storage includes its metadata,
  // so this is allocation usage, not the length of the saved user payloads.
  const bool safeEntries = view.nvsUsedEntries <= UINT64_MAX / 32 && view.nvsTotalEntries <= UINT64_MAX / 32;
  storagePair(c, 105, "SAVES", view.nvsValid && safeEntries,
              safeEntries ? view.nvsUsedEntries * 32 : 0, safeEntries ? view.nvsTotalEntries * 32 : 0);

  c.roundRect(16, 129, 208, 68, 5, rgb(24, 45, 42));
  c.text(24, 137, "MICROSD", mint);
  using storage_status::CardState;
  const bool validCard = view.cardTotalBytes > 0 && (!view.cardSpaceKnown ||
      (view.cardUsedBytes <= view.cardTotalBytes && view.cardFreeBytes == view.cardTotalBytes - view.cardUsedBytes));
  if (view.cardState == CardState::Ready && validCard) {
    const char* labels[] = {"CAPACITY", view.cardSpaceCached?"USED (EST)":"USED", view.cardSpaceCached?"FREE (EST)":"FREE"};
    const uint64_t values[] = {view.cardTotalBytes, view.cardUsedBytes, view.cardFreeBytes};
    for (int i = 0; i < 3; ++i) {
      if(i && !view.cardSpaceKnown)strcpy(size,"NOT COUNTED");
      else storageSize(size, sizeof(size), values[i]);
      c.text(24, 153 + i * 13, labels[i], muted);
      c.text(216 - static_cast<int>(strlen(size)) * 6 + 1, 153 + i * 13, size, cream);
    }
  } else {
    const char* status = "CARD UNAVAILABLE";
    const char* help = "INSERT A FAT32 CARD";
    switch (view.cardState) {
      case CardState::Unchecked: status = "NOT CHECKED"; help = "TAP REFRESH TO CHECK"; break;
      case CardState::Checking: status = "CHECKING..."; help = "PLEASE WAIT"; break;
      case CardState::NoCard: status = "NO CARD DETECTED"; help = "INSERT CARD, THEN REFRESH"; break;
      case CardState::Error: status = "COULD NOT READ CARD"; help = "CHECK CARD, THEN REFRESH"; break;
      case CardState::Ready: case CardState::Unavailable: break;
    }
    const bool knownCard = view.cardCapacityBytes && view.cardState == CardState::Error;
    c.text(24, knownCard ? 151 : 155, status, gold);
    if (knownCard) {
      storageSize(size, sizeof(size), view.cardCapacityBytes);
      snprintf(line, sizeof(line), "CAPACITY %s", size);
      c.text(24, 166, line, cream);
    }
    c.text(24, knownCard ? 183 : 177, help, muted);
  }
  button(c, 24, 204, 94, 24, "REFRESH", ui.focus() == SettingsUi::kStorageRefresh, true, false, 1, UiIcon::Continue);
  button(c, 124, 204, 92, 24, "BACK", ui.focus() == SettingsUi::kStorageBack, true, false, 1, UiIcon::Back);
}

void drawMotion(Canvas& c, const SettingsUi& ui, const MotionView& view) {
  const bool finite = std::isfinite(view.x) && std::isfinite(view.y) &&
      std::isfinite(view.z) && std::isfinite(view.magnitude) &&
      std::isfinite(view.motion) && std::isfinite(view.peak) &&
      std::isfinite(view.tiltX) && std::isfinite(view.tiltY);
  const bool fresh = view.available && view.ready && finite;
  c.roundRect(12, 49, 132, 81, 5, rgb(24, 45, 42));
  metric(c, 55, "X", view.x, fresh, true);
  metric(c, 67, "Y", view.y, fresh, true);
  metric(c, 79, "Z", view.z, fresh, true);
  c.line(20, 89, 136, 89, rgb(48, 74, 63));
  metric(c, 95, "TOTAL", view.magnitude, fresh, false);
  metric(c, 107, "MOTION", view.motion, fresh, false);
  metric(c, 119, "PEAK", view.peak, fresh, false);

  c.roundRect(151, 49, 77, 84, 5, rgb(24, 45, 42));
  c.text(166, 54, "TILT DEG", muted);
  c.oval(190, 86, 21, 21, muted);
  c.oval(190, 86, 20, 20, ink);
  c.line(173, 86, 207, 86, rgb(57, 86, 71));
  c.line(190, 69, 190, 103, rgb(57, 86, 71));
  if (fresh) {
    float dx = bounded(view.tiltX, -45.0f, 45.0f) * 16.0f / 45.0f;
    float dy = -bounded(view.tiltY, -45.0f, 45.0f) * 16.0f / 45.0f;
    const float radius = std::sqrt(dx * dx + dy * dy);
    if (radius > 16.0f) { dx *= 16.0f / radius; dy *= 16.0f / radius; }
    const int x = 190 + static_cast<int>(std::lround(dx));
    const int y = 86 + static_cast<int>(std::lround(dy));
    c.oval(x, y, 4, 4, radius < 3.0f ? mint : gold);
    c.dot(x - 1, y - 1, cream);
  } else c.text(185, 83, "--", muted);

  char text[40];
  if (std::isfinite(view.threshold))
    snprintf(text, sizeof(text), "LIMIT %.2fG",
             static_cast<double>(bounded(view.threshold, 0.0f, 9.99f)));
  else copyText(text, sizeof(text), "LIMIT --.--");
  c.text(157, 113, text, muted);
  if (fresh && std::isfinite(view.sampleHz))
    snprintf(text, sizeof(text), "RATE %5.1fHZ",
             static_cast<double>(bounded(view.sampleHz, 0.0f, 999.9f)));
  else copyText(text, sizeof(text), "RATE  --.-HZ");
  c.text(154, 125, text, muted);
  snprintf(text, sizeof(text), "SAMPLES %lu", static_cast<unsigned long>(view.samples));
  c.text(16, 138, text, muted);
  if (fresh) snprintf(text, sizeof(text), "PEAKS %u/3", view.peaks > 3 ? 3 : view.peaks);
  else copyText(text, sizeof(text), "PEAKS -/3");
  c.text(169, 138, text, muted);

  const bool flash = fresh && view.triggered;
  c.roundRect(12, 150, 216, 20, 5, flash ? mint : rgb(37, 60, 49));
  if (!view.available) copyText(text, sizeof(text), "IMU UNAVAILABLE");
  else if (!fresh) copyText(text, sizeof(text), "DATA STALE - WAITING");
  else snprintf(text, sizeof(text), "%s  COUNT %lu",
                flash ? "DETECTED!" : view.cooldown ? "SETTLING" : "READY",
                static_cast<unsigned long>(view.triggers));
  c.centered(157, text, flash ? ink : fresh ? cream : gold);

  snprintf(text, sizeof(text), "SENSITIVITY: %s", shakeSensitivityName(ui.draft().shakeSensitivity));
  c.centered(178, text, cream);
  for (unsigned level = 0; level < kShakeSensitivityCount; ++level)
    c.roundRect(74 + static_cast<int>(level) * 19, 192, 16, 4, 1,
                level <= ui.draft().shakeSensitivity ? mint : rgb(49, 73, 61));
  button(c, 12, kMotionFooterY, 68, kMotionFooterHeight, "LESS",
         ui.focus() == SettingsUi::kMotionLess, ui.targetEnabled(SettingsUi::kMotionLess), false, 1, UiIcon::Minus);
  button(c, 86, kMotionFooterY, 68, kMotionFooterHeight, "BACK",
         ui.focus() == SettingsUi::kMotionBack, true, false, 1, UiIcon::Back);
  button(c, 160, kMotionFooterY, 68, kMotionFooterHeight, "MORE",
         ui.focus() == SettingsUi::kMotionMore, ui.targetEnabled(SettingsUi::kMotionMore), false, 1, UiIcon::Plus);
}
}  // namespace

SettingsUi::SettingsUi()
    : open_(false), draft_(), page_(SettingsPage::Main), focus_(0), scroll_(0), touching_(false), dragged_(false), scrollGesture_(false),
      touchX_(0), touchY_(0), touchScroll_(0), touchTarget_(-1), numeric_(false), name_{}, notice_{} {}

void SettingsUi::open(const Settings& settings) {
  draft_ = validSettings(settings) ? settings : Settings();
  open_ = true;
  page_ = SettingsPage::Main;
  focus_ = 0;
  scroll_ = 0;
  cancelTouch();
  numeric_ = false;
  name_[0] = notice_[0] = '\0';
}

KeyboardSpec SettingsUi::keyboardSpec() const {
  static const uint8_t numericTargets[]={0,1,2,3,4,5,6,7,8,9,10,11,26};
  KeyboardSpec s;s.text=name_;s.keys=numeric_?"1234567890-' ":"ABCDEFGHIJKLMNOPQRSTUVWXYZ ";s.targets=numeric_?numericTargets:nullptr;
  s.title="PET NAME";s.capacity=sizeof(name_);s.targetCount=31;s.doneEnabled=nameCanSave();s.modeAction=true;
  s.actions[0]=kNameDelete;s.actions[1]=kNameToggle;s.actions[2]=kNameCancel;s.actions[3]=kNameSave;
  s.labels[1]=numeric_?"ABC":"123 / #+=";s.labels[3]="USE";return s;
}
SettingsEvent SettingsUi::keyboardEvent(KeyboardEvent event){
  if(event==KeyboardEvent::Back || event==KeyboardEvent::Cancel)return back();
  if(event==KeyboardEvent::Mode){numeric_=!numeric_;keyboard_.reset(kNameToggle);return SettingsEvent::None;}
  if(event==KeyboardEvent::Done){
    Settings candidate=draft_;
    if(setName(candidate,name_)){const bool changed=strcmp(candidate.name,draft_.name)!=0;draft_=candidate;mainPage(kMainName);return changed?SettingsEvent::Changed:SettingsEvent::None;}
  }
  return SettingsEvent::None;
}
void SettingsUi::close() { open_ = false; cancelTouch(); }

void SettingsUi::setNotice(const char* text) { copyText(notice_, sizeof(notice_), text); }

int SettingsUi::choiceCount() const {
  if (page_ == SettingsPage::Animal) return static_cast<int>(Animal::Count);
  if (page_ == SettingsPage::Scene) return static_cast<int>(Scene::Count);
  if (page_ == SettingsPage::TimeZone) return zoneCount();
  if (page_ == SettingsPage::Rotation) return 4;
  return 0;
}

int SettingsUi::visibleChoiceCount() const {
  int count = 0;
  for (int i = 0; i < choiceCount(); ++i) {
    const int top = kRowTop + i * kRowStep - scroll_;
    if (top < kViewportBottom && top + kRowHeight > kRowTop) ++count;
  }
  return count;
}

int SettingsUi::targetCount() const {
  if (!open_) return 0;
  if (page_ == SettingsPage::Main) return kMainCancel + 1;
  if (page_ == SettingsPage::Motion) return 3;
  if (page_ == SettingsPage::Storage) return 2;
  if (page_ == SettingsPage::Name) return 31;
  return choiceCount() + 1; // Absolute choice IDs followed by Back.
}

int SettingsUi::maxScroll() const {
  if(page_==SettingsPage::Name)return Keyboard::maxScroll(keyboardSpec());
  int content = 0, viewport = kViewportBottom - kRowTop;
  if (page_ == SettingsPage::Main) content = kMainSave * kMainRowStep - 4;
  else content = choiceCount() * kRowStep - 4;
  return content > viewport ? content - viewport : 0;
}

void SettingsUi::cancelTouch() { keyboard_.cancelTouch(); touching_ = dragged_ = scrollGesture_ = false; }

SettingsEvent SettingsUi::touch(bool down, int x, int y) {
  if (!open_) { cancelTouch(); return SettingsEvent::None; }
  if(page_==SettingsPage::Name)return keyboardEvent(keyboard_.touch(keyboardSpec(),name_,down,x,y));
  if (down) {
    if (!touching_) {
      touching_ = true; dragged_ = false;
      touchX_ = x; touchY_ = y; touchScroll_ = scroll_;
      touchTarget_ = hitTest(x, y);
      const int top = kRowTop;
      scrollGesture_ = page_ != SettingsPage::Motion && page_ != SettingsPage::Storage && x >= 0 && x < 240 &&
          y >= top && y < kViewportBottom;
    }
    if (x - touchX_ > 7 || touchX_ - x > 7 || y - touchY_ > 7 || touchY_ - y > 7)
      dragged_ = true;
    if (dragged_ && scrollGesture_) {
      const int offset = touchScroll_ + touchY_ - y;
      scroll_ = offset < 0 ? 0 : offset > maxScroll() ? maxScroll() : offset;
    }
    return SettingsEvent::None;
  }
  if (!touching_) return SettingsEvent::None;
  const int target = !dragged_ && touchTarget_ == hitTest(x, y) ? touchTarget_ : -1;
  cancelTouch();
  return target >= 0 ? activate(target) : SettingsEvent::None;
}

void SettingsUi::revealFocus() {
  int top = 0, height = 0, viewport = kViewportBottom - kRowTop;
  if (page_ == SettingsPage::Main && focus_ < kMainSave) {
    top = focus_ * kMainRowStep; height = kMainRowHeight;
  } else if (choiceCount() && focus_ < choiceCount()) {
    top = focus_ * kRowStep; height = kRowHeight;
  } else return;
  if (top < scroll_) scroll_ = top;
  else if (top + height > scroll_ + viewport) scroll_ = top + height - viewport;
  if (scroll_ > maxScroll()) scroll_ = maxScroll();
}

char SettingsUi::keyCharacter(int target) const { return Keyboard::character(keyboardSpec(),target); }

bool SettingsUi::nameCanSave() const {
  Settings candidate = draft_;
  return setName(candidate, name_);
}

bool SettingsUi::targetEnabled(int target) const {
  if (open_ && (target == kNavBack || target == kNavNext)) return true;
  if (open_ && target == kNavSelect) return targetEnabled(focus());
  if (target < 0 || target >= targetCount()) return false;
  if (page_ == SettingsPage::Motion) {
    if (target == kMotionLess) return draft_.shakeSensitivity > 0;
    if (target == kMotionMore) return draft_.shakeSensitivity + 1 < kShakeSensitivityCount;
  } else if (page_ == SettingsPage::Name) return Keyboard::enabled(keyboardSpec(),target);
  return true;
}

void SettingsUi::next() {
  if(open_ && page_==SettingsPage::Name){keyboard_.next(keyboardSpec());return;}
  const int count = targetCount();
  if (!count) return;
  cancelTouch();
  do { focus_ = (focus_ + 1) % count; } while (!targetEnabled(focus_));
  revealFocus();
}

void SettingsUi::mainPage(int row) {
  page_ = SettingsPage::Main;
  focus_ = row;
  scroll_ = 0;
  cancelTouch();
  revealFocus();
}

void SettingsUi::enter(SettingsPage page) {
  page_ = page;
  scroll_ = 0;
  cancelTouch();
  if (page == SettingsPage::Storage) {
    focus_ = kStorageBack;
  } else if (page == SettingsPage::Motion) {
    focus_ = kMotionBack;
  } else if (page == SettingsPage::Name) {
    copyText(name_, sizeof(name_), draft_.name);keyboard_.reset();
    numeric_ = false;
    focus_ = 0;
  } else {
    const int choice = selectedChoice(*this);
    focus_ = choice;
    revealFocus();
  }
}

SettingsEvent SettingsUi::activate(int target) {
  if (!open_) return SettingsEvent::None;
  if(page_==SettingsPage::Name)return keyboardEvent(keyboard_.activate(keyboardSpec(),name_,target));
  if (target == kNavBack) return back();
  if (target == kNavNext) { next(); return SettingsEvent::Changed; }
  if (target == kNavSelect) target = focus_;
  if (target == -1) target = focus_;
  if (!targetEnabled(target)) return SettingsEvent::None;
  cancelTouch();
  focus_ = target;
  revealFocus();
  notice_[0] = '\0';
  if (page_ == SettingsPage::Main) {
    switch (target) {
      case kMainAnimal: enter(SettingsPage::Animal); break;
      case kMainName: enter(SettingsPage::Name); break;
      case kMainTimeZone: enter(SettingsPage::TimeZone); break;
      case kMainSubtitle:
        draft_.showSubtitle = !draft_.showSubtitle;
        return SettingsEvent::Changed;
      case kMainScene: enter(SettingsPage::Scene); break;
      case kMainRotation: enter(SettingsPage::Rotation); break;
      case kMainShake: enter(SettingsPage::Motion); break;
      case kMainStorage: enter(SettingsPage::Storage); return SettingsEvent::StorageRefresh;
      case kMainSave: return SettingsEvent::SaveRequested;
      case kMainCancel: return SettingsEvent::Cancelled;
    }
    return SettingsEvent::None;
  }
  if (page_ == SettingsPage::Storage) {
    if (target == kStorageRefresh) return SettingsEvent::StorageRefresh;
    mainPage(kMainStorage);
    return SettingsEvent::None;
  }
  if (page_ == SettingsPage::Motion) {
    if (target == kMotionBack) {
      mainPage(kMainShake);
      return SettingsEvent::None;
    }
    if (target == kMotionLess) --draft_.shakeSensitivity;
    else if (target == kMotionMore) ++draft_.shakeSensitivity;
    return SettingsEvent::Changed;
  }
  const int count = choiceCount();
  if (target < count) {
    const int choice = target;
    const bool changed = choice != selectedChoice(*this);
    int row = kMainTimeZone;
    if (page_ == SettingsPage::Animal) {
      draft_.animal = static_cast<Animal>(choice);
      row = kMainAnimal;
    } else if (page_ == SettingsPage::Scene) {
      draft_.scene = static_cast<Scene>(choice);
      row = kMainScene;
    } else if (page_ == SettingsPage::Rotation) {
      draft_.screenRotation = static_cast<uint8_t>(choice);
      row = kMainRotation;
    } else draft_.timeZone = static_cast<uint8_t>(choice);
    mainPage(row);
    return changed ? SettingsEvent::Changed : SettingsEvent::None;
  }
  mainPage(page_ == SettingsPage::Animal ? kMainAnimal :
           page_ == SettingsPage::TimeZone ? kMainTimeZone :
           page_ == SettingsPage::Rotation ? kMainRotation : kMainScene);
  return SettingsEvent::None;
}

SettingsEvent SettingsUi::back() {
  if (!open_) return SettingsEvent::None;
  cancelTouch();
  notice_[0] = '\0';
  switch (page_) {
    case SettingsPage::Main: return SettingsEvent::Cancelled;
    case SettingsPage::Animal: mainPage(kMainAnimal); break;
    case SettingsPage::Name:
      name_[0] = '\0';
      mainPage(kMainName);
      break;
    case SettingsPage::TimeZone: mainPage(kMainTimeZone); break;
    case SettingsPage::Scene: mainPage(kMainScene); break;
    case SettingsPage::Motion: mainPage(kMainShake); break;
    case SettingsPage::Storage: mainPage(kMainStorage); break;
    case SettingsPage::Rotation: mainPage(kMainRotation); break;
  }
  return SettingsEvent::None;
}

int SettingsUi::hitTest(int x, int y) const {
  if (!open_ || x < 0 || x >= 240 || y < 0 || y >= 240) return -1;
  if (inside(x, y, 20, 8, 200, 16)) {
    const int nav = x < 86 ? kNavNext : x < 154 ? kNavBack : kNavSelect;
    return targetEnabled(nav) ? nav : -1;
  }
  int target = -1;
  if (page_ == SettingsPage::Storage) {
    if (inside(x, y, 24, 204, 94, 24)) target = kStorageRefresh;
    if (inside(x, y, 124, 204, 92, 24)) target = kStorageBack;
  } else if (page_ == SettingsPage::Motion) {
    for (int i = 0; i < 3; ++i)
      if (inside(x, y, 12 + i * 74, kMotionFooterY, 68, kMotionFooterHeight)) target = i;
  } else if (page_ == SettingsPage::Name) return keyboard_.hitTest(keyboardSpec(),x,y);
  else {
    const bool main = page_ == SettingsPage::Main;
    const int count = main ? kMainSave : choiceCount();
    if (y >= kRowTop && y < kViewportBottom) {
      for (int i = 0; i < count; ++i) {
        const int y0 = kRowTop + i * (main ? kMainRowStep : kRowStep) - scroll_;
        if (inside(x, y, kLeft, y0, kWidth, main ? kMainRowHeight : kRowHeight)) target = i;
      }
    }
    if (main) {
      if (inside(x, y, 12, kFooterY, 100, kFooterHeight)) target = kMainCancel;
      if (inside(x, y, 128, kFooterY, 100, kFooterHeight)) target = kMainSave;
    } else if (inside(x, y, 12, kFooterY, 216, kFooterHeight)) target = count;
  }
  return targetEnabled(target) ? target : -1;
}

void drawSettings(uint16_t* pixels, const SettingsUi& ui, const MotionView& motion,
                  const storage_status::Snapshot& storage) {
  if (!pixels) return;
  Canvas c = {pixels};
  c.rect(0, 0, 240, 240, ink);
  if (!ui.isOpen()) return;
  if(ui.page()==SettingsPage::Name){drawKeyboard(pixels,ui.keyboardSpec(),ui.keyboard());return;}
  if (ui.page() == SettingsPage::Storage) {
    drawNavigationBar(c);
    c.centered(30, pageTitle(ui.page()), cream, 2);
    drawStorage(c, ui, storage);
    return;
  }
  if (ui.page() == SettingsPage::Motion) {
    drawNavigationBar(c);
    c.centered(30, pageTitle(ui.page()), cream, 2);
    drawMotion(c, ui, motion);
    return;
  }
  const bool main = ui.page() == SettingsPage::Main;
  const int viewportTop = kRowTop;
  if (main) {
    const char* labels[] = {"ANIMAL", "NAME", "TIME ZONE", "SUBTITLE", "SCENE", "SHAKE", "STORAGE", "SCREEN ROTATION"};
    const UiIcon icons[] = {animalIcon(ui.draft().animal), UiIcon::Name, UiIcon::Clock,
                            UiIcon::Subtitle, sceneIcon(ui.draft().scene), UiIcon::Shake, UiIcon::Storage, UiIcon::Settings};
    const char* values[] = {animalName(ui.draft().animal), ui.draft().name,
                           zoneLabel(ui.draft().timeZone), ui.draft().showSubtitle ? "ON" : "OFF",
                           sceneName(ui.draft().scene), shakeSensitivityName(ui.draft().shakeSensitivity), "FLASH + MICROSD", rotationLabel(ui.draft().screenRotation)};
    for (int i = 0; i < SettingsUi::kMainSave; ++i) {
      const int y = kRowTop + i * kMainRowStep - ui.scrollOffset();
      if (y + kMainRowHeight <= viewportTop || y >= kViewportBottom) continue;
      button(c, kLeft, y, kWidth, kMainRowHeight, "", ui.focus() == i);
      drawUiIcon(c, 20, y + 12, icons[i], mint);
      c.text(44, y + 8, labels[i], muted);
      c.text(44, y + 24, values[i], cream);
      c.line(207, y + 16, 211, y + 20, gold);
      c.line(211, y + 20, 207, y + 24, gold);
    }
  } else {
    for (int i = 0; i < ui.choiceCount(); ++i) {
      const int y = kRowTop + i * kRowStep - ui.scrollOffset();
      if (y + kRowHeight <= viewportTop || y >= kViewportBottom) continue;
      button(c, kLeft, y, kWidth, kRowHeight, "", ui.focus() == i);
      drawUiIcon(c, 20, y + 8, choiceIcon(ui, i), mint);
      c.text(44, y + 13, choiceLabel(ui, i), cream);
      if (i == selectedChoice(ui)) drawUiIcon(c, 199, y + 8, UiIcon::Check, mint);
    }
  }
  // Mask scrolling content behind the fixed header/footer. This clips every
  // primitive without allocating another framebuffer or changing Canvas.
  c.rect(0, 0, 240, viewportTop, ink);
  c.rect(0, kViewportBottom, 240, 240 - kViewportBottom, ink);
  drawNavigationBar(c);
  c.centered(30, pageTitle(ui.page()), cream, 2);
  if (ui.maxScroll() > 0) {
    const int track = kViewportBottom - viewportTop;
    const int thumb = track * track / (track + ui.maxScroll());
    const int y = viewportTop + ui.scrollOffset() * (track - thumb) / ui.maxScroll();
    c.roundRect(232, viewportTop, 3, track, 1, rgb(37, 60, 49));
    c.roundRect(232, y, 3, thumb, 1, mint);
  }
  if (main) {
    if (ui.notice()[0]) c.centered(199, ui.notice(), gold);
    button(c, 12, kFooterY, 100, kFooterHeight, "CANCEL", ui.focus() == SettingsUi::kMainCancel, true, false, 1, UiIcon::Cancel);
    button(c, 128, kFooterY, 100, kFooterHeight, "SAVE", ui.focus() == SettingsUi::kMainSave, true, true, 1, UiIcon::Save);
  } else {
    button(c, 12, kFooterY, 216, kFooterHeight, "BACK", ui.focus() == ui.choiceCount(), true, false, 1, UiIcon::Back);
  }
}

void drawDisplayStatus(uint16_t* pixels, DisplayStatus status, bool wifi) {
  if (!pixels) return;
  Canvas c = {pixels};
  c.rect(0, 0, 240, 240, ink);
  c.centered(14, "REMOTE DISPLAY", cream, 2);
  c.centered(35, wifi ? "480 X 480 / 2.4GHZ WI-FI" : "480 X 480 / USB-C", muted);

  // The square panel is deliberately idle: no fabricated connected desktop.
  c.roundRect(84, 54, 72, 74, 6, mint);
  c.roundRect(87, 57, 66, 68, 3, rgb(24, 45, 42));
  c.roundRect(94, 67, 52, 37, 3, rgb(49, 73, 61));
  c.rect(97, 70, 46, 3, muted);
  c.rect(97, 77, 16, 23, rgb(37, 60, 49));
  c.rect(116, 77, 27, 23, rgb(24, 45, 42));
  c.centered(111, wifi ? "WI-FI" : "USB", cream);
  c.rect(115, 128, 10, 7, mint);
  c.roundRect(103, 135, 34, 4, 1, mint);

  const char* title = "WAITING FOR YOUR MAC";
  const char* instruction = "OPEN MOSS DISPLAY ON MAC";
  const char* detail = wifi ? "USB ONLY FOR FIRST SETUP" : "KEEP USB CONNECTED";
  if (status == DisplayStatus::PermissionNeeded) {
    title = "PERMISSION NEEDED";
    instruction = "ALLOW SCREEN RECORDING";
    detail = "IN MAC SYSTEM SETTINGS";
  } else if (status == DisplayStatus::Disconnected) {
    title = "CONNECTION LOST";
    instruction = "REOPEN MOSS DISPLAY ON MAC";
    detail = wifi ? "CHECK WI-FI CONNECTION" : "CHECK THE USB CONNECTION";
  }
  c.centered(153, title, gold);
  c.centered(175, instruction, cream);
  c.centered(187, detail, muted);
  c.line(34, 208, 206, 208, rgb(49, 73, 61));
  c.centered(213, "BOOT: VOL- / KEY: VOL+", mint);
  c.centered(227, "PWR TAP: MENU", cream);
}

}  // namespace sloth
