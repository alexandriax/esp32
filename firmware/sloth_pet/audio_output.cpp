#include "audio_output.h"
#include <Arduino.h>
#include <Wire.h>
#include <new>
#include "driver/i2s_std.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace audio_output {
namespace {
// Waveshare ESP32-C6-Touch-AMOLED-2.16 schematic, codec/PA block:
// https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf
// ES8311 register definitions: https://files.waveshare.com/wiki/common/ES8311.DS.pdf
// Clock/power sequence informed by Espressif's Apache-2.0 ES8311 driver in
// Waveshare commit 294543798f1a44e2f2c4d2976522323f2beee11d, 07_Audio_Test.
// Driver copyright 2023 Espressif Systems (Shanghai) CO LTD; Apache-2.0,
// see src/vendor/LICENSE. Register values are restricted to our fixed format.
// Fixed output-only configuration; ALDO1 powers codec AVDD and the unused
// external ADC analog rail. Save/restore its prior state; never configure ADC/RX.
constexpr uint8_t kCodec = 0x18, kPmic = 0x34;
constexpr size_t kChunkSamples = 256; // 16 ms; four DMA descriptors = 64 ms.
AudioBuffer* buffer = nullptr;
i2s_chan_handle_t tx = nullptr;
TaskHandle_t task = nullptr;
SemaphoreHandle_t finished = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool running = false, stopping = false, txEnabled = false, codecTouched = false;
uint32_t writeErrors = 0;
bool analogRailSaved = false, analogRailWasEnabled = false;
uint8_t analogRailVoltage = 0;

class BusTimeout {
 public:
  BusTimeout() : previous_(Wire.getTimeOut()) { Wire.setTimeOut(10); }
  ~BusTimeout() { Wire.setTimeOut(previous_); }
 private: uint16_t previous_;
};
bool writeReg(uint8_t address, uint8_t reg, uint8_t value) {
  BusTimeout timeout;
  Wire.beginTransmission(address); Wire.write(reg); Wire.write(value);
  return Wire.endTransmission(true) == 0;
}
bool readReg(uint8_t address, uint8_t reg, uint8_t& value) {
  BusTimeout timeout;
  Wire.beginTransmission(address); Wire.write(reg);
  if (Wire.endTransmission(false) || Wire.requestFrom(address, size_t(1), true) != 1) return false;
  const int read = Wire.read();
  if (read < 0) return false;
  value = static_cast<uint8_t>(read); return true;
}
bool amplifier(bool enabled) {
  uint8_t rails = 0;
  if (!readReg(kPmic, 0x90, rails)) return false;
  return writeReg(kPmic, 0x90, enabled ? (rails | 0x02) : (rails & ~0x02));
}
bool prepareAnalogRail() {
  uint8_t rails = 0, voltage = 0;
  if (!readReg(kPmic, 0x90, rails) || !readReg(kPmic, 0x92, voltage)) return false;
  analogRailVoltage = voltage;
  analogRailWasEnabled = (rails & 0x01) != 0;
  analogRailSaved = true; // Roll back even if a subsequent write fails.
  if (!writeReg(kPmic, 0x92, (voltage & 0xe0) | 28) ||
      !writeReg(kPmic, 0x90, rails | 0x01)) return false;
  delay(5); // Settle AVDD before reading the codec ID or programming registers.
  return true;
}
void restoreAnalogRail() {
  if (!analogRailSaved) return;
  uint8_t rails = 0;
  if (!readReg(kPmic, 0x90, rails)) return; // Keep snapshot for a later stop retry.
  if (!analogRailWasEnabled && !writeReg(kPmic, 0x90, rails & ~0x01)) return;
  if (!writeReg(kPmic, 0x92, analogRailVoltage)) return;
  if (analogRailWasEnabled && !writeReg(kPmic, 0x90, rails | 0x01)) return;
  analogRailSaved = false;
}
bool codecStart() {
  uint8_t id1 = 0, id2 = 0, volts = 0;
  if (!amplifier(false) || !prepareAnalogRail() ||
      !readReg(kCodec, 0xfd, id1) || !readReg(kCodec, 0xfe, id2) ||
      id1 != 0x83 || id2 != 0x11 || !readReg(kPmic, 0x93, volts) ||
      !writeReg(kPmic, 0x93, (volts & 0xe0) | 28)) return false; // ALDO2 3.3 V.
  codecTouched = true;
  struct Register { uint8_t address, value; };
  static const Register setup[] = {
    {0x44,0x08}, {0x44,0x08}, // I2C noise immunity, first-write retry per vendor.
    {0x01,0x30}, {0x02,0x00}, {0x04,0x20}, {0x05,0x00},
    {0x06,0x03}, {0x07,0x10}, {0x08,0xff}, // 4.096 MHz MCLK / 256; ADC output tristated.
    {0x09,0x4c}, {0x0a,0x4c}, // 16-bit Philips; both serial ports initially muted.
    {0x0b,0x00}, {0x0c,0x00}, {0x10,0x1f}, {0x11,0x7f},
    {0x00,0x82}, // Slave DAC enabled; ADC digital block held in reset.
    {0x01,0x35}, // MCLK/BCLK + DAC clocks only; ADC clocks disabled.
    {0x0e,0x62}, // ADC PGA and modulator powered down.
    {0x12,0x00}, {0x13,0x10}, {0x14,0x00}, // DAC output, no microphone selected.
    {0x0d,0x31}, // Analog/DAC bias on; ADC bias/reference off.
    {0x31,0x60}, {0x32,0x00}, {0x37,0x08}, {0x45,0x00},
    {0x09,0x0c} // Unmute only DAC serial input; volume remains muted.
  };
  for (const auto& reg : setup) if (!writeReg(kCodec, reg.address, reg.value)) return false;
  return true;
}
void worker(void*) {
  int16_t samples[kChunkSamples];
  for (;;) {
    portENTER_CRITICAL(&mux);
    const bool quit = stopping;
    if (!quit) buffer->render(samples, kChunkSamples);
    portEXIT_CRITICAL(&mux);
    if (quit) break;
    size_t written = 0;
    const esp_err_t result = i2s_channel_write(tx, samples, sizeof(samples), &written, 40);
    if (result != ESP_OK || written != sizeof(samples)) {
      portENTER_CRITICAL(&mux); ++writeErrors; portEXIT_CRITICAL(&mux);
      // Auto-clear DMA prevents replaying stale sound. Don't spin on failure.
      vTaskDelay(pdMS_TO_TICKS(16));
    }
  }
  xSemaphoreGive(finished);
  vTaskDelete(nullptr); // No access to shared buffers/channel after signaling.
}
} // namespace

