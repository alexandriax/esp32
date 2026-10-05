#include "board.h"
#include "board_bus.h"
#include "jpeg_display.h"
#include "screen_rotation.h"
#include "storage_status.h"
#include <Arduino.h>
#include <Wire.h>
#include <cstring>
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "src/vendor/esp_lcd_sh8601.h"

namespace board {
namespace {
esp_lcd_panel_handle_t panel;
esp_lcd_panel_io_handle_t io;
SemaphoreHandle_t transferDone;
uint16_t* stripe;
constexpr size_t kStripePixels = 480 * 16;
size_t scratchBytes = 0;
bool decodingJpeg = false;
uint8_t savedBrightness = 65;
bool panelSleeping = false;
bool panelVisible = true;
bool presenting = false;
unsigned configuredRotation = 0;
bool frameReady = false;

// CO5300 datasheet v0.00, sections 7.5.11–12 (pp.143–146): both sleep
// transitions need 120 ms, with at least 5 ms before another command.
// Keep this board's conservative Waveshare wake/ON delays (600/100 ms).
// https://files.waveshare.com/wiki/common/CO5300_Datasheet_V0.00.pdf
constexpr unsigned kSleepInDelayMs = 120;
constexpr unsigned kSleepOutDelayMs = 600;
constexpr unsigned kDisplayOnDelayMs = 100;

static const uint8_t init_data_0[] = { 0x00 };
static const uint8_t init_data_1[] = { 0x20 };
static const uint8_t init_data_2[] = { 0x10 };
static const uint8_t init_data_3[] = { 0xA0 };
static const uint8_t init_data_4[] = { 0x00 };
static const uint8_t init_data_5[] = { 0x80 };
static const uint8_t init_data_6[] = { 0x55 };
static const uint8_t init_data_7[] = { 0x00 };
static const uint8_t init_data_8[] = { 0x30 };
static const uint8_t init_data_9[] = { 0x20 };
static const uint8_t init_data_10[] = { 0xFF };
static const uint8_t init_data_11[] = { 0xFF };
static const uint8_t init_data_12[] = { 0x00, 0x00, 0x01, 0xDF };
static const uint8_t init_data_13[] = { 0x00, 0x00, 0x01, 0xDF };
static const uint8_t init_data_14[] = { 0x00 };
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
  { 0x11, init_data_0, 0, 600 },
  { 0xFE, init_data_1, 1, 0 },
  { 0x19, init_data_2, 1, 0 },
  { 0x1C, init_data_3, 1, 0 },
  { 0xFE, init_data_4, 1, 0 },
  { 0xC4, init_data_5, 1, 0 },
  { 0x3A, init_data_6, 1, 0 },
  { 0x35, init_data_7, 1, 0 },
  { 0x36, init_data_8, 1, 0 },
  { 0x53, init_data_9, 1, 0 },
  { 0x51, init_data_10, 1, 0 },
  { 0x63, init_data_11, 1, 0 },
  { 0x2A, init_data_12, 4, 0 },
  { 0x2B, init_data_13, 4, 0 },
  { 0x29, init_data_14, 0, 100 },
};

void panelCommand(uint8_t command, const void* data = nullptr, size_t size = 0) {
  // Same 32-bit QSPI command framing as the bundled SH8601 transport.
  const uint32_t encoded = 0x02000000u | (static_cast<uint32_t>(command) << 8);
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, encoded, data, size));
}

void requireIdlePanel() {
  ESP_ERROR_CHECK(panel && !presenting && !decodingJpeg ? ESP_OK : ESP_ERR_INVALID_STATE);
}

uint8_t readPmic(uint8_t reg) {
  Wire.beginTransmission(0x34);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x34, 1) != 1) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }
  return Wire.read();
}

void writePmic(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x34);
  Wire.write(reg);
  Wire.write(value);
  ESP_ERROR_CHECK(Wire.endTransmission() == 0 ? ESP_OK : ESP_FAIL);
}

bool IRAM_ATTR transferComplete(esp_lcd_panel_io_handle_t,
                               esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t wake = pdFALSE;
  xSemaphoreGiveFromISR(transferDone, &wake);
  return wake == pdTRUE;
}
}  // namespace

