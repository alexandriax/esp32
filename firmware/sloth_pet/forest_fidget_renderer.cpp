#include "forest_fidget.h"
#include "pet_canvas.h"

#include <cmath>
#include <stdio.h>

namespace sloth {
namespace {
using namespace graphics;
const uint16_t water = rgb(18, 66, 74), waterLight = rgb(85, 164, 170);
const uint16_t moss = rgb(87, 156, 77), leafLight = rgb(186, 222, 119);
const uint16_t bark = rgb(116, 78, 51), amber = rgb(222, 160, 81);
const uint16_t lilac = rgb(174, 156, 220), coral = rgb(232, 133, 112);

float clamp(float n, float lo, float hi) { return n < lo ? lo : n > hi ? hi : n; }
int rounded(float n) { return static_cast<int>(std::lround(n)); }
uint16_t mix(uint16_t a, uint16_t b, float amount) {
  const unsigned t = static_cast<unsigned>(clamp(amount, 0, 1) * 256);
  const unsigned r = (((a >> 11) & 31) * (256 - t) + ((b >> 11) & 31) * t) >> 8;
  const unsigned g = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
  const unsigned blue = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;
  return static_cast<uint16_t>((r << 11) | (g << 5) | blue);
}
struct Point { int x, y; };
struct Rotation {
  float cs, sn;
  explicit Rotation(float angle) : cs(std::cos(angle)), sn(std::sin(angle)) {}
};
Point rotated(float x, float y, float dx, float dy, const Rotation& turn) {
  return Point{rounded(x + dx * turn.cs - dy * turn.sn), rounded(y + dx * turn.sn + dy * turn.cs)};
}
// Q14 unit-circle samples preserve the original 48-segment outline within one
// pixel, without evaluating sine and cosine again for every ring, every frame.
const int16_t kCircle[48][2] = {
  {16384,0},  {16244,2139},  {15826,4240},  {15137,6270},
  {14189,8192},  {12998,9974},  {11585,11585},  {9974,12998},
  {8192,14189},  {6270,15137},  {4240,15826},  {2139,16244},
  {0,16384},  {-2139,16244},  {-4240,15826},  {-6270,15137},
  {-8192,14189},  {-9974,12998},  {-11585,11585},  {-12998,9974},
  {-14189,8192},  {-15137,6270},  {-15826,4240},  {-16244,2139},
  {-16384,0},  {-16244,-2139},  {-15826,-4240},  {-15137,-6270},
  {-14189,-8192},  {-12998,-9974},  {-11585,-11585},  {-9974,-12998},
  {-8192,-14189},  {-6270,-15137},  {-4240,-15826},  {-2139,-16244},
  {0,-16384},  {2139,-16244},  {4240,-15826},  {6270,-15137},
  {8192,-14189},  {9974,-12998},  {11585,-11585},  {12998,-9974},
  {14189,-8192},  {15137,-6270},  {15826,-4240},  {16244,-2139},
};
int scaledUnit(int16_t unit, int radius) {
  const int scaled = static_cast<int>(unit) * radius;
  return scaled >= 0 ? (scaled + 8192) / 16384 : -((-scaled + 8192) / 16384);
}
void stroke(Canvas& c, Point a, Point b, uint16_t color, int radius = 0) {
  c.line(a.x, a.y, b.x, b.y, color, radius);
}
void ovalOutline(Canvas& c, int x, int y, int rx, int ry, uint16_t color) {
  if (rx < 1 || ry < 1) return;
  Point previous{x + rx, y};
  for (unsigned i = 1; i <= 48; ++i) {
    const Point next{x + scaledUnit(kCircle[i % 48][0], rx), y + scaledUnit(kCircle[i % 48][1], ry)};
    stroke(c, previous, next, color); previous = next;
  }
}
void rotatedOval(Canvas& c, float x, float y, float rx, float ry, const Rotation& turn, uint16_t color) {
  if (rx < 1 || ry < 1) return;
  const int extent = rounded(rx > ry ? rx : ry) + 1;
  const float inverseX = 1.0f / (rx * rx), inverseY = 1.0f / (ry * ry);
  const float cs2 = turn.cs * turn.cs, sn2 = turn.sn * turn.sn;
  const float a = cs2 * inverseX + sn2 * inverseY;
  const float b = 2 * turn.cs * turn.sn * (inverseX - inverseY);
  const float cc = sn2 * inverseX + cs2 * inverseY;
  const float inverseA = 1.0f / a;
  const float centerSlope = -0.5f * b * inverseA;
  const float rowCurve = cc - 0.25f * b * b * inverseA;
  const int centerX = rounded(x), centerY = rounded(y);
  // Solve the rotated ellipse's conic once per scanline. Span writes are all
  // integer work; the C6 no longer performs software floats for every pixel.
  for (int yy = -extent; yy <= extent; ++yy) {
    const float halfSquared = (1.0f - rowCurve * yy * yy) * inverseA;
    if (halfSquared < 0) continue;
    const float middle = centerSlope * yy, half = std::sqrt(halfSquared);
    int left = static_cast<int>(std::ceil(middle - half));
    int right = static_cast<int>(std::floor(middle + half));
    if (left < -extent) left = -extent;
    if (right > extent) right = extent;
    if (left <= right) c.rect(centerX + left, centerY + yy, right - left + 1, 1, color);
  }
}
void diamond(Canvas& c, Point top, Point right, Point bottom, Point left, uint16_t color) {
  c.triangle(top.x, top.y, right.x, right.y, bottom.x, bottom.y, color);
  c.triangle(top.x, top.y, left.x, left.y, bottom.x, bottom.y, color);
}
void leaf(Canvas& c, float x, float y, float radius, float angle, uint16_t color, float width = 0.55f) {
  const Rotation turn(angle);
  const Point a = rotated(x, y, 0, -radius, turn), b = rotated(x, y, radius * width, 0, turn);
  const Point d = rotated(x, y, 0, radius, turn), e = rotated(x, y, -radius * width, 0, turn);
  diamond(c, a, b, d, e, color);
  stroke(c, a, d, mix(color, cream, 0.45f));
}
void glint(Canvas& c, int x, int y, uint16_t color) {
  c.line(x - 2, y, x + 2, y, color); c.line(x, y - 2, x, y + 2, color);
}
bool valid(const ForestBody& b) {
  return b.active && std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.radius) &&
      std::isfinite(b.angle) && std::isfinite(b.value) && std::isfinite(b.vx) && std::isfinite(b.vy) &&
      std::isfinite(b.spin) && b.x >= -128 && b.x <= 368 &&
      b.y >= -128 && b.y <= 368 && b.radius >= 0 && b.radius <= 80;
}
void paw(Canvas& c, int x, int y, uint16_t color) {
  c.oval(x, y + 2, 3, 3, color);
  c.oval(x - 4, y - 2, 1, 2, color); c.oval(x, y - 4, 1, 2, color); c.oval(x + 4, y - 2, 1, 2, color);
}

void background(Canvas& c, ForestToy toy, uint32_t elapsed) {
  c.rect(16, 68, 208, 148, rgb(22, 48, 42));
  if (toy == ForestToy::DewPond) {
    c.rect(16, 68, 208, 148, water);
    for (int y = 80; y < 212; y += 22)
      for (int x = 28 + (y % 3) * 11; x < 218; x += 52)
        c.line(x, y, x + 15, y, rgb(27, 78, 85));
    c.oval(40, 91, 13, 6, moss); c.triangle(40, 91, 53, 88, 51, 95, water);
    c.oval(198, 195, 16, 8, moss); c.triangle(198, 195, 214, 192, 211, 201, water);
    c.oval(194, 192, 4, 3, coral); c.dot(194, 191, cream);
    c.line(31, 89, 38, 87, leafLight); c.line(184, 194, 193, 190, leafLight);
    const int drift = static_cast<int>(elapsed / 600 % 4);
    for (int i = 0; i < 5; ++i) {
      const int x = 64 + i * 31 % 127, y = 94 + i * 27 % 102;
      c.line(x + drift, y, x + 7 + drift, y, rgb(40,107,111));
      c.dot(x + 3 + drift, y - 1, rgb(63,128,128));
    }
  } else if (toy == ForestToy::AcornRoll) {
    c.rect(16, 68, 208, 148, rgb(65, 56, 39));
    for (int i = 0; i < 36; ++i) {
      const int x = 20 + i * 37 % 198, y = 74 + i * 29 % 132;
      c.line(x, y, x + 3, y - 1, rgb(90, 80, 52));
    }
  } else if (toy == ForestToy::MushroomPop || toy == ForestToy::FernBrush || toy == ForestToy::MossSquish) {
    c.rect(16, 68, 208, 148, toy == ForestToy::MushroomPop ? rgb(30, 42, 51) : rgb(24, 51, 41));
    for (int i = 0; i < 32; ++i) {
      const int x = 22 + i * 31 % 198, y = 76 + i * 47 % 132;
      c.line(x, y + 3, x + 1, y, rgb(40, 78, 47));
    }
  } else if (toy == ForestToy::FireflyJar) {
    c.rect(16, 68, 208, 148, rgb(18, 33, 55));
    c.roundRect(24, 80, 192, 128, 17, rgb(39, 74, 88));
    c.roundRect(26, 82, 188, 124, 15, rgb(22, 43, 61));
    c.roundRect(40, 70, 160, 12, 3, bark);
    for (int x = 48; x < 196; x += 9) c.line(x, 72, x, 79, amber);
    c.line(33, 101, 33, 129, rgb(76, 119, 136));
    c.line(208, 166, 208, 184, rgb(76, 119, 136));
    c.line(46, 83, 191, 83, rgb(74,104,107));
    c.line(43, 203, 194, 203, rgb(43,72,84));
  } else if (toy == ForestToy::PineconeSpin) {
    c.rect(16, 68, 208, 148, rgb(40, 44, 47));
    ovalOutline(c, 120, 142, 72, 59, rgb(59, 71, 67));
    ovalOutline(c, 120, 142, 76, 63, rgb(48, 59, 57));
    for (int i = 0; i < 8; ++i) {
      glint(c, 120 + scaledUnit(kCircle[i * 6][0], 83), 142 + scaledUnit(kCircle[i * 6][1], 65), rgb(102, 113, 92));
    }
  } else if (toy == ForestToy::PebbleStack) {
    c.rect(16, 68, 208, 148, rgb(49, 68, 68));
    for (int y = 77; y < 213; y += 17) {
      c.line(22, y, 75, y, rgb(62, 82, 78)); c.line(146, y + 4, 217, y + 4, rgb(62, 82, 78));
    }
    c.rect(16, 209, 208, 7, rgb(101, 107, 84));
    for (int i = 0; i < 16; ++i) c.dot(25 + i * 12, 211 + i % 3, rgb(149,150,115));
  } else if (toy == ForestToy::LeafGlobe) {
    c.rect(16, 68, 208, 148, rgb(28, 49, 66));
    ovalOutline(c, 120, 141, 102, 70, rgb(121, 160, 168));
    ovalOutline(c, 120, 141, 99, 67, rgb(54, 91, 107));
    c.line(42, 95, 52, 86, rgb(139, 185, 190));
    c.roundRect(66, 207, 108, 12, 4, bark);
  } else if (toy == ForestToy::VineSwing) {
    c.rect(16, 68, 208, 148, rgb(27, 54, 49));
    c.line(29, 72, 211, 76, bark, 3);
    c.line(38, 70, 206, 74, amber);
    leaf(c, 54, 83, 8, -0.7f, moss); leaf(c, 191, 88, 10, 0.8f, moss);
    c.rect(16, 212, 208, 4, rgb(40, 72, 45));
  } else if (toy == ForestToy::Rainstick) {
    c.rect(16, 68, 208, 148, rgb(64, 62, 39));
    c.rect(16, 68, 5, 148, bark); c.rect(219, 68, 5, 148, bark);
    c.line(23, 70, 23, 212, amber); c.line(216, 70, 216, 212, amber);
    for (int y = 82; y < 211; y += 27) {
      c.line(17,y,20,y+2,furLight); c.line(220,y+1,223,y-1,furLight);
      c.line(28,y,211,y,rgb(72,70,44));
    }
    for (unsigned i = 0; i < kForestRainPegs; ++i) {
      const int x = rounded(forestRainPegX(i)), y = rounded(forestRainPegY(i));
      c.oval(x, y + 1, 4, 4, bark); c.oval(x, y, 3, 3, amber); c.dot(x - 1, y - 1, cream);
    }
  } else if (toy == ForestToy::ZenRake) {
    c.rect(16, 68, 208, 148, rgb(182, 159, 112));
    for (int y = 76; y < 216; y += 8)
      c.line(19, y, 220, y, rgb(193, 171, 124));
    for (int i = 0; i < 54; ++i) c.dot(20 + i * 37 % 200, 70 + i * 23 % 140, rgb(166, 142, 95));
    c.oval(197, 87, 12, 8, rgb(101, 117, 107)); c.line(191, 83, 202, 82, muted);
    c.oval(44, 193, 10, 7, rgb(103, 103, 87)); c.line(41, 189, 48, 189, muted);
  }
  // A very sparse glimmer gives the still scenes life without obscuring toys.
  if (toy == ForestToy::MushroomPop || toy == ForestToy::FernBrush) {
    const int x = 31 + static_cast<int>((elapsed / 400) % 6) * 34;
    glint(c, x, 78 + static_cast<int>((elapsed / 1200) % 3) * 12, rgb(111, 138, 82));
  }
}

void acorn(Canvas& c, const ForestBody& b) {
  const Rotation turn(b.angle);
  const float r = b.radius, squash = clamp(b.value, 0, 1);
  const Point nut = rotated(b.x, b.y, 0, r * 0.2f, turn);
  c.oval(rounded(b.x + 1), rounded(b.y + r * .7f), rounded(r), 3, rgb(37,36,27));
  rotatedOval(c, nut.x, nut.y, r * (0.76f + squash * 0.08f), r * (0.78f - squash * 0.08f), turn, amber);
  const Point tip = rotated(b.x, b.y, 0, r, turn);
  c.oval(tip.x, tip.y, 2, 2, bark);
  for (int i = -1; i <= 1; ++i) {
    const Point a = rotated(b.x, b.y, i * r * 0.28f, 0, turn);
    const Point d = rotated(b.x, b.y, i * r * 0.17f, r * 0.66f, turn);
    stroke(c, a, d, rgb(191, 121, 58));
  }
  const Point cap = rotated(b.x, b.y, 0, -r * 0.36f, turn);
  rotatedOval(c, cap.x, cap.y, r * 0.83f, r * 0.4f, turn, bark);
  for (int i = -1; i <= 1; ++i) {
    const Point mark = rotated(b.x, b.y, i * r * 0.4f, -r * 0.4f, turn);
    c.dot(mark.x, mark.y, gold);
  }
  stroke(c, rotated(b.x, b.y, 0, -r * 0.65f, turn), rotated(b.x, b.y, 1, -r * 0.92f, turn), bark, 1);
  const Point gleam = rotated(b.x,b.y,-r*.35f,r*.12f,turn);
  c.dot(gleam.x,gleam.y,cream);
}
void mushroom(Canvas& c, const ForestBody& b) {
  const Rotation turn(b.angle);
  const float press = clamp(b.value, 0, 1), r = b.radius;
  const int x = rounded(b.x), y = rounded(b.y + press * 5);
  const int capHeight = rounded(r * (0.62f - press * 0.28f));
  c.oval(x, y + rounded(r * 0.72f), rounded(r * 0.84f), 3, rgb(23, 31, 35));
  c.roundRect(x - 3, y, 7, rounded(r * (0.9f - press * 0.25f)), 2, cream);
  c.line(x + 2,y + 2,x + 2,y + rounded(r*.65f),rgb(190,172,142));
  const uint16_t capColors[] = {coral, lilac, amber};
  rotatedOval(c, x, y, r * (1 + press * 0.12f), capHeight, turn, capColors[b.variant % 3]);
  const Point a = rotated(x, y, -r * 0.43f, -capHeight * 0.2f, turn);
  const Point d = rotated(x, y, r * 0.34f, -capHeight * 0.35f, turn);
  c.oval(a.x, a.y, 2, 1, cream); c.oval(d.x, d.y, 2, 2, cream);
  c.dot(x, y + 1, cream);
  c.line(x-rounded(r*.55f),y+capHeight-1,x+rounded(r*.5f),y+capHeight-1,rgb(129,79,87));
  if (press < .1f && b.vy > .12f) {
    c.dot(x-rounded(r)-3,y-2,leafLight); c.dot(x+rounded(r)+3,y-3,leafLight);
  }
}
void fern(Canvas& c, const ForestBody& b) {
  const float length = b.radius, spread = 0.25f + clamp(b.value, 0, 1) * 0.75f;
  Point previous{rounded(b.x), rounded(b.y)};
  for (int level = 1; level <= 9; ++level) {
    const float t = level / 9.0f;
    const Rotation turn(b.angle * (0.25f + t * 0.75f));
    const Point node = rotated(b.x, b.y, 0, -length * t, turn);
    stroke(c, previous, node, leafLight, 1);
    if (level < 9) {
      const float branch = (1 - t) * length * 0.29f * spread + 2;
      for (int side = -1; side <= 1; side += 2) {
        const Point outer = rotated(node.x, node.y, side * branch, -branch * 0.5f, turn);
        const Point low = rotated(node.x, node.y, side * branch * 0.38f, branch * 0.15f, turn);
        c.triangle(node.x, node.y, outer.x, outer.y, low.x, low.y, level % 2 ? moss : mint);
      }
    }
    previous = node;
  }
  c.oval(rounded(b.x), rounded(b.y), 5, 3, rgb(59, 97, 54));
}
void firefly(Canvas& c, const ForestBody& b) {
  const float glow = clamp(b.value, 0, 1);
  const int x = rounded(b.x), y = rounded(b.y), r = rounded(b.radius);
  c.oval(x, y, r + 4, r + 4, mix(rgb(22, 43, 61), moss, glow * 0.18f));
  c.oval(x, y, r + 1, r + 1, mix(rgb(22, 43, 61), gold, glow * 0.34f));
  const int flap = glow > .55f ? -1 : 1;
  c.oval(x - 3, y - 1 + flap, 2, 1, waterLight); c.oval(x + 3, y - 1 + flap, 2, 1, waterLight);
  c.oval(x, y, 2, 3, mix(moss, gold, glow)); c.dot(x, y - 1, cream);
  c.dot(x,y-3,ink);
}
void pinecone(Canvas& c, const ForestBody& b) {
  const Rotation turn(b.angle);
  const float r = b.radius;
  rotatedOval(c, b.x, b.y, r * 0.66f, r, turn, bark);
  for (int row = 0; row < 6; ++row) {
    const float yy = -r * 0.74f + row * r * 0.29f;
    const float half = r * (0.5f - std::fabs(yy) / r * 0.2f);
    for (int column = -1; column <= 1; ++column) {
      const float xx = column * half * 0.83f + (row % 2 ? r * 0.08f : 0);
      const float small = r * 0.18f;
      diamond(c, rotated(b.x, b.y, xx, yy - small, turn),
          rotated(b.x, b.y, xx + small, yy, turn),
          rotated(b.x, b.y, xx, yy + small * 0.75f, turn),
          rotated(b.x, b.y, xx - small, yy, turn), row % 2 ? amber : furLight);
      stroke(c, rotated(b.x,b.y,xx-small*.7f,yy,turn),
          rotated(b.x,b.y,xx,yy+small*.6f,turn),rgb(102,63,41));
    }
  }
  if (b.value > 0.1f) ovalOutline(c, rounded(b.x), rounded(b.y), rounded(r + 5), rounded(r + 5), gold);
  if (std::fabs(b.spin) > 1) {
    for (unsigned i = 1; i < 8; ++i) {
      const unsigned at = b.spin > 0 ? i : 48-i;
      stroke(c,Point{rounded(b.x)+scaledUnit(kCircle[(at+47)%48][0],rounded(r+10)),rounded(b.y)+scaledUnit(kCircle[(at+47)%48][1],rounded(r+10))},
          Point{rounded(b.x)+scaledUnit(kCircle[at][0],rounded(r+10)),rounded(b.y)+scaledUnit(kCircle[at][1],rounded(r+10))},rgb(151,157,116));
    }
  }
}
void pebble(Canvas& c, const ForestBody& b) {
  const Rotation turn(b.angle);
  const uint16_t colors[] = {rgb(140, 168, 162), rgb(190, 176, 144), rgb(119, 140, 159), rgb(169, 158, 171)};
  const float r = b.radius, squash = clamp(b.value, 0, 1);
  const uint16_t color = colors[b.variant % 4];
  c.oval(rounded(b.x + 1), rounded(b.y + 2), rounded(r), rounded(r), rgb(35, 51, 51));
  rotatedOval(c, b.x, b.y, r * (1 + squash * 0.08f), r * (0.95f - squash * 0.1f), turn, color);
  const Point a = rotated(b.x, b.y, -r * 0.45f, -r * 0.43f, turn);
  const Point d = rotated(b.x, b.y, r * 0.15f, -r * 0.6f, turn);
  stroke(c, a, d, mix(color, cream, 0.6f));
  c.dot(rounded(b.x + r * 0.35f), rounded(b.y + r * 0.23f), mix(color, ink, 0.25f));
  stroke(c,rotated(b.x,b.y,-r*.7f,r*.2f,turn),rotated(b.x,b.y,r*.55f,r*.4f,turn),mix(color,ink,.13f));
}
void tuft(Canvas& c, const ForestBody& b) {
  const float pressure = clamp(b.value, 0, 1), r = b.radius;
  const int x = rounded(b.x), y = rounded(b.y + pressure * r * 0.2f);
  const int height = rounded(r * (0.85f - pressure * 0.45f));
  c.oval(x, y + height / 2, rounded(r + 2), height, rgb(25, 67, 41));
  c.oval(x, y, rounded(r * (1 + pressure * 0.15f)), height, moss);
  for (int blade = -2; blade <= 2; ++blade) {
    const int bx = x + rounded(blade * r * 0.3f);
    const int dx = rounded(b.angle * 3 + blade);
    c.line(bx, y + height / 3, bx + dx, y - height + (blade & 1 ? 3 : 0), blade & 1 ? mint : leafLight);
    c.dot(bx+dx-1,y-height+(blade&1?3:0),cream);
  }
}
void vine(Canvas& c, const ForestBody& b) {
  const int x = rounded(b.x), y = rounded(b.y), r = rounded(b.radius);
  c.line(120, 76, x, y, moss, 1); c.line(119, 76, x - 1, y, leafLight);
  leaf(c, 120 + (b.x - 120) * 0.42f + 3, 76 + (b.y - 76) * 0.42f, 6, b.angle + 0.7f, mint);
  c.oval(x, y, r, r, furLight); c.oval(x, y + 1, rounded(r * 0.84f), rounded(r * 0.7f), face);
  c.oval(x-r+1,y+3,3,4,brown); c.oval(x+r-1,y+3,3,4,brown);
  c.oval(x - r / 3, y, rounded(r * 0.27f), rounded(r * 0.2f), mask);
  c.oval(x + r / 3, y, rounded(r * 0.27f), rounded(r * 0.2f), mask);
  c.dot(x - r / 3, y, ink); c.dot(x + r / 3, y, ink);
  c.oval(x, y + r / 4, 2, 1, mask); c.line(x - 2, y + r / 2, x + 2, y + r / 2, mask);
  c.line(x-7,y+5,x-7,y+8,cream); c.line(x+7,y+5,x+7,y+8,cream);
}
void rakeTrail(Canvas& c, const ForestFidgetSnapshot& s) {
  const unsigned count = s.trailCount > ForestFidgetSnapshot::kTrailPoints ? ForestFidgetSnapshot::kTrailPoints : s.trailCount;
  const unsigned head = s.trailHead % ForestFidgetSnapshot::kTrailPoints;
  for (unsigned i = 0; i < count; ++i) {
    const auto& p = s.trail[(head + ForestFidgetSnapshot::kTrailPoints - count + i) % ForestFidgetSnapshot::kTrailPoints];
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.strength) || p.strength <= 0 ||
        p.x < -32 || p.x > 272 || p.y < 32 || p.y > 248) continue;
    const uint16_t dark = mix(rgb(182,159,112), rgb(103,87,60), p.strength * 0.85f);
    const uint16_t light = mix(rgb(182,159,112), rgb(231,208,157), p.strength * 0.65f);
    for (int groove = -1; groove <= 1; ++groove) {
      c.dot(rounded(p.x) + groove * 5, rounded(p.y), dark);
      c.dot(rounded(p.x) + groove * 5 + 1, rounded(p.y), light);
    }
    if (!i) continue;
    const auto& before = s.trail[(head + ForestFidgetSnapshot::kTrailPoints - count + i - 1) % ForestFidgetSnapshot::kTrailPoints];
    const float dx = p.x - before.x, dy = p.y - before.y;
    if (!std::isfinite(dx) || !std::isfinite(dy) || dx * dx + dy * dy > 28 * 28) continue;
    for (int groove = -1; groove <= 1; ++groove) {
      c.line(rounded(before.x) + groove * 5, rounded(before.y), rounded(p.x) + groove * 5, rounded(p.y), dark);
      c.line(rounded(before.x) + groove * 5 + 1, rounded(before.y), rounded(p.x) + groove * 5 + 1, rounded(p.y), light);
    }
  }
}
}  // namespace

