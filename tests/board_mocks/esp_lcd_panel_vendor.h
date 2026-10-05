#pragma once
#include "esp_lcd_panel_io.h"
using esp_lcd_panel_handle_t = void*;
constexpr int LCD_RGB_ELEMENT_ORDER_RGB = 0;
struct esp_lcd_panel_dev_config_t {
  int reset_gpio_num, rgb_ele_order, bits_per_pixel;
  const void* vendor_config;
};
