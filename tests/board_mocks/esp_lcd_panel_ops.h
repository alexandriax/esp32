#pragma once
#include "esp_lcd_panel_vendor.h"
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void*);
