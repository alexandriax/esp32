#include "../firmware/sloth_pet/pong_game.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: pong_preview output.ppm [ready|playing|score|score-wave|score-rest|winner|winner-rest]\n");
    return 1;
  }
  sloth::PongGame game;
  const char* mode = argc > 2 ? argv[2] : "ready";
  uint32_t now = 0;
  if (strcmp(mode, "playing") == 0) {
    game.press(0, now); game.press(0, now); game.press(1, now); game.press(1, now);
    for (; now < 400; now += 8) game.advance(now + 8);
  } else if (strncmp(mode, "score", 5) == 0 || strncmp(mode, "winner", 6) == 0) {
    const unsigned points = strncmp(mode, "winner", 6) == 0 ? sloth::PongGame::kWinningScore : 1;
    for (unsigned point = 0; point < points; ++point) {
      game.press(0, now);
      for (unsigned frame = 0; frame < 150 && game.snapshot().phase == sloth::PongPhase::Playing; ++frame) {
        now += 8; game.advance(now);
      }
    }
    if (strcmp(mode, "score-wave") == 0) game.advance(now + 240);
    if (strcmp(mode, "score-rest") == 0 || strcmp(mode, "winner-rest") == 0)
      game.advance(now + sloth::PongGame::kCheerDurationMs);
  }
  uint16_t pixels[240 * 240];
  sloth::drawPong(pixels, game);
  FILE* output = fopen(argv[1], "wb");
  if (!output) return 2;
  fprintf(output, "P6\n480 480\n255\n");
  for (int y = 0; y < 480; ++y) {
    for (int x = 0; x < 480; ++x) {
      const uint16_t pixel = pixels[(y / 2) * 240 + x / 2];
      const unsigned r = (pixel >> 11) & 31, g = (pixel >> 5) & 63, b = pixel & 31;
      const unsigned char color[] = {static_cast<unsigned char>((r << 3) | (r >> 2)),
                                    static_cast<unsigned char>((g << 2) | (g >> 4)),
                                    static_cast<unsigned char>((b << 3) | (b >> 2))};
      if (fwrite(color, 1, sizeof(color), output) != sizeof(color)) { fclose(output); return 3; }
    }
  }
  return fclose(output) == 0 ? 0 : 3;
}
