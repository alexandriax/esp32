// CO5300 QSPI AMOLED transport for the Waveshare ESP32-C6-Touch-AMOLED-2.16.
// The Doom renderer supplies pre-swapped RGB565 strips; this component never
// allocates a full screen framebuffer.
#include "display.h"
#include "esp_lcd_sh8601.h"
#include "font8x8.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include <assert.h>
#include <string.h>

#define STRIP_BYTES (DISPLAY_WIDTH * STRIP_ROWS * 2)
static const char *TAG = "DOOM_DISPLAY";
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t done;
static uint16_t *strips[2];
static int current;
static bool pending;
static int vx, vy, vw = DISPLAY_WIDTH, vh = DISPLAY_HEIGHT;
uint32_t display_wait_us;

static const uint8_t p10[] = {0x10}, pa0[] = {0xA0}, p80[] = {0x80};
static const uint8_t p55[] = {0x55}, p00[] = {0x00}, p30[] = {0x30};
static const uint8_t p20[] = {0x20}, pff[] = {0xFF};
static const uint8_t full_range[] = {0x00, 0x00, 0x01, 0xDF};
static const sh8601_lcd_init_cmd_t init_commands[] = {
    {0x11, NULL, 0, 600}, {0xFE, p20, 1, 0}, {0x19, p10, 1, 0},
    {0x1C, pa0, 1, 0}, {0xFE, p00, 1, 0}, {0xC4, p80, 1, 0},
    {0x3A, p55, 1, 0}, {0x35, p00, 1, 0}, {0x36, p30, 1, 0},
    {0x53, p20, 1, 0}, {0x51, pff, 1, 0}, {0x63, pff, 1, 0},
    {0x2A, full_range, 4, 0}, {0x2B, full_range, 4, 0},
    {0x29, NULL, 0, 100},
};

static bool IRAM_ATTR transfer_done(esp_lcd_panel_io_handle_t bus,
                                    esp_lcd_panel_io_event_data_t *event, void *arg)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(done, &wake);
    return wake == pdTRUE;
}

static void command(uint8_t cmd, const void *data, size_t size)
{
    uint32_t encoded = 0x02000000u | ((uint32_t)cmd << 8);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, encoded, data, size));
}

static uint16_t wire(uint16_t color) { return (uint16_t)((color << 8) | (color >> 8)); }

// The SD socket shares CLK and two data pins with QSPI. A card left in native
// mode can respond despite CS high, so put it in SPI mode before LCD traffic.
static void prepare_card(void)
{
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_6, 1);
    if (sdspi_host_init() != ESP_OK) return;
    sdspi_device_config_t cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    cfg.host_id = SPI2_HOST;
    cfg.gpio_cs = GPIO_NUM_6;
    cfg.gpio_cd = SDSPI_SLOT_NO_CD;
    cfg.gpio_wp = SDSPI_SLOT_NO_WP;
    sdspi_dev_handle_t device;
    if (sdspi_host_init_device(&cfg, &device) == ESP_OK) {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = device;
        host.max_freq_khz = 10000;
        sdmmc_card_t card = {0};
        sdmmc_card_init(&host, &card);
        sdspi_host_remove_device(device);
    }
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_6, 1);
}

void display_init(void)
{
    done = xSemaphoreCreateBinary();
    strips[0] = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA);
    strips[1] = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA);
    ESP_ERROR_CHECK(done && strips[0] && strips[1] ? ESP_OK : ESP_ERR_NO_MEM);

    spi_bus_config_t bus = {0};
    bus.sclk_io_num = 0;
    bus.data0_io_num = 1;
    bus.data1_io_num = 2;
    bus.data2_io_num = 3;
    bus.data3_io_num = 4;
    bus.max_transfer_sz = STRIP_BYTES;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    prepare_card();

    esp_lcd_panel_io_spi_config_t cfg = {0};
    cfg.cs_gpio_num = 15;
    cfg.dc_gpio_num = -1;
    cfg.pclk_hz = 40000000;
    cfg.trans_queue_depth = 1;
    cfg.on_color_trans_done = transfer_done;
    cfg.lcd_cmd_bits = 32;
    cfg.lcd_param_bits = 8;
    cfg.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &cfg, &io));

    sh8601_vendor_config_t vendor = {0};
    vendor.init_cmds = init_commands;
    vendor.init_cmds_size = sizeof(init_commands) / sizeof(init_commands[0]);
    vendor.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t dev = {0};
    dev.reset_gpio_num = -1;
    dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    dev.bits_per_pixel = 16;
    dev.vendor_config = &vendor;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &dev, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    display_set_backlight(165);
    display_fill(0);
    ESP_LOGI(TAG, "480x480 CO5300 ready, 2 x %u-byte strips", STRIP_BYTES);
}

