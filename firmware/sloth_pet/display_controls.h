#pragma once
#include "display_idle.h"
#include "audio_buffer.h"

namespace sloth {
enum class SideButtonAction { None, Next, Select, Both };

// Front-left BOOT and front-right KEY follow the menu's NEXT / BACK / SELECT
// rail on pet home and menus. Games and live display use their own button order.
inline SideButtonAction sideButtonAction(bool key, bool boot, bool menuNavigation, bool pong = false) {
  if (key == boot) return key && pong ? SideButtonAction::Both : SideButtonAction::None;
  return key != menuNavigation ? SideButtonAction::Next : SideButtonAction::Select;
}

// Shared policy for PMIC short taps and the USB diagnostic equivalent. Long
// holds never enter this path: the PMIC continues to own hardware power-off.
inline bool displayPowerTap(DisplayIdle& idle, bool displayMode, uint32_t now) {
  if (displayMode) { idle.begin(now); return true; } // Exit into visible Settings.
  idle.powerTap(now);
  return false;
}

inline uint8_t steppedDisplayVolume(uint8_t current, bool increase) {
  const unsigned bounded = current > audio_output::kMaxVolume ? audio_output::kMaxVolume : current;
  return increase ? static_cast<uint8_t>(bounded + 5 > audio_output::kMaxVolume ? audio_output::kMaxVolume : bounded + 5)
                  : static_cast<uint8_t>(bounded < 5 ? 0 : bounded - 5);
}
} // namespace sloth