void begin() {
  pinMode(kKeyPin, INPUT_PULLUP);
  pinMode(kBootPin, INPUT_PULLUP);
  Wire.begin(8, 7, 100000);
  Wire.setTimeOut(100);
  const uint8_t id = readPmic(0x03);
  Serial.printf("PMIC id=0x%02X\n", id);
  Serial.printf("POWER battery_detection=%u battery_present=%u\n",
      readPmic(0x68) & 1, (readPmic(0x00) >> 3) & 1);
  ESP_ERROR_CHECK(id == 0x4A ? ESP_OK : ESP_ERR_NOT_FOUND);
  writePmic(0x94, (readPmic(0x94) & 0xE0) | 28); // ALDO3 = 3.3V.
  const uint8_t rails = readPmic(0x90);
  writePmic(0x90, rails | 0x04);
  delay(100);
  writePmic(0x90, rails & ~0x04);
  delay(100);
  writePmic(0x90, rails | 0x04);
  delay(100);

  transferDone = xSemaphoreCreateBinary();
  // JPEG decode and panel DMA never overlap. Keep the existing stripe geometry
  // but share a single allocation large enough for either temporary use.
  scratchBytes = sloth::jpeg_display::workspaceBytes();
  if (scratchBytes < kStripePixels * 2) scratchBytes = kStripePixels * 2;
  stripe = static_cast<uint16_t*>(heap_caps_malloc(scratchBytes, MALLOC_CAP_DMA));
  ESP_ERROR_CHECK(transferDone && stripe ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(reinterpret_cast<uintptr_t>(stripe) % sloth::jpeg_display::workspaceAlignment() == 0
                      ? ESP_OK : ESP_ERR_INVALID_ARG);

  spi_bus_config_t bus = {};
  bus.sclk_io_num = 0;
  bus.data0_io_num = 1;
  bus.data1_io_num = 2;
  bus.data2_io_num = 3;
  bus.data3_io_num = 4;
  bus.max_transfer_sz = kStripePixels * 2;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t config = {};
  config.cs_gpio_num = 15;
  config.dc_gpio_num = -1;
  config.spi_mode = 0;
  config.pclk_hz = 40000000;
  config.trans_queue_depth = 1;
  config.on_color_trans_done = transferComplete;
  config.lcd_cmd_bits = 32;
  config.lcd_param_bits = 8;
  config.flags.quad_mode = true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &config, &io));
  // A card powers up in native SD mode and may react even with CS high.
  // Establish SPI mode before sending the first LCD command on the shared bus.
  storage_status::prepareCard();

  sh8601_vendor_config_t vendor = {};
  vendor.init_cmds = lcd_init_cmds;
  vendor.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);
  vendor.flags.use_qspi_interface = 1;
  esp_lcd_panel_dev_config_t display = {};
  display.reset_gpio_num = -1;
  display.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  display.bits_per_pixel = 16;
  display.vendor_config = &vendor;
  ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &display, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  brightness(65);
  Serial.println("DISPLAY ready: CO5300 480x480 QSPI");
}

bool decodeJpeg(const uint8_t* data, size_t bytes, uint16_t* pixels, size_t pixelCapacity) {
  if (!panel || !stripe || panelSleeping || presenting || decodingJpeg) return false;
  // Only the main loop enters these APIs; DMA callbacks merely signal a
  // semaphore. Hold ownership across every validation/failure path too.
  struct Lease {
    Lease() { decodingJpeg = true; }
    ~Lease() { decodingJpeg = false; }
  } lease;
  return sloth::jpeg_display::decode(data, bytes, pixels, pixelCapacity, stripe, scratchBytes);
}

void brightness(uint8_t percent) {
  board_bus::Guard bus;
  savedBrightness = min(percent, static_cast<uint8_t>(100));
  if (panelSleeping) return;
  const uint8_t value = (savedBrightness * 255) / 100;
  panelCommand(0x51, &value, 1);
}

