#include "../firmware/sloth_pet/tetris_game.h"
#include "../firmware/sloth_pet/tetris_controls.h"
#include "../firmware/sloth_pet/pet_canvas.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace sloth {
struct TetrisTestAccess { static TetrisSnapshot& state(TetrisGame& game) { return game.state_; } };
}
namespace {
constexpr unsigned kPixels = 240 * 240;
bool inside(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}
uint32_t hashRegion(const uint16_t* pixels, int left, int top, int width, int height) {
  uint32_t hash = 2166136261u;
  for (int y = top; y < top + height; ++y) for (int x = left; x < left + width; ++x) {
    hash ^= pixels[y * 240 + x]; hash *= 16777619u;
  }
  return hash;
}
}
int main() {
  using namespace sloth;
  TetrisGame game; game.reset(0, 123);
  auto& s = TetrisTestAccess::state(game);
  uint16_t frame[kPixels + 2], baseline[kPixels];
  frame[0] = 0x1234; frame[kPixels + 1] = 0xabcd;
  drawTetris(nullptr, game);
  drawTetris(baseline, game);
  const auto original = s;
  uint32_t shapes[7] = {};
  for (unsigned piece = 0; piece < 7; ++piece) {
    s.next = static_cast<TetrisPiece>(piece);
    drawTetris(frame + 1, game);
    shapes[piece] = hashRegion(frame + 1, 142, 98, 30, 19);
    for (unsigned prior = 0; prior < piece; ++prior) assert(shapes[prior] != shapes[piece]);
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x)
      if (!inside(x, y, 142, 98, 30, 19)) assert(frame[1 + y * 240 + x] == baseline[y * 240 + x]);
  }
  s = original;
  s.score = UINT32_MAX; s.lines = UINT32_MAX;
  drawTetris(frame + 1, game);
  assert(hashRegion(baseline, 112, 60, 108, 7) != hashRegion(frame + 1, 112, 60, 108, 7));
  assert(hashRegion(baseline, 112, 85, 60, 7) != hashRegion(frame + 1, 112, 85, 60, 7));
  for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x)
    if (!inside(x, y, 112, 60, 60, 7) && !inside(x, y, 112, 85, 60, 7))
      assert(frame[1 + y * 240 + x] == baseline[y * 240 + x]); // No spill into LEVEL, NEXT or buttons.
  s.level = 99; drawTetris(frame + 1, game);
  assert(hashRegion(baseline, 190, 85, 18, 7) != hashRegion(frame + 1, 190, 85, 18, 7));
  s = original;
  drawTetris(frame + 1, game, 0, 4560);
  unsigned blinkChanges = 0;
  for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x) {
    if (frame[1 + y * 240 + x] != baseline[y * 240 + x]) {
      assert(inside(x,y,180,94,40,30));
      ++blinkChanges;
    }
  }
  assert(blinkChanges > 0); // Idle motion stays inside Moss's preview companion area.
  for (unsigned action = 1; action <= 5; ++action) {
    drawTetris(frame + 1, game, static_cast<int>(action));
    const auto rect = tetrisButtonRect(static_cast<TetrisAction>(action));
    if (action < 5) assert(hashRegion(baseline, rect.x, rect.y, rect.width, rect.height) !=
                          hashRegion(frame + 1, rect.x, rect.y, rect.width, rect.height));
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x)
      if (!rect.contains(x, y) && !inside(x, y, 20, 8, 200, 16))
        assert(frame[1 + y * 240 + x] == baseline[y * 240 + x]);
  }
  drawTetris(frame + 1, game, 255);
  assert(memcmp(baseline, frame + 1, sizeof(baseline)) == 0);
  assert(memcmp(&s, &original, sizeof(s)) == 0); // Renderer is read-only.
  for (int row = 0; row < 20; ++row) for (int column = 0; column < 10; ++column)
    s.board[row][column] = static_cast<uint8_t>((row + column) % 7 + 1);
  s.phase = TetrisPhase::GameOver;
  drawTetris(frame + 1, game);
  for (unsigned action = 1; action <= 4; ++action) {
    const auto r = tetrisButtonRect(static_cast<TetrisAction>(action));
    assert(hashRegion(baseline, r.x, r.y, r.width, r.height) == hashRegion(frame + 1, r.x, r.y, r.width, r.height));
  }
  for (int y = 224; y < 240; ++y) for (int x = 0; x < 240; ++x) assert(frame[1 + y * 240 + x] == graphics::ink);
  assert(frame[0] == 0x1234 && frame[kPixels + 1] == 0xabcd);
  puts("tetris_render: all seven next shapes, maximum metrics without overlap, pressed highlights, game-over buttons and framebuffer bounds passed");
}
