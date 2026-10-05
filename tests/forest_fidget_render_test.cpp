#include "../firmware/sloth_pet/forest_fidget.h"
#include <assert.h>
#include <limits>
#include <stdio.h>
#include <string.h>

namespace {
constexpr unsigned kPixels = 240 * 240;
uint32_t hash(const uint16_t* pixels) {
  uint32_t result = 2166136261u;
  for (unsigned i = 0; i < kPixels; ++i) { result ^= pixels[i]; result *= 16777619u; }
  return result;
}
bool field(int x, int y) { return x >= 16 && x < 224 && y >= 68 && y < 216; }
}
int main() {
  using namespace sloth;
  uint16_t guarded[kPixels + 2], baseline[kPixels];
  guarded[0] = 0x1234; guarded[kPixels + 1] = 0xabcd;
  uint32_t hashes[static_cast<unsigned>(ForestToy::Count)] = {};
  for (unsigned toy = 0; toy < static_cast<unsigned>(ForestToy::Count); ++toy) {
    ForestFidget fidget; fidget.reset(0, 551); fidget.cycle(static_cast<int>(toy), 0);
    drawForestFidget(nullptr, fidget.snapshot());
    assert(strlen(forestToyName(fidget.snapshot().toy)) <= 24);
    assert(strlen(forestToyHint(fidget.snapshot().toy)) <= 30);
    for (uint32_t t = 20; t <= 240; t += 20) fidget.advance(t);
    const auto before = fidget.snapshot();
    drawForestFidget(guarded + 1, before);
    assert(memcmp(&before, &fidget.snapshot(), sizeof(before)) == 0);
    hashes[toy] = hash(guarded + 1);
    for (unsigned prior = 0; prior < toy; ++prior) assert(hashes[prior] != hashes[toy]);
    memcpy(baseline, guarded + 1, sizeof(baseline));
    for (unsigned edge = 0; edge < 4; ++edge) {
      auto s = before;
      s.touching = true; s.touchX = edge & 1 ? 223 : 16; s.touchY = edge & 2 ? 215 : 68;
      for (unsigned i = 0; i < s.bodyCount; ++i) {
        s.bodies[i].active = true; s.bodies[i].radius = 76;
        s.bodies[i].x = s.touchX; s.bodies[i].y = s.touchY;
        s.bodies[i].value = .9f; s.bodies[i].angle = 1.1f;
      }
      s.trailCount = ForestFidgetSnapshot::kTrailPoints;
      s.trailHead = 51;
      for (auto& p : s.trail) { p.x = s.touchX; p.y = s.touchY; p.strength = .9f; }
      drawForestFidget(guarded + 1, s);
      assert(guarded[0] == 0x1234 && guarded[kPixels + 1] == 0xabcd);
      for (int y = 0; y < 240; ++y) for (int x = 0; x < 240; ++x)
        if (!field(x, y)) assert(guarded[1 + y * 240 + x] == baseline[y * 240 + x]);
    }
    // Touch feedback and toy changes produce visible frames, not only physics.
    fidget.touch(true, 100, 130, 260); fidget.touch(true, 136, 155, 280);
    drawForestFidget(guarded + 1, fidget.snapshot());
    assert(memcmp(baseline, guarded + 1, sizeof(baseline)) != 0);
    auto malformed = before;
    malformed.bodyCount = malformed.trailCount = malformed.trailHead = 255;
    for (auto& b : malformed.bodies) { b.active = true; b.x = std::numeric_limits<float>::quiet_NaN(); }
    for (auto& p : malformed.trail) p.strength = std::numeric_limits<float>::quiet_NaN();
    drawForestFidget(guarded + 1, malformed);
    assert(guarded[0] == 0x1234 && guarded[kPixels + 1] == 0xabcd);
  }
  puts("forest_fidget_render: 12 distinct toys, visible touch responses, fixed chrome clipping, immutable snapshots and invalid-input bounds passed");
}