void drawForestFidget(uint16_t* pixels, const ForestFidgetSnapshot& s) {
  if (!pixels) return;
  Canvas c{pixels};
  c.rect(0, 0, 240, 240, ink);
  background(c, s.toy, s.elapsedMs);
  const unsigned count = s.bodyCount > ForestFidgetSnapshot::kBodies ? ForestFidgetSnapshot::kBodies : s.bodyCount;
  for (unsigned i = 0; i < count; ++i) {
    const auto& b = s.bodies[i];
    if (!valid(b)) continue;
    const int x = rounded(b.x), y = rounded(b.y), r = rounded(b.radius);
    switch (s.toy) {
      case ForestToy::DewPond:
        ovalOutline(c, x, y, r, rounded(b.radius * 0.6f), mix(water, waterLight, clamp(b.value, 0, 1)));
        if (r > 5) ovalOutline(c, x, y, r - 4, rounded((b.radius - 4) * 0.6f), mix(water, mint, b.value * 0.45f));
        break;
      case ForestToy::AcornRoll: acorn(c, b); break;
      case ForestToy::MushroomPop: mushroom(c, b); break;
      case ForestToy::FernBrush: fern(c, b); break;
      case ForestToy::FireflyJar: firefly(c, b); break;
      case ForestToy::PineconeSpin: if (b.radius >= 1) pinecone(c, b); break;
      case ForestToy::PebbleStack: pebble(c, b); break;
      case ForestToy::MossSquish: tuft(c, b); break;
      case ForestToy::LeafGlobe: {
        const uint16_t colors[] = {mint, gold, coral, moss};
        leaf(c, b.x, b.y, b.radius, b.angle, colors[b.variant % 4], 0.35f + 0.23f * std::fabs(std::cos(b.value)));
        break;
      }
      case ForestToy::VineSwing: if (i == 0) vine(c, b); break;
      case ForestToy::Rainstick:
        c.dot(x+1,y+r+1,rgb(41,42,30));
        c.oval(x, y, r, r, b.variant % 2 ? gold : coral);
        c.dot(x - 1, y - 1, cream);
        if (b.value > 0.2f) {
          const uint16_t spark = mix(amber,cream,b.value);
          c.dot(x-r-2,y-1,spark); c.dot(x+r+2,y+1,spark); c.dot(x,y-r-2,spark);
        }
        break;
      case ForestToy::ZenRake: case ForestToy::Count: break;
    }
  }
  if (s.toy == ForestToy::ZenRake) rakeTrail(c, s);
  if (s.toy == ForestToy::FireflyJar) {
    c.line(30,105,30,135,rgb(67,104,119)); c.line(32,107,32,116,rgb(122,155,157));
    c.line(209,177,209,189,rgb(66,101,117));
  } else if (s.toy == ForestToy::LeafGlobe) {
    c.line(41,100,46,93,rgb(132,174,183)); c.line(48,91,53,87,rgb(171,204,207));
    c.line(191,184,186,190,rgb(75,119,137));
  }
  if (s.touching && s.touchX >= 16 && s.touchX < 224 && s.touchY >= 68 && s.touchY < 216) {
    if (s.toy == ForestToy::ZenRake) {
      c.line(s.touchX, s.touchY - 14, s.touchX, s.touchY - 3, bark, 1);
      c.line(s.touchX - 8, s.touchY - 3, s.touchX + 8, s.touchY - 3, bark, 1);
      for (int tine = -2; tine <= 2; ++tine) c.line(s.touchX + tine * 4, s.touchY - 3, s.touchX + tine * 4, s.touchY + 2, bark);
    } else if (s.toy == ForestToy::MossSquish || s.toy == ForestToy::MushroomPop || s.toy == ForestToy::FernBrush)
      paw(c, s.touchX, s.touchY, cream);
    else ovalOutline(c, s.touchX, s.touchY, 6, 6, cream);
  }
  // Field-edge clipping is explicit; nothing animated can overwrite the fixed
  // title, hardware rail or footer, including large rings and edge-held toys.
  c.rect(0, 0, 240, 68, ink); c.rect(0, 216, 240, 24, ink);
  c.rect(0, 68, 16, 148, ink); c.rect(224, 68, 16, 148, ink);
  c.roundRect(20, 8, 200, 16, 3, mint);
  c.rect(86, 10, 1, 12, ink); c.rect(154, 10, 1, 12, ink);
  c.text(41, 12, "PREV", ink); c.text(108, 12, "BACK", ink); c.text(175, 12, "NEXT", ink);
  c.centered(30, "FOREST FIDGET", cream, 2);
  c.text(22, 53, forestToyName(s.toy), gold);
  char index[8];
  const unsigned toy = static_cast<unsigned>(s.toy), total = static_cast<unsigned>(ForestToy::Count);
  snprintf(index, sizeof(index), "%u/%u", toy < total ? toy + 1 : 0, total);
  c.text(218 - static_cast<int>(strlen(index)) * 6, 53, index, muted);
  char hint[31] = {};
  const char* source = forestToyHint(s.toy);
  if (source) for (unsigned i = 0; i + 1 < sizeof(hint) && source[i]; ++i) hint[i] = source[i];
  c.centered(224, hint, muted);
}
}  // namespace sloth
