#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "board.h"
#include "stripe_sink.h"
#include "lhp_engine.h"

static bool writeStripe(void*, unsigned y, unsigned width, unsigned height,
                        const uint8_t* pixels, size_t bytes) {
  return board::presentRegion(0, y, width, height, pixels, bytes);
}
static moss_lhp::StripeSink sink(writeStripe, nullptr);
extern "C" int moss_panel_begin(void) { return sink.begin(); }
extern "C" int moss_panel_line(unsigned y, const uint8_t* rgb, size_t bytes) {
  return sink.row(y, rgb, bytes);
}
extern "C" int moss_panel_finish(void) { return sink.finish(); }

static bool running = false, buttonRaw = HIGH, buttonStable = HIGH;
static bool cancelRaw = HIGH, cancelStable = HIGH;
static uint32_t buttonChanged = 0, cancelChanged = 0, renderStarted = 0;
static void memory(const char* stage) {
  Serial.printf("LHP stage=%s heap=%lu min_heap=%lu largest_block=%lu lws_live=%u lws_peak=%u allocation_failures=%u\n",
      stage, static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(ESP.getMinFreeHeap()),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
      static_cast<unsigned>(moss_lhp_live_bytes()), static_cast<unsigned>(moss_lhp_peak_bytes()),
      static_cast<unsigned>(moss_lhp_failures()));
}
static void startFixture() {
  sink.cancel();
  memory("before");
  renderStarted = millis();
  running = moss_lhp_start() == 0;
  if (!running) { Serial.println("LHP start failed"); moss_lhp_stop(); memory("failed"); }
}
void setup() {
  Serial.begin(115200); // Keep the core's small default receive buffer.
  delay(700);
  Serial.println("Moss LHP C6 experiment: offline fixture, no Wi-Fi, no flashing from build tool");
  board::begin();
  // LWS uses loopback sockets for event-loop wakeups; this starts lwIP only.
  // No Wi-Fi driver, credentials, or external connection is initialized.
  ESP_ERROR_CHECK(esp_netif_init());
  // Region-only rendering cannot satisfy board::showDisplay's full-frame wake
  // contract. This first experiment leaves the panel awake.
  buttonRaw = buttonStable = digitalRead(board::kKeyPin);
  cancelRaw = cancelStable = digitalRead(board::kBootPin);
  cancelChanged = buttonChanged = millis();
  startFixture();
}
void loop() {
  if (running) {
    const int result = moss_lhp_service();
    if (result) {
      Serial.printf("LHP result=%s rows=%u elapsed_ms=%lu\n", result > 0 ? "complete" : "failed",
          sink.rowsPresented(), static_cast<unsigned long>(millis() - renderStarted));
      memory("rendered");
      moss_lhp_stop(); running = false; memory("released");
    }
  }
  const uint32_t now = millis();
  const bool pressed = digitalRead(board::kKeyPin);
  if (pressed != buttonRaw) { buttonRaw = pressed; buttonChanged = now; }
  if (now - buttonChanged >= 35 && buttonRaw != buttonStable) {
    buttonStable = buttonRaw;
    if (!buttonStable && !running) startFixture();
  }
  const bool cancel = digitalRead(board::kBootPin);
  if (cancel != cancelRaw) { cancelRaw = cancel; cancelChanged = now; }
  if (now - cancelChanged >= 35 && cancelRaw != cancelStable) {
    cancelStable = cancelRaw;
    if (!cancelStable && running) {
      moss_lhp_stop(); sink.cancel(); running = false;
      Serial.println("LHP cancelled by BOOT"); memory("cancelled");
    }
  }
  delay(1);
}
