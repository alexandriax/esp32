#include "../firmware/sloth_pet/tetris_game.h"
#include "../firmware/sloth_pet/tetris_controls.h"

#include <stdio.h>
#include <string.h>

namespace sloth {
struct TetrisTestAccess { static TetrisSnapshot& state(TetrisGame& game) { return game.state_; } };
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: tetris_preview output.ppm [falling|stack|game-over|metrics|pressed-left|pressed-right|pressed-rotate|pressed-drop|pressed-pause|next-i|next-o|next-t|next-s|next-z|next-j|next-l]\n");
    return 1;
  }
  const char* mode = argc > 2 ? argv[2] : "falling";
  sloth::TetrisGame game;
  game.reset(0, 42);
  uint32_t now = 0;
  if (strcmp(mode, "falling") == 0 || strcmp(mode, "metrics") == 0 ||
      strncmp(mode, "pressed-", 8) == 0 || strncmp(mode, "next-", 5) == 0) {
    for (unsigned i = 0; i < 25; ++i) { now += 100; game.advance(now); }
  } else {
    unsigned locked = 0;
    bool setup = true;
    while (game.snapshot().phase == sloth::TetrisPhase::Playing && now < 1000000) {
      if (setup && strcmp(mode, "stack") == 0) {
        for (unsigned turn = 0; turn < locked % 3; ++turn) game.rotate(now);
        for (unsigned press = 0; press < (locked * 3 + 2) % 10; ++press) game.moveRight(now);
      }
      setup = false;
      now += 100; game.advance(now);
      if (game.takeEvents() & sloth::TetrisLock) {
        ++locked; setup = true;
        if (strcmp(mode, "stack") == 0 && locked == 9) break;
      }
    }
  }
  if (strcmp(mode, "metrics") == 0) {
    auto& s = sloth::TetrisTestAccess::state(game);
    s.score = UINT32_MAX; s.lines = UINT32_MAX; s.level = 99;
  }
  if (strncmp(mode, "next-", 5) == 0) {
    const char* shapes = "iotszjl";
    for (unsigned i = 0; i < 7; ++i) if (mode[5] == shapes[i])
      sloth::TetrisTestAccess::state(game).next = static_cast<sloth::TetrisPiece>(i);
  }
  const char* pressedNames[] = {"", "pressed-left", "pressed-right", "pressed-rotate", "pressed-drop", "pressed-pause"};
  int pressed = 0;
  for (int i = 1; i <= 5; ++i) if (strcmp(mode, pressedNames[i]) == 0) pressed = i;
  uint16_t pixels[240 * 240];
  sloth::drawTetris(pixels, game, pressed, now);
  FILE* output = fopen(argv[1], "wb");
  if (!output) return 2;
  fprintf(output, "P6\n480 480\n255\n");
  for (int y = 0; y < 480; ++y) for (int x = 0; x < 480; ++x) {
    const uint16_t pixel = pixels[(y / 2) * 240 + x / 2];
    const unsigned r = (pixel >> 11) & 31, g = (pixel >> 5) & 63, b = pixel & 31;
    const unsigned char color[] = {static_cast<unsigned char>((r << 3) | (r >> 2)),
        static_cast<unsigned char>((g << 2) | (g >> 4)),
        static_cast<unsigned char>((b << 3) | (b >> 2))};
    if (fwrite(color, 1, sizeof(color), output) != sizeof(color)) { fclose(output); return 3; }
  }
  return fclose(output) == 0 ? 0 : 3;
}
