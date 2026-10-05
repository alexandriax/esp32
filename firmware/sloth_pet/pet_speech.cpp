#include "pet_speech.h"
#include "pet_canvas.h"

#include <stddef.h>
#include <string.h>

namespace sloth {
namespace {
bool space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
}

SpeechLines wrapSpeech(const char* text) {
  SpeechLines lines;
  if (!text) return lines;
  unsigned row = 0, column = 0;
  while (*text) {
    while (space(*text)) ++text;
    if (!*text) break;
    size_t length = 0;
    while (text[length] && !space(text[length])) ++length;
    if (column && column + 1 + length > kSpeechColumns) { ++row; column = 0; }
    if (row >= kSpeechLines) { lines.truncated = true; break; }
    if (column) lines.text[row][column++] = ' ';
    for (size_t i = 0; i < length; ++i) {
      if (column == kSpeechColumns) { ++row; column = 0; }
      if (row >= kSpeechLines) { lines.truncated = true; break; }
      lines.text[row][column++] = text[i];
      lines.count = static_cast<uint8_t>(row + 1);
    }
    if (lines.truncated) break;
    text += length;
  }
  if (lines.truncated) {
    char* last = lines.text[kSpeechLines - 1];
    const size_t length = strlen(last);
    const size_t start = length > kSpeechColumns - 3 ? kSpeechColumns - 3 : length;
    memcpy(last + start, "...", 4);
  }
  return lines;
}

SpeechLayout speechLayout(const char* text) {
  SpeechLayout layout;
  layout.lines = wrapSpeech(text);
  if (!layout.lines.count) return layout;
  unsigned longest = 0;
  for (unsigned i = 0; i < layout.lines.count; ++i) {
    const unsigned length = static_cast<unsigned>(strlen(layout.lines.text[i]));
    if (length > longest) longest = length;
  }
  const int contentWidth = static_cast<int>(longest) * 6 - 1 + 16;
  layout.width = contentWidth < 80 ? 80 : contentWidth > 208 ? 208 : contentWidth;
  layout.height = static_cast<int>(layout.lines.count) * 10 + 13;
  layout.x = (240 - layout.width) / 2;
  layout.y = 168 - layout.height;
  return layout;
}

bool hitTestPetSpeech(int x, int y, const char* speech) {
  // Reject before subtracting/squaring, including hostile integer extremes.
  if (x < 0 || x >= 240 || y < 46 || y >= 168) return false;
  const int dx = x - 120, dy = y - 99;
  if (dx * dx * 50 * 50 + dy * dy * 64 * 64 <= 64 * 64 * 50 * 50) return true;
  const SpeechLayout layout = speechLayout(speech);
  if (!layout.lines.count) return false;
  // Match Canvas::roundRect's rectangular strips and four radius-eight discs.
  const int left = layout.x, top = layout.y, width = layout.width, height = layout.height;
  if (x >= left && x < left + width && y >= top && y < top + height) {
    if ((x >= left + 8 && x < left + width - 8) ||
        (y >= top + 8 && y < top + height - 8)) return true;
    const int cx = x < left + 8 ? left + 8 : left + width - 9;
    const int cy = y < top + 8 ? top + 8 : top + height - 9;
    if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= 64) return true;
  }
  // Triangle vertices (113,top+1), (127,top+1), (120,top-6).
  const int rise = y - (top - 6);
  return rise >= 0 && rise <= 7 && x >= 120 - rise && x <= 120 + rise;
}

uint32_t speechDurationMs(const char* text) {
  if (!text || !*text) return 0;
  size_t length = 0;
  while (length < 160 && text[length]) ++length;
  const uint32_t duration = 6500u + static_cast<uint32_t>(length) * 100u;
  return duration < 10000u ? 10000u : duration > 22000u ? 22000u : duration;
}

void drawPetSpeech(uint16_t* pixels, const char* text) {
  if (!pixels) return;
  const SpeechLayout layout = speechLayout(text);
  if (!layout.lines.count) return;
  using namespace graphics;
  Canvas c{pixels};
  c.roundRect(layout.x, layout.y, layout.width, layout.height, 8, gold);
  c.roundRect(layout.x + 1, layout.y + 1, layout.width - 2, layout.height - 2, 7, cream);
  c.triangle(113, layout.y + 1, 127, layout.y + 1, 120, layout.y - 6, gold);
  c.triangle(115, layout.y + 1, 125, layout.y + 1, 120, layout.y - 4, cream);
  for (unsigned i = 0; i < layout.lines.count; ++i)
    c.text(layout.x + 8, layout.y + 7 + static_cast<int>(i) * 10, layout.lines.text[i], ink);
}
}  // namespace sloth
