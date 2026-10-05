# Vendored display driver

`esp_lcd_sh8601.c` and `.h` are copied without modifications from:
https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16/tree/294543798f1a44e2f2c4d2976522323f2beee11d/02_Example/Arduino-v3.3.3/09_LVGL_V9_Test/src/externLib

Copyright 2023 Espressif Systems (Shanghai) CO LTD; Apache-2.0 (see LICENSE).
The board-specific CO5300 initialization sequence in `../../board.cpp` is adapted
from the same example's `bsp_lvgl_port.cpp`. No LVGL or XPowers library is required.
