#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Allocation-free 240 x 240 RGB565 drawing shared by the pet and settings UI.
namespace sloth {
namespace graphics {

constexpr int kSize = 240;

inline uint16_t rgb(unsigned r, unsigned g, unsigned b) {
  return static_cast<uint16_t>(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3));
}

const uint16_t ink = rgb(14, 35, 35);
const uint16_t cream = rgb(249, 235, 196);
const uint16_t muted = rgb(146, 181, 165);
const uint16_t gold = rgb(243, 189, 98);
const uint16_t mint = rgb(135, 205, 163);
const uint16_t brown = rgb(158, 119, 88);
const uint16_t furLight = rgb(181, 144, 104);
const uint16_t furDark = rgb(108, 77, 59);
const uint16_t mask = rgb(91, 65, 49);
const uint16_t face = rgb(231, 210, 166);

// Five columns, low bit at top. A deliberately small, legible built-in font.
const uint8_t letters[26][5] = {
  {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
  {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
  {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
  {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
  {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
  {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
  {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
  {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
  {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
  {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
  {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};
const uint8_t numbers[10][5] = {
  {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
  {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
  {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e}
};

struct Canvas {
  uint16_t* pixels;

  void dot(int x, int y, uint16_t color) {
    if (x >= 0 && x < kSize && y >= 0 && y < kSize)
      pixels[y * kSize + x] = color;
  }

  void rect(int x, int y, int w, int h, uint16_t color) {
    const int endX = x + w < kSize ? x + w : kSize;
    const int endY = y + h < kSize ? y + h : kSize;
    for (int yy = y < 0 ? 0 : y; yy < endY; ++yy)
      for (int xx = x < 0 ? 0 : x; xx < endX; ++xx)
        pixels[yy * kSize + xx] = color;
  }

  void oval(int x, int y, int rx, int ry, uint16_t color) {
    if (rx <= 0 || ry <= 0) return;
    const int limit = rx * rx * ry * ry;
    for (int yy = -ry; yy <= ry; ++yy)
      for (int xx = -rx; xx <= rx; ++xx)
        if (xx * xx * ry * ry + yy * yy * rx * rx <= limit)
          dot(x + xx, y + yy, color);
  }

  void line(int x0, int y0, int x1, int y1, uint16_t color, int radius = 0) {
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      if (radius) oval(x0, y0, radius, radius, color);
      else dot(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      const int twice = 2 * err;
      if (twice >= dy) { err += dy; x0 += sx; }
      if (twice <= dx) { err += dx; y0 += sy; }
    }
  }

  void triangle(int ax, int ay, int bx, int by, int cx, int cy, uint16_t color) {
    const int minX = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    const int maxX = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    const int minY = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    const int maxY = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
    for (int y = minY; y <= maxY; ++y) {
      for (int x = minX; x <= maxX; ++x) {
        const int a = (x - ax) * (by - ay) - (y - ay) * (bx - ax);
        const int b = (x - bx) * (cy - by) - (y - by) * (cx - bx);
        const int c = (x - cx) * (ay - cy) - (y - cy) * (ax - cx);
        if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0))
          dot(x, y, color);
      }
    }
  }

  void roundRect(int x, int y, int w, int h, int r, uint16_t color) {
    rect(x + r, y, w - 2 * r, h, color);
    rect(x, y + r, w, h - 2 * r, color);
    oval(x + r, y + r, r, r, color);
    oval(x + w - r - 1, y + r, r, r, color);
    oval(x + r, y + h - r - 1, r, r, color);
    oval(x + w - r - 1, y + h - r - 1, r, r, color);
  }

  void text(int x, int y, const char* str, uint16_t color, int scale = 1) {
    if (!str) return;
    for (size_t pos = 0; pos < 40 && str[pos]; ++pos, x += 6 * scale) {
      char ch = str[pos];
      if (ch >= 'a' && ch <= 'z') ch -= ('a' - 'A');
      uint8_t symbol[5] = {0,0,0,0,0};
      const uint8_t* glyph = symbol;
      if (ch >= 'A' && ch <= 'Z') glyph = letters[ch - 'A'];
      else if (ch >= '0' && ch <= '9') glyph = numbers[ch - '0'];
      else if (ch == '-') { symbol[1] = symbol[2] = symbol[3] = 8; }
      else if (ch == '+') { symbol[1] = symbol[3] = 8; symbol[2] = 0x3e; }
      else if (ch == '.') { symbol[2] = 0x60; }
      else if (ch == ',') { symbol[1] = 0x40; symbol[2] = 0x20; }
      else if (ch == '!') { symbol[2] = 0x5f; }
      else if (ch == '?') { symbol[0] = 2; symbol[1] = 1;
        symbol[2] = 0x51; symbol[3] = 9; symbol[4] = 6; }
      else if (ch == ':') { symbol[2] = 0x36; }
      else if (ch == '%') { symbol[0] = 0x63; symbol[1] = 0x13;
        symbol[2] = 8; symbol[3] = 0x64; symbol[4] = 0x63; }
      else if (ch == '\'') { symbol[2] = 3; }
      else if (ch == '/') { symbol[0] = 0x40; symbol[1] = 0x20;
        symbol[2] = 0x18; symbol[3] = 4; symbol[4] = 2; }
      for (int xx = 0; xx < 5; ++xx)
        for (int yy = 0; yy < 7; ++yy)
          if (glyph[xx] & (1 << yy))
            rect(x + xx * scale, y + yy * scale, scale, scale, color);
    }
  }

  void centered(int y, const char* str, uint16_t color, int scale = 1) {
    const int length = str ? static_cast<int>(strlen(str)) : 0;
    text((kSize - (length * 6 - 1) * scale) / 2, y, str, color, scale);
  }
};

}  // namespace graphics
}  // namespace sloth
