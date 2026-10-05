#include "../firmware/sloth_pet/forest_fidget_controls.h"
#include <cassert>
#include <climits>
#include <cstdio>

int main() {
  using A = sloth::ForestFidgetAction;
  using C = sloth::ForestFidgetControls;
  C nav;
  // The entry gate consumes a carried contact and exactly one release.
  assert(nav.touch(true, 190, 16) == A::None);
  assert(nav.touch(false, 0, 0) == A::None);
  const int centers[] = {48, 120, 190};
  const A actions[] = {A::Previous, A::Back, A::Next};
  for (unsigned i = 0; i < 3; ++i) {
    for (unsigned tap = 0; tap < 5; ++tap) {
      assert(nav.touch(true, centers[i], 16) == A::None);
      // Repeated held reports never auto-repeat, nor postpone the release.
      for (unsigned sample = 0; sample < 100; ++sample)
        assert(nav.touch(true, centers[i], 16) == A::None);
      assert(nav.touch(false, 0, 0) == actions[i]);
      assert(nav.touch(false, 0, 0) == A::None);
    }
  }
  for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x) {
    const A expected = x < 20 || x >= 220 || y < 8 || y >= 24 ? A::None :
        x < 86 ? A::Previous : x < 154 ? A::Back : A::Next;
    assert(C::hitTest(x, y) == expected);
  }
  // Never turn a field stroke, cross-button swipe or out-and-back drag into a tap.
  nav.touch(true, 120, 140); nav.touch(true, 190, 16);
  assert(nav.touch(false, 190, 16) == A::None);
  nav.touch(true, 48, 16); nav.touch(true, 190, 16); nav.touch(true, 48, 16);
  assert(nav.touch(false, 48, 16) == A::None);
  nav.touch(true, 190, 16); nav.touch(true, 202, 16); nav.touch(true, 190, 16);
  assert(nav.touch(false, 0, 0) == A::None);
  nav.touch(true, 190, 16); nav.touch(true, 198, 20);
  assert(nav.touch(false, 0, 0) == A::Next); // Modest drift stays responsive.
  nav.touch(true, 190, 16); nav.touch(true, 190, 24);
  assert(nav.touch(false, 0, 0) == A::None); // Leaving rail cancels even within drift limit.
  nav.touch(true, 48, 16); nav.reset();
  assert(nav.touch(true, 48, 16) == A::None);
  assert(nav.touch(false, 0, 0) == A::None);
  nav.touch(true, 48, 16);
  assert(nav.touch(false, 0, 0) == A::Previous); // No extra release after reset/wake.
  nav.touch(true, INT_MIN, INT_MAX); nav.touch(true, 190, 16);
  assert(nav.touch(false, 0, 0) == A::None);
  nav.touch(true, 190, 16); nav.touch(true, INT_MAX, INT_MIN);
  assert(nav.touch(false, 0, 0) == A::None);
  assert(C::hitTest(INT_MIN, INT_MAX) == A::None);
  puts("Forest controls: first-release taps, no repeats, rail geometry, drift, drag rejection, one-release reset/wake and invalid coordinates passed");
}
