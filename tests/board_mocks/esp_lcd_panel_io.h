#pragma once
#include "Arduino.h"
using esp_lcd_panel_io_handle_t = void*;
struct esp_lcd_panel_io_event_data_t {};
using ColorCallback = bool (*)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*);
struct esp_lcd_panel_io_spi_config_t {
  int cs_gpio_num, dc_gpio_num, spi_mode, pclk_hz, trans_queue_depth;
  ColorCallback on_color_trans_done;
  int lcd_cmd_bits, lcd_param_bits;
  struct { bool quad_mode; } flags;
};
esp_err_t esp_lcd_new_panel_io_spi(int, const esp_lcd_panel_io_spi_config_t*, esp_lcd_panel_io_handle_t*);
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, uint32_t, const void*, size_t);