void sleepDisplay() {
  board_bus::Guard bus;
  requireIdlePanel();
  if (panelSleeping) return;
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, false));
  panelVisible = false;
  frameReady = false;
  panelCommand(0x10); // SLPIN disables panel scanning, oscillator, and converter.
  delay(kSleepInDelayMs);
  panelSleeping = true;
}

void wakeDisplay() {
  board_bus::Guard bus;
  requireIdlePanel();
  if (!panelSleeping) return;
  panelCommand(0x11); // SLPOUT; DISPOFF remains in force while preparing RAM.
  delay(kSleepOutDelayMs);
  // SLPOUT can reload extended-register defaults. Restore the exact board
  // parameters already used at startup, without sending another SLPOUT/DISPON.
  for (const auto& command : lcd_init_cmds) {
    if (command.cmd == 0x11 || command.cmd == 0x29 || command.cmd == 0x51) continue;
    panelCommand(static_cast<uint8_t>(command.cmd), command.data, command.data_bytes);
    if (command.delay_ms) delay(command.delay_ms);
  }
  panelSleeping = false;
  panelVisible = false;
  frameReady = false;
  const uint8_t value = (savedBrightness * 255) / 100;
  panelCommand(0x51, &value, 1);
}

void showDisplay() {
  board_bus::Guard bus;
  requireIdlePanel();
  if (panelVisible) return;
  ESP_ERROR_CHECK(!panelSleeping && frameReady ? ESP_OK : ESP_ERR_INVALID_STATE);
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
  delay(kDisplayOnDelayMs);
  panelVisible = true;
}

// Optimize pixel conversion loops only; power, initialization, and ISR code
// retain the platform optimization level.
#if defined(__GNUC__) && !defined(__clang__) && defined(MOSS_DISPLAY_SPEED_OPT) && MOSS_DISPLAY_SPEED_OPT
#pragma GCC push_options
#pragma GCC optimize ("O3")
#endif

void setScreenRotation(unsigned turns) {
  requireIdlePanel();
  ESP_ERROR_CHECK(turns < 4 ? ESP_OK : ESP_ERR_INVALID_ARG);
  configuredRotation = turns;
}
unsigned screenRotation() { return configuredRotation; }

void present(const uint16_t* pixels, void (*serviceInputs)(), unsigned turns) {
  if (turns == kConfiguredRotation) turns = configuredRotation;
  board_bus::Guard bus;
  requireIdlePanel();
  if (panelSleeping) return;
  ESP_ERROR_CHECK(pixels ? ESP_OK : ESP_ERR_INVALID_ARG);
  presenting = true;
  frameReady = false;
  for (int y = 0; y < 240; y += 8) {
    if ((turns & 3u) == 0) {
      // Pet frames and host-rotated JPEG frames are already in panel order.
      // Expand a contiguous source row once, then duplicate the completed row.
      for (int row = 0; row < 8; ++row) {
        const uint16_t* source = pixels + (y + row) * 240;
        uint16_t* output = stripe + row * 960;
        for (int x = 0; x < 240; ++x) {
          const uint16_t c = source[x];
          const uint16_t swapped = (c << 8) | (c >> 8);
          output[x * 2] = output[x * 2 + 1] = swapped;
        }
        std::memcpy(output + 480, output, 480 * sizeof(uint16_t));
      }
    } else {
      for (int row = 0; row < 8; ++row) {
        for (int x = 0; x < 240; ++x) {
          const uint16_t c = pixels[sloth::rotatedSource(x, y + row, 240, turns)];
          const uint16_t swapped = (c << 8) | (c >> 8);
          const int at = row * 960 + x * 2;
          stripe[at] = stripe[at + 1] = swapped;
          stripe[at + 480] = stripe[at + 481] = swapped;
        }
      }
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y * 2, 480, y * 2 + 16, stripe));
    // Reuse the DMA buffer only after the peripheral has consumed it.
    ESP_ERROR_CHECK(xSemaphoreTake(transferDone, pdMS_TO_TICKS(1000)) == pdTRUE
                        ? ESP_OK : ESP_ERR_TIMEOUT);
    if (serviceInputs) serviceInputs();
  }
  presenting = false;
  frameReady = true;
}

