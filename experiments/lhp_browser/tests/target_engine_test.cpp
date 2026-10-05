// Exercise the actual target C engine and its panel adapter on the host.
// This verifies lifecycle and pixel flow, not ESP32 timing, RAM, or hardware.
#include "../target/lhp_engine.h"
#include "../stripe_sink.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct Panel {
  unsigned nextRow = 0, calls = 0, darkPixels = 0;
  unsigned failAtRow = 480;
  uint32_t hash = 2166136261u;
  FILE* preview = nullptr;
};
static Panel panel;
extern "C" size_t moss_probe_network_attempts;
static bool writeStripe(void*, unsigned y, unsigned width, unsigned height,
                        const uint8_t* pixels, size_t bytes) {
  assert(y == panel.nextRow && width == 480 && height == 2 && bytes == 1920);
  if (y >= panel.failAtRow) return false;
  for (size_t i = 0; i < bytes; i += 2) {
    const unsigned v = pixels[i] | (static_cast<unsigned>(pixels[i + 1]) << 8);
    if (((v >> 11) & 31) < 16 && ((v >> 5) & 63) < 32 && (v & 31) < 16) ++panel.darkPixels;
  }
  for (size_t i = 0; i < bytes; ++i) panel.hash = (panel.hash ^ pixels[i]) * 16777619u;
  if (panel.preview) {
    for (size_t i = 0; i < bytes; i += 2) {
      const unsigned v = pixels[i] | (static_cast<unsigned>(pixels[i + 1]) << 8);
      const uint8_t rgb[] = {static_cast<uint8_t>(((v >> 11) & 31) * 255 / 31),
                            static_cast<uint8_t>(((v >> 5) & 63) * 255 / 63),
                            static_cast<uint8_t>((v & 31) * 255 / 31)};
      assert(fwrite(rgb, 1, sizeof(rgb), panel.preview) == sizeof(rgb));
    }
  }
  panel.nextRow += height;
  ++panel.calls;
  return true;
}
static moss_lhp::StripeSink sink(writeStripe, nullptr);
extern "C" int moss_panel_begin(void) { return sink.begin(); }
extern "C" int moss_panel_line(unsigned y, const uint8_t* rgb, size_t bytes) {
  return sink.row(y, rgb, bytes);
}
extern "C" int moss_panel_finish(void) { return sink.finish(); }

int main(int argc, char** argv) {
  assert(argc <= 2);
  uint32_t expectedHash = 0;
  assert(moss_lhp_service() == -1);
  moss_lhp_stop();
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    panel = Panel();
    if (!attempt && argc == 2) {
      panel.preview = fopen(argv[1], "wb");
      assert(panel.preview);
      assert(fprintf(panel.preview, "P6\n480 480\n255\n") > 0);
    }
    assert(moss_lhp_start() == 0);
    assert(moss_lhp_start() == -1);  // cannot replace an active context
    int status = 0;
    while (!(status = moss_lhp_service())) {}
    assert(status == 1 && sink.state() == moss_lhp::StripeSink::State::Complete);
    assert(panel.nextRow == 480 && panel.calls == 240 && panel.darkPixels > 100);
    assert(moss_lhp_failures() == 0 && moss_lhp_peak_bytes() > 0);
    if (!attempt) expectedHash = panel.hash;
    else assert(panel.hash == expectedHash);
    if (panel.preview) assert(fclose(panel.preview) == 0);
    panel.preview = nullptr;
    printf("target fixture: pass %u, rows=%u, stripe_hash=%08x, native_lws_peak=%zu\n",
           attempt + 1, panel.nextRow, panel.hash, moss_lhp_peak_bytes());
    moss_lhp_stop();
    assert(moss_lhp_live_bytes() == 0);
    moss_lhp_stop();  // cleanup is idempotent
  }
  // Cancellation before parsing and after one service turn must free context,
  // VFS registration, pending timers and document ownership, then allow restart.
  for (unsigned step = 0; step < 2; ++step) {
    panel = Panel();
    assert(moss_lhp_start() == 0);
    if (step) (void)moss_lhp_service();
    moss_lhp_stop(); sink.cancel();
    assert(moss_lhp_live_bytes() == 0 && moss_lhp_service() == -1);
  }
  panel = Panel();
  assert(moss_lhp_start() == 0);
  int status = 0;
  while (!(status = moss_lhp_service())) {}
  assert(status == 1 && panel.hash == expectedHash);
  moss_lhp_stop();
  assert(moss_lhp_live_bytes() == 0);
  panel = Panel(); panel.failAtRow = 32;
  assert(moss_lhp_start() == 0);
  status = 0;
  while (!(status = moss_lhp_service())) {}
  assert(status == -1 && panel.nextRow == 32);
  assert(sink.state() == moss_lhp::StripeSink::State::Failed);
  moss_lhp_stop();
  assert(moss_lhp_live_bytes() == 0 && moss_probe_network_attempts == 0);
  puts("target engine lifecycle: all tests passed (host, no device)");
}
