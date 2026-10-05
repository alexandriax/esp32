#pragma once
#include <stddef.h>

namespace sloth {
// Transform panel touch coordinates back into the unrotated UI space.
// Uses the same inverse mapping as pixel lookup, at either 240 or 480 scale.
inline bool unrotateTouch(unsigned& x, unsigned& y, unsigned size, unsigned turns) {
  if (!size || x >= size || y >= size) return false;
  for (unsigned turn = 0; turn < (turns & 3u); ++turn) {
    const unsigned oldX = x; x = y; y = size - 1 - oldX;
  }
  return true;
}
inline void unrotateVector(float& x, float& y, unsigned turns) {
  for (unsigned turn = 0; turn < (turns & 3u); ++turn) {
    const float oldX = x; x = y; y = -oldX;
  }
}
// Source index for a destination pixel after clockwise quarter turns.
inline size_t rotatedSource(unsigned x, unsigned y, unsigned size, unsigned turns) {
  switch (turns & 3u) {
    case 1: return static_cast<size_t>(size - 1 - x) * size + y;
    case 2: return static_cast<size_t>(size - 1 - y) * size + size - 1 - x;
    case 3: return static_cast<size_t>(x) * size + size - 1 - y;
    default: return static_cast<size_t>(y) * size + x;
  }
}
}
