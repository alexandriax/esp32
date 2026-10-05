#ifndef SLOTH_PET_DISPLAY_IDLE_H
#define SLOTH_PET_DISPLAY_IDLE_H

#include <stdint.h>

namespace sloth {

enum class DisplayLevel : uint8_t { Bright, Dim, Off };

// Display policy only: no hardware access and no pet-state changes. The caller
// compares level() before/after an operation and applies the corresponding
// brightness or panel-power transition. All timestamps are uint32_t millis().
class DisplayIdle {
 public:
  enum : uint32_t {
    kDimAfterMs = 60000,
    kOffAfterMs = 120000
  };

  DisplayIdle() : level_(DisplayLevel::Bright), lastInteraction_(0), started_(false) {}

  // Start/reinitialize the policy with a bright display and a fresh idle period.
  void begin(uint32_t now) {
    level_ = DisplayLevel::Bright;
    lastInteraction_ = now;
    started_ = true;
  }

  DisplayLevel level() const { return level_; }

  // Diagnostic elapsed milliseconds, modulo the millis() counter's full wrap.
  uint32_t idleMs(uint32_t now) const {
    return started_ ? now - lastInteraction_ : 0;
  }

  // Force the same latched Off state for a diagnostic/manual screen-off action.
  // This is not an interaction and does not change the idle origin.
  void screenOffNow() {
    level_ = DisplayLevel::Off;
    started_ = true;
  }

  // Dim/off deadlines are both measured from the latest accepted interaction.
  // Unsigned subtraction handles millis rollover. Once Off, no elapsed time
  // (including an entire millis wrap) can light the screen again.
  DisplayLevel update(uint32_t now) {
    if (!started_) begin(now);
    if (level_ == DisplayLevel::Off) return level_;
    const uint32_t idle = idleMs(now);
    if (idle >= kOffAfterMs) level_ = DisplayLevel::Off;
    else if (idle >= kDimAfterMs) level_ = DisplayLevel::Dim;
    return level_;
  }

  // Call once per debounced hardware press, before dispatching its game action.
  // true: this press woke an Off display and must be consumed as wake-only.
  // false: the display was visible, so the normal button action may proceed.
  bool hardwarePress(uint32_t now) {
    const bool consumed = level_ == DisplayLevel::Off;
    begin(now);
    return consumed;
  }

  // A PMIC-classified short PWR tap toggles only the panel. Long presses are
  // handled entirely by the PMIC and never call this method. Return wake-only.
  bool powerTap(uint32_t now) {
    if (level_ == DisplayLevel::Off) { begin(now); return true; }
    screenOffNow();
    return false;
  }

  // Accepted touch/shake interaction can brighten/reset only a visible display.
  // false means Off: do not dispatch the hidden input to the game. Off remains
  // latched and its idle origin is untouched. Call only for accepted gestures,
  // so ignored shakes while the pet naps do not prolong the display timeout.
  // The caller may also call this while a hardware button remains physically
  // held after its initial press, preventing timeout without repeated actions.
  bool visibleInteraction(uint32_t now) {
    if (level_ == DisplayLevel::Off) return false;
    begin(now);
    return true;
  }

 private:
  DisplayLevel level_;
  uint32_t lastInteraction_;
  bool started_;
};

}  // namespace sloth

#endif  // SLOTH_PET_DISPLAY_IDLE_H
