#include "../firmware/sloth_pet/board.h"
#include "../firmware/sloth_pet/jpeg_display.h"
#include "Arduino.h"
#include "Wire.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "../firmware/sloth_pet/src/vendor/esp_lcd_sh8601.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
struct Event { uint32_t command; unsigned value; };
std::vector<Event> events;
ColorCallback colorCallback;
uint8_t pmicRegister;
unsigned pmicByteCount, pmicWrites, stripes;
bool pendingDma, transferReady;
bool busReady = false, panelAttached = false, cardPrepared = false;
bool regionTest = false;
unsigned expectedTurns = 0;
bool rotationTest = false;
bool captureRegion = false;
std::vector<uint16_t> capturedPanel(480*480);
const uint16_t* identitySource = nullptr;
std::vector<uint16_t> expectedRegion;
unsigned regionX, regionY, regionWidth, regionHeight, regionScale;
std::vector<uint64_t> dmaAllocation;
uint16_t* dmaBuffer = nullptr;
size_t dmaCapacity = 0;
const uint8_t* testJpeg = nullptr;
size_t testJpegBytes = 0;
uint16_t* testFrame = nullptr;
constexpr uint32_t command(uint8_t code) { return 0x02000000u | (uint32_t(code) << 8); }
void scratchGuards() {
  assert(dmaAllocation.front() == UINT64_C(0xa5a5a5a5a5a5a5a5));
  const uint8_t* tail = reinterpret_cast<const uint8_t*>(dmaBuffer) + dmaCapacity;
  const uint8_t* end = reinterpret_cast<const uint8_t*>(dmaAllocation.data() + dmaAllocation.size());
  while (tail < end) assert(*tail++ == 0xa5);
}
void expectFailure(void (*operation)()) {
  bool rejected = false;
  try { operation(); } catch (const std::runtime_error&) { rejected = true; }
  assert(rejected);
}
void inputCallback() {
  assert(!pendingDma);
  // A callback occurs after a stripe, but a whole-frame transfer is still open.
  const size_t before = events.size();
  expectFailure(board::sleepDisplay);
  expectFailure(board::wakeDisplay);
  expectFailure(board::showDisplay);
  assert(!board::decodeJpeg(testJpeg, testJpegBytes, testFrame, 240 * 240));
  assert(events.size() == before);
}
}  // namespace

