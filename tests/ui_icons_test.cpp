#include "../firmware/sloth_pet/ui_icons.h"
#include <assert.h>
#include <cmath>
#include <stdio.h>
#include <string.h>

namespace {
double linear(double channel) {
  return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}
double luminance(uint16_t color) {
  return 0.2126 * linear(((color >> 11) & 31) / 31.0) +
         0.7152 * linear(((color >> 5) & 63) / 63.0) +
         0.0722 * linear((color & 31) / 31.0);
}
double contrast(uint16_t first, uint16_t second) {
  const double a = luminance(first), b = luminance(second);
  return a > b ? (a + 0.05) / (b + 0.05) : (b + 0.05) / (a + 0.05);
}
}

int main() {
  using namespace sloth;
  constexpr unsigned size = 240 * 240;
  uint16_t guarded[size + 2];
  guarded[0] = 0xa55a; guarded[size + 1] = 0x5aa5;
  graphics::Canvas c{guarded + 1};
  const uint16_t hints[] = {graphics::mint, graphics::cream, graphics::gold, graphics::muted,
      graphics::rgb(84,108,95), graphics::ink, graphics::rgb(30,40,55)};
  for (unsigned index = 0; index < static_cast<unsigned>(UiIcon::Count); ++index) {
    const auto icon = static_cast<UiIcon>(index);
    bool silhouette[16 * 16] = {};
    uint16_t normalColors[4] = {};
    for (unsigned variant = 0; variant < sizeof(hints) / sizeof(hints[0]); ++variant) {
      memset(c.pixels, 0, size * sizeof(uint16_t));
      drawUiIcon(c, 37, 53, icon, hints[variant]);
      uint16_t colors[4] = {}; unsigned colorCount = 0, foreground = 0;
      const uint16_t background = variant < 5 ? graphics::rgb(29,53,47) : graphics::mint;
      for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x) {
        const uint16_t pixel = c.pixels[y * 240 + x];
        if (pixel) {
          assert(x >= 37 && x < 53 && y >= 53 && y < 69);
          ++foreground;
          unsigned found = 0;
          while (found < colorCount && colors[found] != pixel) ++found;
          if (found == colorCount) {
            assert(colorCount < 4);
            colors[colorCount++] = pixel;
            assert(contrast(pixel, background) >= 3.0);
          }
        }
        if (x >= 37 && x < 53 && y >= 53 && y < 69) {
          const unsigned offset = (y - 53) * 16 + x - 37;
          if (!variant) silhouette[offset] = pixel != 0;
          else assert(silhouette[offset] == (pixel != 0));
        }
      }
      if (index && colorCount < 2) fprintf(stderr, "Icon %u has only %u colors\n", index, colorCount);
      assert(index == 0 ? foreground == 0 && colorCount == 0 :
          foreground >= 12 && foreground <= 144 && colorCount >= 2 && colorCount <= 4);
      if (!variant) memcpy(normalColors, colors, sizeof(colors));
      else if (variant >= 5 && index) assert(memcmp(normalColors, colors, sizeof(colors)) != 0);

      // An icon may change foreground pixels only, even on a colored surface.
      for (unsigned i = 0; i < size; ++i) c.pixels[i] = background;
      drawUiIcon(c, 37, 53, icon, hints[variant]);
      for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x) {
        const bool on = x >= 37 && x < 53 && y >= 53 && y < 69 &&
            silhouette[(y - 53) * 16 + x - 37];
        assert((c.pixels[y * 240 + x] != background) == on);
      }
    }
    // All screen-edge clipping positions are safe, including fully offscreen.
    const int positions[] = {-20, -15, -1, 0, 225, 239, 240};
    for (int y : positions) for (int x : positions) drawUiIcon(c, x, y, icon, graphics::mint);
    assert(guarded[0] == 0xa55a && guarded[size + 1] == 0x5aa5);
  }
  memset(c.pixels, 0, size * sizeof(uint16_t));
  drawUiIcon(c, 10, 10, UiIcon::Count, 0xffff);
  drawUiIcon(c, 10, 10, static_cast<UiIcon>(255), 0xffff);
  for (unsigned i = 0; i < size; ++i) assert(c.pixels[i] == 0);
  graphics::Canvas missing{nullptr};
  drawUiIcon(missing, 0, 0, UiIcon::Games, 0xffff);
  puts("ui_icons: every glyph has 2-4 colors with 3:1 contrast on dark/selected cards, unchanged silhouettes, transparent backgrounds and safe clipping");
}
