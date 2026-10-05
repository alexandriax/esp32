// Waveshare ESP32-C6-Touch-AMOLED-2.16 CO5300 display for Doom.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
#define DOOM_VIEW_HEIGHT 360
#define STRIP_ROWS 16

void display_init(void);
void display_fill(uint16_t rgb565);
void display_set_backlight(uint8_t brightness);
void display_sleep(bool sleep);
void display_set_viewport(int x, int y, int w, int h);
void display_draw_controls(void);
uint16_t *display_acquire_strip(void);
void display_submit_strip(int y0, int nrows);
void display_wait_done(void);
extern uint32_t display_wait_us;

#ifdef __cplusplus
}
#endif
