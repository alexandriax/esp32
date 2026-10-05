#pragma once

#include <stdint.h>
#include "pet_state.h"
#include "pet_settings.h"

namespace sloth {

// Touch coordinates use the same logical 240 x 240 space as the renderer.
// One control row: Feed/Play/Nap at x=14/76/138, width 56; menu at x=200,
// width 28. All are y=198, height 23. Touch targets add 3 px horizontally and
// 8 vertically: x=[11,73)/[73,135)/[135,197)/[197,231), y=[190,229).
// Adjacent targets meet at their edges and never overlap.
// Returns Feed=0, Play=1, Nap/Wake=2, Menu=3, or -1 outside the targets.
int hitTestAction(int x, int y);

// Generous touch padding follows the same roaming body and gesture pose as the
// renderer, including conure tail/wing tips. Speech bubble hit testing is separate.
// Excludes the status, meters and care controls. No state changes or allocation.
bool hitTestPet(int x, int y, const Snapshot& state, uint32_t animationMs,
                const Settings& settings, int actionAnimation = 0,
                int reaction = -1, uint32_t reactionMs = 0);

// Hardware-independent display data. The caller formats local time;
// absent or unavailable readings have explicit placeholders, never guessed data.
struct Hud {
  const char* timeText = "--:--";  // Up to 8 characters, e.g. "12:59 PM".
  const char* zoneText = "ET";    // Compatibility field; zone is not displayed.
  int batteryPercent = -1;       // Negative means unknown; values clamp to 100.
  bool batteryPresent = false;
  bool charging = false;         // Compatibility field; power icon uses VBUS.
  bool externalPower = false;    // Draw a lightning bolt inside the battery.
};

// Render a complete 240 x 240 RGB565 frame. The board adapter doubles each
// pixel to fit the native 480 x 480 display. No allocation or hardware calls.
// actionAnimation: 0=idle, 1=feed, 2=play, 3=shake-triggered leaf dance.
// reaction: -1=none, 0..11=shrug/wave/facepalm/tilt/nod/stretch/peek/giggle/
// sway/point/heart hands/clap. reactionMs is elapsed time; poses settle at 4s.
// Reactions preserve full-size geometry and never wake a sleeping pet.
// Idle scenes use a display-only, species-paced observe/travel/groom/doze cycle;
// ambient animation never updates persistent settings or pet-care state.
void drawPet(uint16_t* pixels, const Snapshot& state, unsigned selectedAction,
             const char* statusMessage, uint32_t animationMs,
             int actionAnimation = 0, const Hud& hud = Hud(),
             const Settings& settings = Settings(), const char* speech = nullptr,
             int reaction = -1, uint32_t reactionMs = 0);

}  // namespace sloth
