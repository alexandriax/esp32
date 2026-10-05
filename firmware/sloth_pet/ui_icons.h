#pragma once

#include "pet_canvas.h"

namespace sloth {

// Original multicolor pixel glyphs. Each has a 12 x 12 drawing inside a 16 x 16
// slot, leaving two pixels of breathing room. Only foreground pixels are drawn.
enum class UiIcon : uint8_t {
  None, Games, Pong, Tetris, LeafSweep, Remote, Settings, Back, Play, Music,
  Effects, Trophy, Sloth, Cat, Frog, Name, Clock, Subtitle, Jungle, Meadow,
  Night, NYC, Space, Island, Sea, Shake, Save, Cancel, Wifi, USB, Lock,
  Delete, Spacebar, Next, Clear, Check, Continue, Speed, Minus, Plus, Keyboard,
  Food, Sleep, Wake, ForestFidget, Storage, SunConure, Utilities, Bluetooth, Count
};

namespace ui_icon_detail {
struct Palette {
  uint16_t blue, purple, teal, green, lime, gold, orange, coral, pink, cream, brown;
};

inline Palette palette(uint16_t foregroundHint) {
  using graphics::rgb;
  // Existing callers use a dark foreground on mint selection fills and a light
  // foreground on dark cards. Keep the same API while adapting both palettes.
  const unsigned red = ((foregroundHint >> 11) & 31) * 8;
  const unsigned green = ((foregroundHint >> 5) & 63) * 4;
  const unsigned blue = (foregroundHint & 31) * 8;
  const bool lightBackground = red * 3 + green * 6 + blue < 720;
  if (lightBackground) return Palette{
      rgb(21,68,121), rgb(77,43,117), rgb(0,84,84), rgb(39,85,32),
      rgb(74,99,16), rgb(119,76,8), rgb(136,58,18), rgb(127,37,63),
      rgb(128,39,82), rgb(51,47,58), rgb(93,54,24)};
  return Palette{
      rgb(105,177,245), rgb(185,153,242), rgb(76,201,184), rgb(132,207,99),
      rgb(213,230,124), rgb(250,200,92), rgb(238,146,77), rgb(242,129,133),
      rgb(242,170,200), rgb(255,238,207), rgb(178,126,82)};
}

// Color regions follow each glyph's actual parts, independent of its placement.
// The silhouette remains the original mask: no background or bounding box fill.
inline uint16_t partColor(UiIcon icon, int x, int y, const Palette& p) {
  switch (icon) {
    case UiIcon::Games: // Lavender shell, pale D-pad, coral face buttons.
      if (y >= 4 && y <= 7 && x >= 3 && x <= 6) return p.cream;
      if (y >= 4 && y <= 7 && x >= 8 && x <= 10) return p.coral;
      return p.purple;
    case UiIcon::Pong: return x < 2 ? p.teal : x > 9 ? p.coral : p.gold;
    case UiIcon::Tetris: return y < 4 ? p.purple : x < 5 ? p.blue : p.gold;
    case UiIcon::LeafSweep: // Leaf blade, light central vein, woody stem.
      if (y >= 8) return p.brown;
      return x + y >= 10 && x + y <= 12 ? p.lime : p.green;
    case UiIcon::Remote:
      if (y >= 8) return p.purple;
      return y == 4 && x > 1 && x < 10 ? p.cream : p.blue;
    case UiIcon::Settings:
      if (x >= 3 && x <= 8 && y >= 3 && y <= 8) return p.gold;
      return x <= 1 || x >= 10 || y <= 1 || y >= 10 ? p.purple : p.blue;
    case UiIcon::Back: return x <= 4 ? p.purple : p.blue;
    case UiIcon::Play: return x <= 2 ? p.teal : p.lime;
    case UiIcon::Music:
      if (y >= 8) return p.coral;
      return y <= 3 ? p.purple : p.blue;
    case UiIcon::Effects: return x <= 7 ? p.teal : p.gold;
    case UiIcon::Trophy:
      if (y >= 8) return p.brown;
      return x <= 1 || x >= 10 ? p.orange : p.gold;
    case UiIcon::Sloth:
      if (x >= 3 && x <= 8 && y >= 4 && y <= 6) return p.cream;
      return y >= 7 && y <= 9 && x >= 4 && x <= 7 ? p.gold : p.brown;
    case UiIcon::Cat:
      if (y == 5 && x >= 3 && x <= 8) return p.teal;
      return y <= 2 || (y == 7 && x >= 5 && x <= 6) ? p.pink : p.gold;
    case UiIcon::Frog:
      if (y >= 1 && y <= 3 && x >= 2 && x <= 9) return p.cream;
      return y == 4 || y == 8 ? p.teal : p.green;
    case UiIcon::Name:
      if (y == 10) return p.blue; // The underline is separate from the pencil.
      return y <= 2 ? p.pink : y >= 8 ? p.brown : p.gold;
    case UiIcon::Clock: return x >= 4 && x <= 8 && y >= 3 && y <= 6 ? p.cream : p.blue;
    case UiIcon::Subtitle:
      if (y >= 9) return p.purple;
      return (y == 4 || y == 6) && x > 0 && x < 11 ? p.cream : p.blue;
    case UiIcon::Jungle:
      return y >= 8 ? p.brown : y <= 3 ? p.lime : p.green;
    case UiIcon::Meadow:
      if (y >= 6) return p.green;
      return x >= 4 && x <= 6 && y >= 2 && y <= 4 ? p.gold : p.pink;
    case UiIcon::Night: return x <= 2 ? p.cream : p.purple;
    case UiIcon::NYC:
      if (y <= 2) return p.gold;
      return x >= 4 && x <= 7 ? p.purple : p.blue;
    case UiIcon::Space:
      if (y >= 9) return p.gold;
      return x <= 2 || (x >= 8 && y >= 4) ? p.coral : p.purple;
    case UiIcon::Island: return y <= 3 ? p.green : y >= 9 ? p.gold : p.brown;
    case UiIcon::Sea:
      if (y >= 10) return p.teal;
      return y == 5 && x >= 2 && x <= 3 ? p.cream : p.blue;
    case UiIcon::Shake: return x <= 1 || x >= 10 ? p.gold : p.purple;
    case UiIcon::Save: return x >= 3 && x <= 8 && y >= 2 && y <= 9 ? p.cream : p.blue;
    case UiIcon::Cancel: return x > y ? p.purple : p.coral;
    case UiIcon::Wifi: return y <= 3 ? p.blue : y >= 10 ? p.gold : p.teal;
    case UiIcon::USB:
      return y <= 1 ? p.gold : x >= 5 && x <= 6 ? p.purple : p.blue;
    case UiIcon::Lock:
      if (y <= 4) return p.blue;
      return x >= 5 && x <= 6 && y >= 7 && y <= 8 ? p.brown : p.gold;
    case UiIcon::Delete: return y >= 4 && y <= 7 && x >= 4 && x <= 8 ? p.purple : p.coral;
    case UiIcon::Spacebar: return y == 9 ? p.purple : p.blue;
    case UiIcon::Next: return x >= 7 ? p.purple : p.blue;
    case UiIcon::Clear: return y <= 3 ? p.purple : p.blue;
    case UiIcon::Check: return x <= 3 ? p.gold : p.green;
    case UiIcon::Continue: return x >= 7 ? p.purple : p.blue;
    case UiIcon::Speed: return x >= 2 && x <= 9 && y >= 4 && y <= 8 ? p.coral : p.blue;
    case UiIcon::Minus: return y == 5 ? p.blue : p.purple; // Lit bevel and lower edge.
    case UiIcon::Plus: return x >= 5 && x <= 6 ? p.blue : p.purple;
    case UiIcon::Keyboard:
      if (y == 8 && x >= 3 && x <= 8) return p.teal;
      return (y == 4 || y == 6) && x > 0 && x < 11 ? p.cream : p.purple;
    case UiIcon::Food:
      if (y >= 9) return p.brown;
      return x + y >= 9 && x + y <= 11 ? p.lime : p.green;
    case UiIcon::Sleep: return y <= 5 ? p.purple : p.blue;
    case UiIcon::Wake: return y >= 4 && y <= 7 ? p.gold : p.orange;
    case UiIcon::ForestFidget: // Fern stem/blades beside a blue-and-gold spinner.
      if (x <= 3) return x == 3 ? p.brown : p.green;
      return y <= 3 ? p.blue : p.gold;
    case UiIcon::Storage: // Gold contacts, blue card shell, mint capacity stripes.
      if (y <= 3 && x >= 4) return p.gold;
      return y >= 7 && x >= 3 && x <= 8 ? p.teal : p.blue;
    case UiIcon::SunConure:
      if (y < 5) return x >= 8 ? p.blue : p.orange;
      return x < 5 ? p.gold : y >= 9 ? p.blue : p.green;
    case UiIcon::Utilities: return y <= 3 ? p.gold : x < 6 ? p.blue : p.teal;
    case UiIcon::Bluetooth: return x >= 6 ? p.blue : p.purple;
    case UiIcon::None: case UiIcon::Count: return p.blue;
  }
  return p.blue;
}
}  // namespace ui_icon_detail

// The final argument remains a foreground contrast hint, not a solid tint.
inline void drawUiIcon(graphics::Canvas& c, int x, int y, UiIcon icon, uint16_t color) {
  static const uint16_t rows[][12] = {
    {}, // None
    {0x000,0x3fc,0x7fe,0x603,0x693,0x7e3,0x693,0x60b,0x603,0x606,0x30c,0x000}, // Games
    {0x000,0xc03,0xc03,0xc03,0xc33,0xc33,0xc03,0xc03,0xc03,0xc03,0x000,0x000}, // Pong
    {0x000,0x1e0,0x120,0x120,0x7f8,0x498,0x498,0x7f8,0x000,0x000,0x000,0x000}, // Tetris
    {0x000,0x036,0x06a,0x0dc,0x1b8,0x330,0x260,0x4c0,0x980,0x900,0x600,0x000}, // LeafSweep
    {0x000,0xfff,0x801,0x801,0x999,0x801,0x801,0xfff,0x060,0x060,0x1f8,0x000}, // Remote
    {0x060,0x366,0x7fe,0x6f6,0xd9b,0xd0b,0xd0b,0xd9b,0x6f6,0x7fe,0x366,0x060}, // Settings
    {0x000,0x000,0x080,0x180,0x300,0x7fe,0x7fe,0x300,0x180,0x080,0x000,0x000}, // Back
    {0x000,0x600,0x700,0x780,0x7c0,0x7e0,0x7e0,0x7c0,0x780,0x700,0x600,0x000}, // Play
    {0x000,0x0fe,0x0c6,0x0fe,0x0c6,0x0c6,0x0c6,0x0c6,0x3ce,0x7de,0x798,0x300}, // Music
    {0x000,0x080,0x184,0x388,0x79a,0x79a,0x79a,0x79a,0x388,0x184,0x080,0x000}, // Effects
    {0x000,0x3fc,0xfff,0xc03,0xc03,0x606,0x30c,0x1f8,0x060,0x060,0x1f8,0x000}, // Trophy
    {0x000,0x3fc,0x606,0xc03,0xd9b,0xf9f,0xe97,0xc63,0xc03,0x666,0x3fc,0x000}, // Sloth
    {0xc03,0xe07,0xf0f,0xfff,0x801,0x999,0x801,0x060,0x966,0x606,0x3fc,0x000}, // Cat
    {0x000,0x396,0x7df,0x555,0x7df,0xc03,0xc03,0xc03,0x666,0x63c,0x3fc,0x000}, // Frog
    {0x000,0x00c,0x01e,0x036,0x06c,0x0d8,0x1b0,0x360,0x6c0,0x780,0xffe,0x000}, // Name
    {0x000,0x1f8,0x30c,0x666,0xc63,0xc63,0xc7b,0xc03,0x606,0x30c,0x1f8,0x000}, // Clock
    {0x000,0xfff,0x801,0x801,0xddd,0x801,0xbbb,0x801,0xfff,0x180,0x100,0x000}, // Subtitle
    {0x000,0x060,0x0f0,0x1f8,0x3fc,0x1f8,0x3fc,0x7fe,0x060,0x060,0x1f8,0x000}, // Jungle
    {0x000,0x0c0,0x1e0,0x3f0,0x1e0,0x0c0,0x0c0,0x4d8,0x6f0,0x3c0,0x0c0,0x000}, // Meadow
    {0x078,0x0e0,0x1c0,0x380,0x700,0x700,0x702,0x706,0x38e,0x1fc,0x0f8,0x000}, // Night
    {0x020,0x070,0x272,0x7f7,0x555,0x777,0x555,0x777,0x555,0x777,0xfff,0x000}, // NYC
    {0x018,0x038,0x07c,0x0fe,0x1fa,0x3f2,0x7e0,0x7c0,0x4c0,0x600,0xa00,0x000}, // Space
    {0x000,0x078,0x3fe,0x76e,0x0e0,0x160,0x220,0x020,0x020,0x1fc,0x7ff,0x000}, // Island
    {0x000,0x000,0x000,0x3c4,0x7ee,0xfff,0xdfb,0x7ee,0x3c4,0x000,0x666,0x999}, // Sea
    {0x000,0x5fa,0x902,0x9f9,0x50a,0x108,0x108,0x50a,0x9f9,0x902,0x5fa,0x000}, // Shake
    {0x000,0xffc,0x90e,0x912,0x9f2,0x802,0x9fa,0x906,0x906,0x9fa,0xffe,0x000}, // Save
    {0x000,0x603,0x306,0x18c,0x0d8,0x070,0x070,0x0d8,0x18c,0x306,0x603,0x000}, // Cancel
    {0x000,0x1f8,0x706,0xc03,0x0f0,0x318,0x606,0x060,0x090,0x000,0x060,0x060}, // Wifi
    {0x060,0x0f0,0x060,0x064,0x266,0x264,0x364,0x168,0x070,0x060,0x0f0,0x060}, // USB
    {0x000,0x0f0,0x198,0x108,0x108,0x7fe,0x603,0x663,0x663,0x603,0x7fe,0x000}, // Lock
    {0x000,0x000,0x3ff,0x601,0xc99,0x871,0x871,0xc99,0x601,0x3ff,0x000,0x000}, // Delete
    {0x000,0x000,0x000,0x000,0x000,0x000,0x801,0x801,0x801,0xfff,0x000,0x000}, // Spacebar
    {0x000,0x000,0x010,0x018,0x00c,0x7fe,0x7fe,0x00c,0x018,0x010,0x000,0x000}, // Next
    {0x000,0x060,0x1f8,0x7fe,0x000,0x30c,0x36c,0x36c,0x36c,0x30c,0x1f8,0x000}, // Clear
    {0x000,0x000,0x003,0x007,0x00e,0x61c,0x738,0x3f0,0x1e0,0x0c0,0x000,0x000}, // Check
    {0x000,0x618,0x718,0x798,0x7d8,0x7f8,0x7f8,0x7d8,0x798,0x718,0x618,0x000}, // Continue
    {0x000,0x1f8,0x306,0x603,0xc09,0xc19,0xc31,0xc61,0xcc1,0x801,0xfff,0x000}, // Speed
    {0x000,0x000,0x000,0x000,0x000,0x7fe,0x7fe,0x000,0x000,0x000,0x000,0x000}, // Minus
    {0x000,0x060,0x060,0x060,0x060,0x7fe,0x7fe,0x060,0x060,0x060,0x060,0x000}, // Plus
    {0x000,0x000,0xfff,0x801,0xdad,0x801,0xdb5,0x801,0x9f9,0x801,0xfff,0x000}, // Keyboard
    {0x000,0x038,0x068,0x0d8,0x1b0,0x360,0x6c0,0x580,0x700,0x600,0xc00,0x000}, // Food
    {0x000,0x7c0,0x0c0,0x180,0x300,0x7c0,0x03e,0x006,0x00c,0x018,0x03e,0x000}, // Sleep
    {0x060,0x462,0x264,0x000,0x1f8,0x3fc,0x3fc,0x1f8,0x000,0x264,0x462,0x060}, // Wake
    {0x000,0x10c,0x712,0x321,0xf21,0x112,0x70c,0x300,0xf00,0x100,0x180,0x000}, // Forest Fidget
    {0x000,0x3fe,0x602,0xcda,0x802,0x802,0x802,0x9fa,0x802,0x9fa,0xffe,0x000}, // Storage
    {0x070,0x0f8,0x1dc,0x1fa,0x0fc,0x1f8,0x3fc,0x3fc,0x3f8,0x1f0,0x0e0,0x0c0}, // Sun conure
    {0x000,0x0f0,0x198,0x108,0xfff,0x801,0x999,0xbfd,0x999,0x801,0xfff,0x000}, // Utilities
    {0x060,0x070,0x078,0x26c,0x168,0x0f0,0x0f0,0x168,0x26c,0x078,0x070,0x060}, // Bluetooth
  };
  static_assert(sizeof(rows) / sizeof(rows[0]) == static_cast<unsigned>(UiIcon::Count),
                "Each UI icon needs one bounded glyph");
  const unsigned index = static_cast<unsigned>(icon);
  if (!c.pixels || index >= static_cast<unsigned>(UiIcon::Count)) return;
  const auto palette = ui_icon_detail::palette(color);
  for (int row = 0; row < 12; ++row)
    for (int col = 0; col < 12; ++col)
      if (rows[index][row] & (1u << (11 - col)))
        c.dot(x + col + 2, y + row + 2, ui_icon_detail::partColor(icon, col, row, palette));
}

}  // namespace sloth
