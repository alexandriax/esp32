#include "../firmware/sloth_pet/forest_fidget.h"
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "Usage: forest_fidget_preview output.ppm toy-index [active]\n"); return 1; }
  const unsigned toy = static_cast<unsigned>(atoi(argv[2]));
  if (toy >= static_cast<unsigned>(sloth::ForestToy::Count)) return 2;
  sloth::ForestFidget fidget;
  fidget.reset(0, 4207); fidget.cycle(static_cast<int>(toy), 0);
  uint32_t now = 0;
  const bool active = argc > 3 && strcmp(argv[3], "active") == 0;
  if (active) {
    for (unsigned frame = 0; frame < 40; ++frame) {
      now += 20;
      fidget.motion(0.2f, 0.25f, 0.9f, now);
      int x = 64 + static_cast<int>(frame) * 3, y = 133 + static_cast<int>(std::sin(frame * 0.18f) * 22);
      if (toy == static_cast<unsigned>(sloth::ForestToy::PineconeSpin)) {
        x = 120 + static_cast<int>(std::cos(frame * .15f) * 32);
        y = 142 + static_cast<int>(std::sin(frame * .15f) * 32);
      } else if (toy == static_cast<unsigned>(sloth::ForestToy::AcornRoll) ||
                 toy == static_cast<unsigned>(sloth::ForestToy::PebbleStack)) {
        if (!frame) { x = static_cast<int>(fidget.snapshot().bodies[0].x); y = static_cast<int>(fidget.snapshot().bodies[0].y); }
        else { x = 60 + static_cast<int>(frame) * 3; y = 142 - static_cast<int>(frame) / 2; }
      } else if (toy == static_cast<unsigned>(sloth::ForestToy::MushroomPop)) { x = 145; y = 143; }
      else if (toy == static_cast<unsigned>(sloth::ForestToy::MossSquish)) { x = 120; y = 123; }
      else if (toy == static_cast<unsigned>(sloth::ForestToy::Rainstick)) { x = 120; y = 93; }
      fidget.touch(true, x, y, now);
      fidget.advance(now);
    }
  } else for (unsigned frame = 0; frame < 25; ++frame) { now += 20; fidget.advance(now); }
  uint16_t pixels[240 * 240];
  sloth::drawForestFidget(pixels, fidget.snapshot());
  FILE* output = fopen(argv[1], "wb");
  if (!output) return 3;
  fprintf(output, "P6\n480 480\n255\n");
  for (int y = 0; y < 480; ++y) for (int x = 0; x < 480; ++x) {
    const uint16_t p = pixels[(y / 2) * 240 + x / 2];
    const unsigned r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    const unsigned char color[] = {static_cast<unsigned char>((r << 3) | (r >> 2)),
        static_cast<unsigned char>((g << 2) | (g >> 4)), static_cast<unsigned char>((b << 3) | (b >> 2))};
    if (fwrite(color, 1, 3, output) != 3) { fclose(output); return 4; }
  }
  return fclose(output) == 0 ? 0 : 4;
}
