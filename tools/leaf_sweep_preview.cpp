#include "../firmware/sloth_pet/leaf_sweep.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: leaf_sweep_preview output.ppm [ready|sweeping|cans|game-over]\n");
    return 1;
  }
  const char* mode = argc > 2 ? argv[2] : "ready";
  sloth::LeafSweepGame game;
  game.reset(0, 143);
  uint32_t now = 0;
  while (now < (strcmp(mode, "ready") == 0 ? 600u : 9000u)) { now += 20; game.advance(now); }
  if (strcmp(mode, "sweeping") == 0) {
    game.touch(true, 35, 150, now); game.touch(true, 152, 160, now);
  } else if (strcmp(mode, "game-over") == 0) {
    uint32_t random = 2731;
    while (game.snapshot().phase == sloth::LeafSweepPhase::Playing && now < 180000) {
      random ^= random << 13; random ^= random >> 17; random ^= random << 5;
      now += 20;
      game.touch(true, 15 + random % 210, 83 + (random >> 9) % 134, now);
    }
  }
  uint16_t pixels[240 * 240];
  sloth::drawLeafSweep(pixels, game);
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
