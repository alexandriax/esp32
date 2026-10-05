#include "../stripe_sink.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using moss_lhp::StripeSink;

struct Panel {
  unsigned calls = 0, nextY = 0;
  bool fail = false;
  bool checkColors = true;
};

static bool writePanel(void* opaque, unsigned y, unsigned width, unsigned height,
                       const uint8_t* pixels, size_t bytes) {
  Panel& panel = *static_cast<Panel*>(opaque);
  assert(y == panel.nextY);
  assert(width == 480 && height == 2 && bytes == 1920);
  assert(!(y & 1) && y + height <= 480);
  assert(bytes <= 480 * 16 * 2);  // board::presentRegion's DMA capacity
  ++panel.calls;
  if (panel.fail) return false;
  if (panel.checkColors) {
    const uint8_t expected[] = {0x00,0xf8, 0xe0,0x07, 0x1f,0x00,
                               0xff,0xff, 0x00,0x00};
    assert(!memcmp(pixels, expected, sizeof(expected)));
    assert(!memcmp(pixels + 960, expected, sizeof(expected)));
    for (unsigned x = 5; x < 480; ++x) {
      assert(pixels[2*x] == 0 && pixels[2*x+1] == 0);
      assert(pixels[960+2*x] == 0 && pixels[960+2*x+1] == 0);
    }
  }
  panel.nextY += 2;
  return true;
}

int main() {
  uint8_t row[StripeSink::kRowBytes] = {};
  const uint8_t colors[] = {255,0,0, 0,255,0, 0,0,255, 255,255,255, 0,0,0};
  memcpy(row, colors, sizeof(colors));
  Panel panel;
  StripeSink sink(writePanel, &panel);
  static_assert(sizeof(StripeSink) < 2048, "Keep scanline adapter below 2 KiB");
  assert(!sink.row(0, row, sizeof(row)));
  assert(sink.begin());
  for (unsigned y = 0; y < 480; ++y) {
    assert(sink.row(y, row, sizeof(row)));
    assert(panel.calls == (y + 1) / 2);
  }
  assert(sink.finish());
  assert(sink.state() == StripeSink::State::Complete);
  assert(sink.rowsPresented() == 480 && panel.calls == 240);
  assert(!sink.row(480, row, sizeof(row)));
  assert(!sink.finish());

  panel = Panel(); panel.nextY = 64;
  assert(sink.begin(64, 416));
  for (unsigned y = 0; y < 416; ++y) assert(sink.row(y, row, sizeof(row)));
  assert(sink.finish() && panel.nextY == 480);

  assert(!sink.begin(1, 478));
  assert(!sink.begin(0, 479));
  assert(!sink.begin(480, 2));
  assert(!sink.begin(0, 0));
  assert(!sink.begin(~0u, 2));
  assert(!sink.begin(0, ~0u));
  StripeSink missingWriter(nullptr, nullptr);
  assert(!missingWriter.begin());

  panel = Panel();
  assert(sink.begin(0, 2));
  assert(!sink.row(1, row, sizeof(row)));
  assert(sink.state() == StripeSink::State::Failed && panel.calls == 0);
  assert(!sink.row(0, row, sizeof(row)));
  assert(sink.begin(0, 2));
  assert(!sink.row(0, nullptr, sizeof(row)));
  assert(sink.begin(0, 2));
  assert(!sink.row(0, row, sizeof(row) - 1));
  assert(sink.begin(0, 2));
  assert(!sink.row(0, row, sizeof(row) + 1));
  assert(sink.begin(0, 2));
  assert(sink.row(0, row, sizeof(row)));
  assert(!sink.row(0, row, sizeof(row)));  // duplicate row
  assert(panel.calls == 0);

  assert(sink.begin(0, 2));
  assert(sink.row(0, row, sizeof(row)));
  assert(!sink.finish());  // never publish an incomplete row pair
  assert(panel.calls == 0);
  assert(sink.begin(0, 2));
  assert(sink.row(0, row, sizeof(row)));
  sink.cancel();
  assert(!sink.row(1, row, sizeof(row)) && !sink.finish());
  assert(sink.state() == StripeSink::State::Cancelled && panel.calls == 0);

  panel = Panel(); panel.fail = true;
  assert(sink.begin(0, 4));
  assert(sink.row(0, row, sizeof(row)));
  assert(!sink.row(1, row, sizeof(row)));
  assert(sink.rowsAccepted() == 2 && sink.rowsPresented() == 0);
  assert(!sink.row(2, row, sizeof(row)) && !sink.finish());
  assert(panel.calls == 1);

  // A new render after cancellation/failure cannot reuse stale first-row pixels.
  panel = Panel(); panel.checkColors = false;
  memset(row, 0, sizeof(row));
  assert(sink.begin(0, 2));
  assert(sink.row(0, row, sizeof(row)) && sink.row(1, row, sizeof(row)));
  assert(sink.finish() && sink.rowsPresented() == 2);
  puts("stripe_sink: all tests passed");
}
