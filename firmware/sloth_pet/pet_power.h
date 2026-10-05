#pragma once

namespace pet_power {

struct Status {
  bool available = false;
  // True only with battery detection enabled and a positive presence result.
  // False also covers disabled detection, which cannot prove battery absence.
  bool batteryPresent = false;
  bool externalPower = false;  // PMIC reports usable VBUS input.
  bool charging = false;       // Actual charging current direction, not charger enable.
  int percent = -1;            // PMIC fuel-gauge value 0..100; -1 means unknown.
};

// Read-only AXP2101 status using Wire after board::begin(). No charging, gauge,
// rail or detection configuration is changed. Calls have bounded 10ms transfers
// and restore the shared bus timeout. A failed percentage read retains known
// power-source status but returns percent=-1; failed basic status is unavailable.
Status read();

// Pressed means a completed PMIC-classified short tap, not initial button-down.
enum class ButtonEvent { Unavailable, None, Pressed };

// Enable PWR short-press reporting after board::begin(). Preserves all other
// IRQ enables and clears only the stale short-press flag. Does not change
// charging, power rails, shutdown enable, or hardware long-hold timing.
bool beginButton();

// Poll the latched short tap (no ESP interrupt pin required). Falling/rising
// edges and long presses produce no event, so a hold cannot toggle the display
// before hardware shutdown. Read failures return Unavailable. A short tap is
// delivered even if its W1C acknowledgement fails; polls suppress that same latch
// until it clears. Multiple taps between polls may coalesce in the PMIC latch.
ButtonEvent readButton();

}  // namespace pet_power