MockSerial Serial;
MockWire Wire;
void pinMode(int, int) {}
void delay(unsigned ms) {
  assert(!pendingDma);
  events.push_back({0, ms});
}
void MockWire::beginTransmission(int) { pmicByteCount = 0; }
void MockWire::write(uint8_t value) {
  if (pmicByteCount++ == 0) pmicRegister = value;
  else ++pmicWrites;
}
int MockWire::endTransmission(bool) { return 0; }
int MockWire::read() { return pmicRegister == 0x03 ? 0x4A : 0; }
esp_err_t spi_bus_initialize(int, const spi_bus_config_t*, int) { busReady = true; return ESP_OK; }
esp_err_t esp_lcd_new_panel_io_spi(int, const esp_lcd_panel_io_spi_config_t* config,
                                  esp_lcd_panel_io_handle_t* out) {
  colorCallback = config->on_color_trans_done;
  assert(busReady && !cardPrepared);
  panelAttached = true;
  *out = reinterpret_cast<void*>(1);
  return ESP_OK;
}
namespace storage_status {
void prepareCard() { assert(busReady && panelAttached && !cardPrepared); cardPrepared = true; }
}
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, uint32_t cmd,
                                   const void* data, size_t size) {
  assert(!pendingDma);
  assert(cardPrepared);
  events.push_back({cmd, size ? static_cast<const uint8_t*>(data)[0] : 0u});
  return ESP_OK;
}
extern "C" esp_err_t esp_lcd_new_panel_sh8601(esp_lcd_panel_io_handle_t,
                                             const esp_lcd_panel_dev_config_t*,
                                             esp_lcd_panel_handle_t* out) {
  *out = reinterpret_cast<void*>(2);
  return ESP_OK;
}
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t) { assert(cardPrepared); return ESP_OK; }
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool on) {
  return esp_lcd_panel_io_tx_param(nullptr, command(on ? 0x29 : 0x28), nullptr, 0);
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int x0, int y0,
                                   int x1, int y1, const void* data) {
  assert(!pendingDma);
  if (captureRegion) {
    assert(x0>=0&&y0>=0&&x1<=480&&y1<=480&&x1>x0&&y1>y0);
    const auto* source=static_cast<const uint8_t*>(data);
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
      const unsigned at=((y-y0)*(x1-x0)+x-x0)*2;
      capturedPanel[y*480+x]=(unsigned(source[at])<<8)|source[at+1];
    }
  } else {
  if (!expectedRegion.empty()) {
    assert(x0 == static_cast<int>(regionX * regionScale));
    assert(y0 == static_cast<int>(regionY * regionScale));
    assert(x1 == static_cast<int>((regionX + regionWidth) * regionScale));
    assert(y1 == static_cast<int>((regionY + regionHeight) * regionScale));
  } else assert(x0 == 0 && x1 == 480 && y1 == y0 + 16);
  if (!regionTest) assert(y0 == static_cast<int>(stripes % 30) * 16);
  if (!expectedRegion.empty()) {
    const auto* actual = static_cast<const uint8_t*>(data);
    for (unsigned row = 0; row < regionHeight * regionScale; ++row)
      for (unsigned column = 0; column < regionWidth * regionScale; ++column) {
        const uint16_t expected = expectedRegion[(row / regionScale) * regionWidth + column / regionScale];
        const unsigned at = (row * regionWidth * regionScale + column) * 2;
        assert(actual[at] == static_cast<uint8_t>(expected >> 8));
        assert(actual[at + 1] == static_cast<uint8_t>(expected));
      }
  } else if (identitySource) {
    const auto* actual = static_cast<const uint8_t*>(data);
    // Independently address every physical destination pixel, including the
    // final row/column; compare panel byte order rather than native uint16_t.
    for (unsigned row = 0; row < 16; ++row) for (unsigned column = 0; column < 480; ++column) {
      const uint16_t expected = identitySource[((static_cast<unsigned>(y0) + row) / 2) * 240 + column / 2];
      const unsigned at = (row * 480 + column) * 2;
      assert(actual[at] == static_cast<uint8_t>(expected >> 8));
      assert(actual[at + 1] == static_cast<uint8_t>(expected));
    }
  } else if (rotationTest) {
    const auto *actual = static_cast<const uint16_t *>(data);
    // Forward-map every source pixel; inspect the corresponding output stripe.
    for (unsigned sy = 0; sy < 240; ++sy) for (unsigned sx = 0; sx < 240; ++sx) {
      unsigned dx = sx, dy = sy;
      for (unsigned turn = 0; turn < expectedTurns; ++turn) {
        const unsigned oldX = dx; dx = 239 - dy; dy = oldX;
      }
      if (dy * 2 < static_cast<unsigned>(y0) || dy * 2 >= static_cast<unsigned>(y1)) continue;
      const uint16_t color = static_cast<uint16_t>(sy * 240 + sx);
      const uint16_t swapped = (color << 8) | (color >> 8);
      const unsigned at = (dy * 2 - y0) * 480 + dx * 2;
      assert(actual[at] == swapped && actual[at + 1] == swapped);
      assert(actual[at + 480] == swapped && actual[at + 481] == swapped);
    }
  } else assert(static_cast<const uint16_t*>(data)[0] == 0x3412);
  }
  ++stripes;
  pendingDma = true;
  // Scratch remains owned until DMA has consumed it, even if decoding would
  // otherwise accept this valid JPEG and overwrite the transfer source.
  assert(!board::decodeJpeg(testJpeg, testJpegBytes, testFrame, 240 * 240));
  events.push_back({command(0x2C), static_cast<unsigned>(y0)});
  return ESP_OK;
}
void* heap_caps_malloc(size_t size, int) {
  assert(size == std::max(size_t(480 * 16 * 2), sloth::jpeg_display::workspaceBytes()));
  assert(sloth::jpeg_display::workspaceAlignment() <= alignof(uint64_t));
  assert(dmaAllocation.empty()); // One allocation serves decoding and every panel stripe.
  dmaAllocation.assign((size + 7) / 8 + 2, UINT64_C(0xa5a5a5a5a5a5a5a5));
  dmaBuffer = reinterpret_cast<uint16_t*>(dmaAllocation.data() + 1);
  dmaCapacity = size;
  return dmaBuffer;
}
SemaphoreHandle_t xSemaphoreCreateBinary() { return reinterpret_cast<void*>(3); }
void xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*) { transferReady = true; }
int xSemaphoreTake(SemaphoreHandle_t, unsigned) {
  assert(pendingDma);
  pendingDma = false;
  colorCallback(nullptr, nullptr, nullptr);
  assert(transferReady);
  transferReady = false;
  scratchGuards();
  return pdTRUE;
}

