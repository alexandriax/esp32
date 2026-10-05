#pragma once
#include "audio_buffer.h"
#include "pet_canvas.h"
#include <stdio.h>

namespace sloth {
constexpr int kVolumeOverlayX = 24;
constexpr int kVolumeOverlayY = 164;
constexpr int kVolumeOverlayWidth = 192;
constexpr int kVolumeOverlayHeight = 58;

// Only the waiting screen uses this local timer. Live desktops compose the
// same card on the Mac so expiring it restores even unchanged desktop pixels.
class VolumeOverlay {
 public:
  void show(uint8_t volume, uint32_t now) {
    volume_ = volume > audio_output::kMaxVolume ? audio_output::kMaxVolume : volume;
    since_ = now;
    shown_ = true;
  }
  bool visible(uint32_t now) const { return shown_ && now - since_ < 1000u; }
  uint8_t volume() const { return volume_; }
  void clear() { shown_ = false; }
 private:
  uint32_t since_ = 0;
  uint8_t volume_ = 0;
  bool shown_ = false;
};

// Paint only this opaque card into an existing unrotated 240x240 RGB565 canvas.
// The host reuses this artwork at both transport sizes, before rotating pixels.
inline void drawVolumeOverlay(uint16_t* pixels, uint8_t volume) {
  if (!pixels) return;
  using namespace graphics;
  const unsigned bounded = volume > audio_output::kMaxVolume ? audio_output::kMaxVolume : volume;
  Canvas c = {pixels};
  c.rect(kVolumeOverlayX, kVolumeOverlayY, kVolumeOverlayWidth, kVolumeOverlayHeight, mint);
  c.rect(kVolumeOverlayX + 1, kVolumeOverlayY + 1,
         kVolumeOverlayWidth - 2, kVolumeOverlayHeight - 2, ink);
  char label[24];
  snprintf(label, sizeof(label), "VOLUME %u/%u", bounded, static_cast<unsigned>(audio_output::kMaxVolume));
  c.centered(174, label, cream);
  c.rect(36, 188, 168, 10, rgb(49, 73, 61));
  c.rect(36, 188, 168 * bounded / audio_output::kMaxVolume, 10, mint);
  c.text(36, 206, "MIN", muted);
  c.text(186, 206, "MAX", muted);
}
} // namespace sloth