bool start(uint8_t volumePercent) {
  if (active()) return setVolume(volumePercent);
  stop();
  if (task || analogRailSaved) return false; // Previous cleanup must finish before restart.
  buffer = new (std::nothrow) AudioBuffer;
  finished = xSemaphoreCreateBinary();
  if (!buffer || !finished || !codecStart()) { stop(); return false; }
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  channel.dma_desc_num = 4; channel.dma_frame_num = kChunkSamples;
  channel.auto_clear_after_cb = true;
  if (i2s_new_channel(&channel, &tx, nullptr) != ESP_OK) { stop(); return false; }
  i2s_std_config_t config = {};
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  config.gpio_cfg.mclk = GPIO_NUM_19; config.gpio_cfg.bclk = GPIO_NUM_20;
  config.gpio_cfg.ws = GPIO_NUM_22; config.gpio_cfg.dout = GPIO_NUM_23;
  config.gpio_cfg.din = I2S_GPIO_UNUSED; // ES7210/DIN21 is never configured.
  if (i2s_channel_init_std_mode(tx, &config) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
    stop(); return false;
  }
  txEnabled = true;
  portENTER_CRITICAL(&mux); stopping = false; writeErrors = 0; portEXIT_CRITICAL(&mux);
  if (xTaskCreate(worker, "moss-audio", 3072, nullptr, 2, &task) != pdPASS) {
    task = nullptr; stop(); return false;
  }
  running = true;
  if (!setVolume(volumePercent)) { stop(); return false; }
  return true;
}
void stop() {
  running = false;
  // Only the main loop touches Wire. Mute before waiting for worker completion.
  amplifier(false); // Also safe as a one-time boot call to establish audio off.
  if (codecTouched) {
    writeReg(kCodec, 0x31, 0x60);
  }
  if (task) {
    portENTER_CRITICAL(&mux); stopping = true; portEXIT_CRITICAL(&mux);
    if (xSemaphoreTake(finished, pdMS_TO_TICKS(500)) != pdTRUE) return;
    task = nullptr;
  }
  if (tx) {
    if (txEnabled) i2s_channel_disable(tx);
    i2s_del_channel(tx); tx = nullptr; txEnabled = false;
  }
  if (codecTouched) {
    writeReg(kCodec, 0x09, 0x4c); writeReg(kCodec, 0x12, 0x02);
    writeReg(kCodec, 0x0d, 0xfc); writeReg(kCodec, 0x00, 0x1f);
    writeReg(kCodec, 0x01, 0x00); codecTouched = false;
  }
  restoreAnalogRail();
  delete buffer; buffer = nullptr;
  if (finished) { vSemaphoreDelete(finished); finished = nullptr; }
}
bool setVolume(uint8_t volumePercent) {
  if (!running) return false;
  if (volumePercent > kMaxVolume) volumePercent = kMaxVolume;
  // Conservative codec ceiling -12 dB (0xa7), never positive digital gain.
  // 1..60 maps -41.5..-12 dB; zero explicitly mutes and disables the PA.
  const uint8_t level = volumePercent ? static_cast<uint8_t>(107 + volumePercent) : 0;
  return writeReg(kCodec, 0x32, level) &&
         writeReg(kCodec, 0x31, volumePercent ? 0x00 : 0x60) && amplifier(volumePercent != 0);
}
bool pushPcm(const uint8_t* data, size_t bytes) {
  if (!running || !validPcm(data, bytes)) return false;
  portENTER_CRITICAL(&mux); const bool ok = buffer->push(data, bytes); portEXIT_CRITICAL(&mux);
  return ok;
}
bool active() { return running; }
Stats stats() {
  portENTER_CRITICAL(&mux);
  Stats result = buffer ? buffer->stats() : Stats{};
  result.writeErrors = writeErrors;
  portEXIT_CRITICAL(&mux);
  return result;
}
} // namespace audio_output
