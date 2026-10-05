#include "../firmware/sloth_pet/volume_overlay.h"
#include <cassert>
#include <cstdio>

int main() {
  sloth::VolumeOverlay overlay;
  assert(!overlay.visible(0));
  overlay.show(35, 50);
  assert(overlay.volume() == 35 && overlay.visible(50) && overlay.visible(1049));
  assert(!overlay.visible(1050));
  // Every press refreshes the timeout, including an unchanged clamped value.
  overlay.show(60, 2000);
  overlay.show(60, 2900);
  assert(overlay.visible(3899) && !overlay.visible(3900));
  overlay.show(255, UINT32_MAX - 499u);
  assert(overlay.volume() == 60 && overlay.visible(499) && !overlay.visible(500));
  overlay.clear();
  assert(!overlay.visible(0));

  const uint16_t sentinel = 0x1234;
  uint16_t guarded[240 * 240 + 2];
  for (unsigned value = 0; value <= 255; ++value) {
    for (auto& pixel : guarded) pixel = sentinel;
    sloth::drawVolumeOverlay(guarded + 1, static_cast<uint8_t>(value));
    assert(guarded[0] == sentinel && guarded[240 * 240 + 1] == sentinel);
    for (int y = 0; y < 240; ++y) {
      for (int x = 0; x < 240; ++x) {
        const bool inside = x >= sloth::kVolumeOverlayX && x < sloth::kVolumeOverlayX + sloth::kVolumeOverlayWidth &&
                            y >= sloth::kVolumeOverlayY && y < sloth::kVolumeOverlayY + sloth::kVolumeOverlayHeight;
        const uint16_t pixel = guarded[1 + y * 240 + x];
        assert(inside ? pixel != sentinel : pixel == sentinel);
      }
    }
    // Empty at mute, full at the safe maximum, proportional between them.
    unsigned filled = 0;
    for (int x = 36; x < 204; ++x)
      filled += guarded[1 + 192 * 240 + x] == sloth::graphics::mint;
    assert(filled == 168 * (value > 60 ? 60 : value) / 60);
  }
  sloth::drawVolumeOverlay(nullptr, 35);
  std::puts("Volume overlay: expiry, repeated presses, wraparound, bounds and proportional min/max passed");
}
