idf_component_register(
    SRCS "src/display.c" "src/esp_lcd_sh8601.c"
    INCLUDE_DIRS "include" "src"
    REQUIRES driver esp_timer esp_lcd fatfs sdmmc
)