bool presentRegion(unsigned x, unsigned y, unsigned width, unsigned height,
                   const uint8_t* data, unsigned bytes, unsigned scale, bool rle, unsigned turns) {
  turns = turns == kConfiguredRotation ? configuredRotation : turns & 3u;
  board_bus::Guard bus;
  requireIdlePanel();
  if (panelSleeping || !data || !width || !height || (scale != 1 && scale != 2)) return false;
  const unsigned limit = 480 / scale;
  if (x >= limit || y >= limit || width > limit - x || height > limit - y ||
      (((x | y | width | height) * scale) & 1u) ||
      width * height * scale * scale > kStripePixels) return false;
  const unsigned count = width * height;
  if (rle) {
    if (!bytes || bytes % 4 || bytes > kStripePixels * 2) return false;
    unsigned total = 0;
    for (unsigned at = 0; at < bytes; at += 4) {
      const unsigned run = data[at] | (static_cast<unsigned>(data[at + 1]) << 8);
      if (!run || run > count - total) return false;
      total += run;
    }
    if (total != count) return false;
  } else if (bytes != count * 2) return false;
  presenting = true;
  unsigned at = 0, remaining = 0;
  uint16_t color = 0;
  auto nextColor = [&]() -> uint16_t {
    if (rle) {
      if (!remaining) {
        remaining = data[at] | (static_cast<unsigned>(data[at + 1]) << 8);
        color = (static_cast<uint16_t>(data[at + 2]) << 8) | data[at + 3];
        at += 4;
      }
      --remaining;
    } else {
      color = (static_cast<uint16_t>(data[at]) << 8) | data[at + 1];
      at += 2;
    }
    return color;
  };
  // DMA byte order is panel big-endian. Keep the run cursor across row
  // boundaries, and duplicate completed rows without per-pixel division.
  if (turns) {
    const unsigned w = width * scale, h = height * scale;
    const unsigned outputWidth = (turns & 1u) ? h : w;
    for (unsigned row = 0; row < height; ++row) for (unsigned col = 0; col < width; ++col) {
      const uint16_t pixel = nextColor();
      for (unsigned ry = 0; ry < scale; ++ry) for (unsigned rx = 0; rx < scale; ++rx) {
        const unsigned sx = col * scale + rx, sy = row * scale + ry;
        unsigned dx = sx, dy = sy;
        if (turns == 1) { dx = h - 1 - sy; dy = sx; }
        else if (turns == 2) { dx = w - 1 - sx; dy = h - 1 - sy; }
        else { dx = sy; dy = w - 1 - sx; }
        stripe[dy * outputWidth + dx] = pixel;
      }
    }
  } else if (scale == 1) {
    for (unsigned i = 0; i < count; ++i) stripe[i] = nextColor();
  } else {
    uint16_t* output = stripe;
    for (unsigned row = 0; row < height; ++row) {
      for (unsigned column = 0; column < width; ++column) {
        const uint16_t expanded = nextColor();
        output[column * 2] = output[column * 2 + 1] = expanded;
      }
      std::memcpy(output + width * 2, output, width * 2 * sizeof(uint16_t));
      output += width * 4;
    }
  }
  unsigned px = x * scale, py = y * scale, pw = width * scale, ph = height * scale;
  for (unsigned turn = 0; turn < turns; ++turn) {
    const unsigned oldX = px, oldWidth = pw;
    px = 480 - py - ph; py = oldX; pw = ph; ph = oldWidth;
  }
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, px, py, px + pw, py + ph, stripe));
  ESP_ERROR_CHECK(xSemaphoreTake(transferDone, pdMS_TO_TICKS(1000)) == pdTRUE
                      ? ESP_OK : ESP_ERR_TIMEOUT);
  presenting = false;
  // A region alone cannot authorize showing a panel that lacks a fresh frame.
  return true;
}

#if defined(__GNUC__) && !defined(__clang__) && defined(MOSS_DISPLAY_SPEED_OPT) && MOSS_DISPLAY_SPEED_OPT
#pragma GCC pop_options
#endif
}  // namespace board
