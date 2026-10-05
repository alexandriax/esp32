#include <esp_wifi.h>

// Arduino's default permits 32 dynamic RX and TX packets each. On the C6
// without PSRAM that burst can consume the foreground app's remaining heap.
// Apply one limit to Arduino station joins and our raw passive scans.
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t*);
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t* requested) {
  if (!requested) return __real_esp_wifi_init(requested);
  wifi_init_config_t bounded = *requested;
  if (bounded.dynamic_rx_buf_num == 0 || bounded.dynamic_rx_buf_num > 8)
    bounded.dynamic_rx_buf_num = 8;
  if (bounded.tx_buf_type == 1 && bounded.dynamic_tx_buf_num > 8)
    bounded.dynamic_tx_buf_num = 8;
  return __real_esp_wifi_init(&bounded);
}
