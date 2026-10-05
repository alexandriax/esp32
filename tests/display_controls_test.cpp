#include "../firmware/sloth_pet/display_controls.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
  using sloth::SideButtonAction;
  // Menu buttons align with the left/center/right hint; a chord never both
  // changes focus and activates a destination. Game/volume order remains
  // independent of the shared pet-home/menu order.
  for (bool menu : {false, true}) {
    assert(sloth::sideButtonAction(false, false, menu) == SideButtonAction::None);
    assert(sloth::sideButtonAction(true, true, menu) == SideButtonAction::None);
    assert(sloth::sideButtonAction(false, true, menu) ==
           (menu ? SideButtonAction::Next : SideButtonAction::Select));
    assert(sloth::sideButtonAction(true, false, menu) ==
           (menu ? SideButtonAction::Select : SideButtonAction::Next));
  }
  // Both players can reverse at the same time during a rally.
  assert(sloth::sideButtonAction(true, true, false, true) == SideButtonAction::Both);
  assert(sloth::sideButtonAction(false, false, false, true) == SideButtonAction::None);
  assert(sloth::sideButtonAction(false, true, false, true) == SideButtonAction::Select);
  assert(sloth::sideButtonAction(true, false, false, true) == SideButtonAction::Next);
  using sloth::DisplayLevel;
  sloth::DisplayIdle idle;
  // A display session always returns to visible Settings, including a dimmed
  // waiting screen and a millis rollover. It never performs a panel-off toggle.
  idle.begin(10);
  assert(idle.update(60010) == DisplayLevel::Dim);
  assert(sloth::displayPowerTap(idle, true, 60020));
  assert(idle.level() == DisplayLevel::Bright && idle.idleMs(60020) == 0);
  idle.screenOffNow();
  assert(sloth::displayPowerTap(idle, true, UINT32_MAX - 1));
  assert(idle.update(8) == DisplayLevel::Bright && idle.idleMs(8) == 10);
  // After returning, another short tap retains the normal pet/settings toggle.
  assert(!sloth::displayPowerTap(idle, false, 20));
  assert(idle.level() == DisplayLevel::Off);
  assert(!sloth::displayPowerTap(idle, false, 30));
  assert(idle.level() == DisplayLevel::Bright);
  for (unsigned value = 0; value <= 60; ++value) {
    const auto up = sloth::steppedDisplayVolume(value, true);
    const auto down = sloth::steppedDisplayVolume(value, false);
    assert(up >= value && up <= 60 && up - value <= 5);
    assert(down <= value && value - down <= 5);
    if (value >= 5 && value <= 55) {
      assert(sloth::steppedDisplayVolume(up, false) == value);
      assert(sloth::steppedDisplayVolume(down, true) == value);
    }
  }
  uint8_t volume = 35;
  for (unsigned i = 0; i < 1000; ++i) volume = sloth::steppedDisplayVolume(volume, true);
  assert(volume == 60);
  for (unsigned i = 0; i < 1000; ++i) volume = sloth::steppedDisplayVolume(volume, false);
  assert(volume == 0);
  assert(sloth::steppedDisplayVolume(255, true) == 60);
  assert(sloth::steppedDisplayVolume(255, false) == 55);
  std::puts("Controls: menu side-button order, display exit, power toggle, rollover and bounded volume passed");
}