void display_wait_done(void)
{
    if (!pending) return;
    int64_t start = esp_timer_get_time();
    ESP_ERROR_CHECK(xSemaphoreTake(done, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT);
    display_wait_us += (uint32_t)(esp_timer_get_time() - start);
    pending = false;
}

void display_set_viewport(int x, int y, int w, int h)
{
    ESP_ERROR_CHECK(x >= 0 && y >= 0 && w > 0 && h > 0 &&
                    x + w <= DISPLAY_WIDTH && y + h <= DISPLAY_HEIGHT
                    ? ESP_OK : ESP_ERR_INVALID_ARG);
    display_wait_done();
    vx = x; vy = y; vw = w; vh = h;
}

uint16_t *display_acquire_strip(void) { return strips[current]; }

void display_submit_strip(int y0, int nrows)
{
    ESP_ERROR_CHECK(y0 >= 0 && nrows > 0 && nrows <= STRIP_ROWS && y0 + nrows <= vh
                    ? ESP_OK : ESP_ERR_INVALID_ARG);
    display_wait_done();
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, vx, vy + y0, vx + vw, vy + y0 + nrows, strips[current]));
    pending = true;
    current ^= 1;
}

void display_fill(uint16_t rgb565)
{
    display_wait_done();
    display_set_viewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    const uint16_t value = wire(rgb565);
    for (int y = 0; y < DISPLAY_HEIGHT; y += STRIP_ROWS) {
        uint16_t *row = display_acquire_strip();
        for (unsigned i = 0; i < DISPLAY_WIDTH * STRIP_ROWS; ++i) row[i] = value;
        display_submit_strip(y, STRIP_ROWS);
    }
    display_wait_done();
}

void display_set_backlight(uint8_t brightness)
{
    display_wait_done();
    command(0x51, &brightness, 1);
}

void display_sleep(bool sleeping)
{
    display_wait_done();
    if (sleeping) {
        command(0x28, NULL, 0);
        command(0x10, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        command(0x11, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(600));
        command(0x29, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static bool label_pixel(const char *label, int x, int y)
{
    int width = (int)strlen(label) * 16;
    int local_x = x - (120 - width) / 2;
    int local_y = y - 22;
    if (local_x < 0 || local_x >= width || local_y < 0 || local_y >= 16) return false;
    unsigned char ch = (unsigned char)label[local_x / 16];
    return ch >= 32 && ch <= 126 &&
           (font8x8[ch - 32][local_y / 2] & (0x80 >> ((local_x % 16) / 2)));
}

void display_draw_controls(void)
{
    static const char *labels[2][4] = {
        {"LEFT", "FWD", "FIRE", "MENU"},
        {"RIGHT", "BACK", "USE", "MAP"},
    };
    static const uint16_t colors[2][4] = {
        {0x2945, 0x2945, 0x7808, 0x7BC0},
        {0x2945, 0x2945, 0x0547, 0x22CF},
    };
    display_set_viewport(0, DOOM_VIEW_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - DOOM_VIEW_HEIGHT);
    for (int y0 = 0; y0 < DISPLAY_HEIGHT - DOOM_VIEW_HEIGHT; y0 += STRIP_ROWS) {
        int rows = DISPLAY_HEIGHT - DOOM_VIEW_HEIGHT - y0;
        if (rows > STRIP_ROWS) rows = STRIP_ROWS;
        uint16_t *strip = display_acquire_strip();
        for (int r = 0; r < rows; ++r) {
            int y = y0 + r, cell_y = y / 60, inside_y = y % 60;
            for (int x = 0; x < DISPLAY_WIDTH; ++x) {
                int cell_x = x / 120, inside_x = x % 120;
                uint16_t c = (inside_x < 3 || inside_x >= 117 || inside_y < 3 || inside_y >= 57)
                    ? 0x0000 : colors[cell_y][cell_x];
                if (label_pixel(labels[cell_y][cell_x], inside_x, inside_y)) c = 0xFFFF;
                strip[r * DISPLAY_WIDTH + x] = wire(c);
            }
        }
        display_submit_strip(y0, rows);
    }
    display_wait_done();
}