int main() {
  std::ifstream fixture("tests/fixtures/jpeg240-texture.jpg", std::ios::binary);
  const std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(fixture)), {});
  assert(jpeg.size() == 6636);
  uint16_t pixels[240 * 240];
  testJpeg = jpeg.data(); testJpegBytes = jpeg.size(); testFrame = pixels;
  assert(!board::decodeJpeg(testJpeg, testJpegBytes, pixels, 240 * 240)); // Not initialized.
  board::begin();
  pmicWrites = 0;
  events.clear();
  board::sleepDisplay();
  assert(events.size() == 3);
  assert(events[0].command == command(0x28));
  assert(events[1].command == command(0x10));
  assert(events[2].command == 0 && events[2].value >= 120);
  events.clear();
  board::sleepDisplay(); // Idempotent; repeated sleep never restarts transitions.
  board::brightness(80); // Cache only; no register writes while asleep.
  board::present(nullptr);
  assert(!board::decodeJpeg(testJpeg, testJpegBytes, pixels, 240 * 240)); // Panel asleep.
  expectFailure(board::showDisplay);
  assert(events.empty() && stripes == 0);

  board::wakeDisplay();
  assert(events[0].command == command(0x11));
  assert(events[1].command == 0 && events[1].value == 600);
  assert(events.back().command == command(0x51) && events.back().value == 204);
  bool restoredQspi = false, restoredFormat = false, restoredOrientation = false;
  for (const auto& e : events) {
    assert(e.command != command(0x29)); // Stale frame is never exposed during wake.
    restoredQspi |= e.command == command(0xC4) && e.value == 0x80;
    restoredFormat |= e.command == command(0x3A) && e.value == 0x55;
    restoredOrientation |= e.command == command(0x36) && e.value == 0x30;
  }
  assert(restoredQspi && restoredFormat && restoredOrientation);
  events.clear();
  board::wakeDisplay(); // Already awake, still off pending a fresh frame.
  expectFailure(board::showDisplay);
  assert(events.empty());

  std::fill(pixels, pixels + 240 * 240, 0x1234);
  board::present(pixels, inputCallback);
  assert(stripes == 30 && !pendingDma);
  assert(events.size() == 30);
  board::showDisplay();
  assert(events[30].command == command(0x29));
  assert(events[31].command == 0 && events[31].value == 100);
  events.clear();
  board::showDisplay();
  assert(events.empty());
  rotationTest = true;
  for (unsigned i = 0; i < 240 * 240; ++i) pixels[i] = static_cast<uint16_t>(i);
  for (expectedTurns = 0; expectedTurns < 4; ++expectedTurns)
    board::present(pixels, inputCallback, expectedTurns);
  for(expectedTurns=0;expectedTurns<4;++expectedTurns) {
    board::setScreenRotation(expectedTurns);assert(board::screenRotation()==expectedTurns);
    board::present(pixels,inputCallback); // Same default used by boot, menus and wake.
  }
  expectedTurns=0;board::present(pixels,inputCallback,0); // Pre-rotated host JPEG must bypass saved turns.
  board::setScreenRotation(0);
  rotationTest = false;
  uint32_t random = 0x6a09e667;
  for (unsigned i = 0; i < 240 * 240; ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    pixels[i] = static_cast<uint16_t>(random);
  }
  pixels[0] = 0x0000; pixels[1] = 0xffff;
  pixels[239] = 0xf800; pixels[240] = 0x07e0; pixels[240 * 240 - 1] = 0x001f;
  const std::vector<uint16_t> original(pixels, pixels + 240 * 240);
  identitySource = original.data();
  const unsigned beforeIdentity = stripes;
  board::present(pixels, nullptr, 0); // Same call as a fully decoded JPEG frame.
  board::present(pixels, inputCallback, 4); // Quarter turns retain modulo-four semantics.
  assert(stripes == beforeIdentity + 60 && !pendingDma);
  assert(std::equal(original.begin(), original.end(), pixels));
  identitySource = nullptr;
  uint8_t region[480 * 16 * 2];
  for (unsigned i = 0; i < sizeof(region); i += 2) {
    region[i] = 0x34; region[i + 1] = 0x12;
  }
  regionTest = true;
  assert(board::presentRegion(0, 464, 480, 16, region, sizeof(region)));
  assert(board::presentRegion(0, 232, 240, 8, region, 240 * 8 * 2, 2));
  // Different adjacent source pixels expand across both axes with exact byte order.
  region[2] = 0xCD; region[3] = 0xAB;
  assert(board::presentRegion(0, 0, 240, 8, region, 240 * 8 * 2, 2));
  assert(dmaBuffer[0] == 0x3412 && dmaBuffer[1] == 0x3412);
  assert(dmaBuffer[2] == 0xCDAB && dmaBuffer[3] == 0xCDAB);
  assert(dmaBuffer[480] == dmaBuffer[0] && dmaBuffer[483] == dmaBuffer[3]);
  region[2] = 0x34; region[3] = 0x12;
  const uint8_t nativeRle[] = {0, 30, 0x34, 0x12}; // 7680 pixels.
  const uint8_t scaledRle[] = {0x80, 7, 0x34, 0x12}; // 1920 pixels.
  assert(board::presentRegion(0, 464, 480, 16, nativeRle, sizeof(nativeRle), 1, true));
  assert(board::presentRegion(0, 232, 240, 8, scaledRle, sizeof(scaledRle), 2, true));
  for (unsigned i = 0; i < 480 * 16; ++i) assert(dmaBuffer[i] == 0x3412);
  // A crop with odd logical width/height reaches the exact bottom-right edge.
  // Run B crosses two logical row boundaries; neither row duplication nor run
  // decoding may change its color sequence or touch the unused DMA tail.
  regionX = regionY = 237; regionWidth = regionHeight = 3; regionScale = 2;
  expectedRegion = {0x1020, 0x1020, 0xabcd, 0xabcd, 0xabcd, 0xabcd, 0xabcd, 0xffff, 0xffff};
  std::vector<uint8_t> croppedRaw;
  for (const uint16_t color : expectedRegion) {
    croppedRaw.push_back(static_cast<uint8_t>(color));
    croppedRaw.push_back(static_cast<uint8_t>(color >> 8));
  }
  const uint8_t croppedRle[] = {2, 0, 0x20, 0x10, 5, 0, 0xcd, 0xab, 2, 0, 0xff, 0xff};
  for (const bool encoded : {false, true}) {
    std::fill(dmaBuffer, dmaBuffer + 480 * 16, 0xa55a);
    assert(board::presentRegion(regionX, regionY, regionWidth, regionHeight,
        encoded ? croppedRle : croppedRaw.data(), encoded ? sizeof(croppedRle) : croppedRaw.size(), 2, encoded));
    for (unsigned i = 36; i < 480 * 16; ++i) assert(dmaBuffer[i] == 0xa55a);
    assert(!pendingDma);
  }
  regionX = 476; regionY = 478; regionWidth = 4; regionHeight = 2; regionScale = 1;
  expectedRegion = {0x0000, 0x1234, 0xffff, 0xff00, 0x0102, 0x3344, 0x5566, 0x7788};
  croppedRaw.clear();
  for (const uint16_t color : expectedRegion) {
    croppedRaw.push_back(static_cast<uint8_t>(color));
    croppedRaw.push_back(static_cast<uint8_t>(color >> 8));
  }
  assert(board::presentRegion(regionX, regionY, regionWidth, regionHeight,
      croppedRaw.data(), croppedRaw.size()));
  expectedRegion.clear();
  captureRegion=true;
  // Browser's native strips and scaled UI rows use the same configured transform.
  // Check every touched and untouched physical pixel, including RLE across rows.
  for(unsigned scale:{1u,2u})for(unsigned turns=0;turns<4;++turns)for(bool rle:{false,true}) {
    const unsigned x=scale==1?476:236,y=scale==1?478:238,w=4,h=2;
    const uint16_t colors[]={0x1234,0x1234,0x1234,0xabcd,0xabcd,0xabcd,0xabcd,0xffff};
    std::vector<uint8_t> raw;for(uint16_t c:colors){raw.push_back(c);raw.push_back(c>>8);}
    const uint8_t encoded[]={3,0,0x34,0x12,4,0,0xcd,0xab,1,0,0xff,0xff};
    std::vector<uint16_t> expected(480*480,0xeeee);std::fill(capturedPanel.begin(),capturedPanel.end(),0xeeee);
    for(unsigned sy=0;sy<h*scale;++sy)for(unsigned sx=0;sx<w*scale;++sx) {
      unsigned dx=x*scale+sx,dy=y*scale+sy;
      for(unsigned t=0;t<turns;++t){const unsigned old=dx;dx=479-dy;dy=old;}
      expected[dy*480+dx]=colors[(sy/scale)*w+sx/scale];
    }
    board::setScreenRotation(turns);
    assert(board::presentRegion(x,y,w,h,rle?encoded:raw.data(),rle?sizeof(encoded):raw.size(),scale,rle));
    assert(capturedPanel==expected);
  }
  // Remote rectangles are already rotated by the host, including native mode.
  std::fill(capturedPanel.begin(),capturedPanel.end(),0xeeee);
  assert(board::presentRegion(0,0,2,2,region,8,1,false,0));
  assert(capturedPanel[0]==0x1234&&capturedPanel[479*480+479]==0xeeee);
  board::setScreenRotation(0);captureRegion=false;
  const unsigned beforeInvalid = stripes;
  const uint8_t shortRle[] = {1, 0, 0x34, 0x12};
  const uint8_t zeroRle[] = {0, 0, 0x34, 0x12};
  assert(!board::presentRegion(0, 0, 480, 16, shortRle, 4, 1, true));
  assert(!board::presentRegion(0, 0, 480, 16, zeroRle, 4, 1, true));
  assert(!board::presentRegion(0, 0, 240, 8, nativeRle, 4, 2, true));
  assert(!board::presentRegion(0, 0, 480, 16, nativeRle, 3, 1, true));
  assert(!board::presentRegion(0, 239, 240, 8, scaledRle, 4, 2, true));
  assert(!board::presentRegion(0, 0, 240, 8, scaledRle, 4, 3, true));
  assert(!board::presentRegion(0, 466, 480, 16, region, sizeof(region)));
  assert(!board::presentRegion(1, 0, 480, 16, region, sizeof(region)));
  assert(!board::presentRegion(0, 1, 480, 16, region, sizeof(region)));
  assert(!board::presentRegion(0, 0, 480, 17, region, sizeof(region)));
  assert(!board::presentRegion(0, 0, 480, 32, region, sizeof(region)));
  assert(!board::presentRegion(0, 0, 480, 16, region, sizeof(region) - 1));
  assert(!board::presentRegion(0, 0, 480, 16, nullptr, sizeof(region)));
  assert(stripes == beforeInvalid && !pendingDma);
  board::sleepDisplay();
  assert(!board::presentRegion(0, 0, 480, 16, region, sizeof(region)));
  board::wakeDisplay();
  expectFailure(board::showDisplay); // A previous cycle's frame is insufficient.
  assert(pmicWrites == 0); // Sleep/wake must never toggle reset or shared rails.
  // Decode -> DMA replaces the decoder storage -> decode again. Check every
  // presented pixel against the successful decode's immutable frame, then
  // alternate failed entropy decodes with a normal local-UI redraw.
  regionTest = false;
  size_t sos = 0;
  for (size_t at = 2; at + 4 < jpeg.size();) {
    if (jpeg[at + 1] == 0xda) { sos = at; break; }
    at += 2 + jpeg[at + 2] * 256u + jpeg[at + 3];
  }
  assert(sos);
  std::vector<uint8_t> bad(jpeg.begin(), jpeg.begin() + sos + 14);
  bad.push_back(0xff); bad.push_back(0xd9);
  for (unsigned cycle = 0; cycle < 8; ++cycle) {
    stripes = 0;
    assert(board::decodeJpeg(jpeg.data(), jpeg.size(), pixels, 240 * 240));
    scratchGuards();
    std::vector<uint16_t> decoded(pixels, pixels + 240 * 240);
    identitySource = decoded.data();
    board::present(pixels, inputCallback);
    assert(stripes == 30 && !pendingDma);
    identitySource = nullptr;
    const unsigned beforeFailure = stripes;
    assert(!board::decodeJpeg(bad.data(), bad.size(), pixels, 240 * 240));
    assert(stripes == beforeFailure && !pendingDma);
    scratchGuards();
    std::fill(pixels, pixels + 240 * 240, 0x1234);
    board::present(pixels, inputCallback); // Failed decode released the lease.
    assert(stripes == 60 && !pendingDma);
  }
  // Passing the shared DMA scratch as image output is explicitly rejected.
  assert(!board::decodeJpeg(jpeg.data(), jpeg.size(), dmaBuffer, 240 * 240));
  assert(board::decodeJpeg(jpeg.data(), jpeg.size(), pixels, 240 * 240));
  scratchGuards();
  std::puts("display tests passed: shared JPEG/DMA lifetime and failure recovery, sleep commands, wake delays, DMA guard, fresh frame, brightness, unchanged rails");
}
