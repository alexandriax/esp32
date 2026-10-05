#pragma once

#include "pet_canvas.h"

namespace sloth {

// Compact hardware legend inside the rounded screen's safe area. The three
// sections sit below front-left BOOT, center PWR, and front-right KEY.
inline void drawNavigationBar(graphics::Canvas& c) {
  using namespace graphics;
  c.roundRect(20, 8, 200, 16, 3, mint);
  c.rect(86, 10, 1, 12, ink);
  c.rect(154, 10, 1, 12, ink);
  c.text(41, 12, "NEXT", ink);
  c.text(108, 12, "BACK", ink);
  c.text(169, 12, "SELECT", ink);
}

}  // namespace sloth
