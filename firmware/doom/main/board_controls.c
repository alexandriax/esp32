#include "board_controls.h"

#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* The I2C register sequences and coordinate transform match Moss's board.cpp,
 * sensors.cpp and pet_power.cpp for this exact Waveshare board. */
#define I2C_PORT I2C_NUM_0
#define PMIC_ADDR 0x34
#define TOUCH_ADDR 0x5a
#define TOUCH_RESET GPIO_NUM_11
#define KEY_BUTTON GPIO_NUM_10
#define BOOT_BUTTON GPIO_NUM_9
#define PMIC_RAILS 0x90
#define PMIC_ALDO1_VOLTAGE 0x92
#define PMIC_ALDO3_VOLTAGE 0x94
#define PMIC_ALDO1 0x01
#define PMIC_ALDO2 0x02
#define PMIC_ALDO3 0x04
#define PMIC_PWR_IRQ_ENABLE 0x41
#define PMIC_PWR_IRQ_STATUS 0x49
#define PMIC_PWR_SHORT 0x08
#define DISPLAY_SIZE 480

static const char *TAG = "DOOM_BOARD";
static bool power_button_ready;
static bool power_button_reported;
static int64_t next_power_button_retry;
static int last_touch_x, last_touch_y;
static int battery_percent = -1, battery_mv;
static int64_t battery_sample_time;

static esp_err_t read_regs(uint8_t address, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_PORT, address, &reg, 1, data, len, pdMS_TO_TICKS(10));
}

static esp_err_t write_reg(uint8_t address, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = { reg, value };
    return i2c_master_write_to_device(I2C_PORT, address, data, sizeof data, pdMS_TO_TICKS(10));
}

static bool pmic_read(uint8_t reg, uint8_t *value)
{
    return read_regs(PMIC_ADDR, reg, value, 1) == ESP_OK;
}

static bool pmic_write(uint8_t reg, uint8_t value)
{
    return write_reg(PMIC_ADDR, reg, value) == ESP_OK;
}

static void delay_ms(unsigned milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

static bool begin_power_button(void)
{
    uint8_t enabled, verified, pending;
    power_button_ready = false;
    power_button_reported = false;
    if (!pmic_read(PMIC_PWR_IRQ_ENABLE, &enabled)) return false;
    uint8_t wanted = enabled | PMIC_PWR_SHORT;
    if (wanted != enabled && !pmic_write(PMIC_PWR_IRQ_ENABLE, wanted)) return false;
    if (!pmic_read(PMIC_PWR_IRQ_ENABLE, &verified) || verified != wanted) return false;
    if (!pmic_read(PMIC_PWR_IRQ_STATUS, &pending)) return false;
    if ((pending & PMIC_PWR_SHORT) && !pmic_write(PMIC_PWR_IRQ_STATUS, PMIC_PWR_SHORT)) return false;
    power_button_ready = true;
    return true;
}

void board_controls_init(void)
{
    i2c_config_t bus = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_7,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &bus));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    gpio_config_t keys = {
        .pin_bit_mask = (1ULL << KEY_BUTTON) | (1ULL << BOOT_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&keys));

    uint8_t id, voltage, rails;
    ESP_ERROR_CHECK(pmic_read(0x03, &id) && id == 0x4a ? ESP_OK : ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(pmic_read(PMIC_ALDO3_VOLTAGE, &voltage) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(pmic_write(PMIC_ALDO3_VOLTAGE, (voltage & 0xe0) | 28) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(pmic_read(PMIC_RAILS, &rails) ? ESP_OK : ESP_FAIL);
    /* Moss pulses ALDO3 once at startup to reset the CO5300 panel. Preserve
     * every unrelated PMIC rail, including any saved audio supply state. */
    ESP_ERROR_CHECK(pmic_write(PMIC_RAILS, rails | PMIC_ALDO3) ? ESP_OK : ESP_FAIL);
    delay_ms(100);
    ESP_ERROR_CHECK(pmic_write(PMIC_RAILS, rails & ~PMIC_ALDO3) ? ESP_OK : ESP_FAIL);
    delay_ms(100);
    ESP_ERROR_CHECK(pmic_write(PMIC_RAILS, rails | PMIC_ALDO3) ? ESP_OK : ESP_FAIL);
    delay_ms(100);

    gpio_config_t touch_reset = {
        .pin_bit_mask = 1ULL << TOUCH_RESET,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&touch_reset));
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET, 1));
    delay_ms(200);
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET, 0));
    delay_ms(200);
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET, 1));
    delay_ms(200);

    power_button_ready = begin_power_button();
    ESP_LOGI(TAG, "AXP2101 ready; display rail on; touch reset; PWR tap %s",
             power_button_ready ? "ready" : "retrying");
}

bool board_touch_read(int *x, int *y, bool *down)
{
    if (!x || !y || !down) return false;
    const uint8_t command[2] = { 0xd0, 0x00 };
    uint8_t data[10];
    if (i2c_master_write_read_device(I2C_PORT, TOUCH_ADDR, command, sizeof command,
                                     data, sizeof data, pdMS_TO_TICKS(10)) != ESP_OK || data[6] != 0xab)
        return false;
    unsigned points = data[5] & 0x7f;
    if (points > 5) return false;
    if (points == 0 || (data[0] & 0x0f) == 0) {
        *x = last_touch_x;
        *y = last_touch_y;
        *down = false;
        return true;
    }
    if (((data[0] & 0x0f) >> 1) != 3) return false;
    unsigned raw_y = ((unsigned)data[1] << 4) | (data[3] >> 4);
    unsigned raw_x = ((unsigned)data[2] << 4) | (data[3] & 0x0f);
    if (raw_x >= DISPLAY_SIZE || raw_y >= DISPLAY_SIZE) return false;
    last_touch_x = DISPLAY_SIZE - 1 - raw_x;
    last_touch_y = raw_y;
    *x = last_touch_x;
    *y = last_touch_y;
    *down = true;
    return true;
}

bool board_power_tap(void)
{
    int64_t now = esp_timer_get_time();
    if (!power_button_ready) {
        if (now < next_power_button_retry) return false;
        next_power_button_retry = now + 5000000;
        power_button_ready = begin_power_button();
        return false;
    }
    uint8_t pending;
    if (!pmic_read(PMIC_PWR_IRQ_STATUS, &pending)) return false;
    if (!(pending & PMIC_PWR_SHORT)) {
        power_button_reported = false;
        return false;
    }
    bool first = !power_button_reported;
    power_button_reported = true;
    bool acknowledged = pmic_write(PMIC_PWR_IRQ_STATUS, PMIC_PWR_SHORT);
    if (!acknowledged) ESP_LOGW(TAG, "PWR short IRQ acknowledgement failed");
    return first;
}

static void sample_battery(void)
{
    int64_t now = esp_timer_get_time();
    if (battery_sample_time && now - battery_sample_time < 5000000) return;
    battery_sample_time = now;
    battery_percent = -1;
    battery_mv = 0;
    uint8_t status[2], detection, adc_enabled, vbat[2], gauge, percentage;
    if (read_regs(PMIC_ADDR, 0x00, status, sizeof status) != ESP_OK ||
        !pmic_read(0x68, &detection)) return;
    unsigned direction = (status[1] >> 5) & 3;
    if ((status[0] & 0xc0) || (status[1] & 0x80) || direction == 3 ||
        (detection & 0xfe) || !(detection & 1) || !(status[0] & 0x08)) return;

    /* AXP2101 VBAT ADC is 14-bit, 1 mV/LSB; its channel is enabled by default.
     * Leave the channel configuration alone when another app has changed it. */
    if (pmic_read(0x30, &adc_enabled) && (adc_enabled & 1) &&
        read_regs(PMIC_ADDR, 0x34, vbat, sizeof vbat) == ESP_OK) {
        int mv = ((vbat[0] & 0x3f) << 8) | vbat[1];
        if (mv >= 1500 && mv <= 5000) battery_mv = mv;
    }
    if (pmic_read(0x18, &gauge) && !(gauge & 0xf0) && (gauge & 0x08) &&
        pmic_read(0xa4, &percentage) && percentage <= 100) {
        battery_percent = percentage;
    } else if (battery_mv) {
        int estimate = (battery_mv - 3300) * 100 / 900;
        battery_percent = estimate < 0 ? 0 : estimate > 100 ? 100 : estimate;
    }
}

int board_battery_percent(void)
{
    sample_battery();
    return battery_percent;
}

int board_battery_mv(void)
{
    sample_battery();
    return battery_mv;
}

bool board_audio_power(bool enabled)
{
    uint8_t rails;
    if (!pmic_read(PMIC_RAILS, &rails)) return false;
    if (!enabled) {
        /* Silence the external amplifier before dropping the codec supply. */
        if (!pmic_write(PMIC_RAILS, rails & ~PMIC_ALDO2)) return false;
        return pmic_write(PMIC_RAILS, rails & ~(PMIC_ALDO1 | PMIC_ALDO2));
    }
    uint8_t voltage;
    if (!pmic_read(PMIC_ALDO1_VOLTAGE, &voltage) ||
        !pmic_write(PMIC_ALDO1_VOLTAGE, (voltage & 0xe0) | 28) ||
        !pmic_write(PMIC_RAILS, rails | PMIC_ALDO1)) return false;
    delay_ms(5);
    if (!pmic_read(0x93, &voltage)) return false;
    return pmic_write(0x93, (voltage & 0xe0) | 28);
}

bool board_audio_amplifier(bool enabled)
{
    uint8_t rails;
    if (!pmic_read(PMIC_RAILS, &rails)) return false;
    return pmic_write(PMIC_RAILS, enabled ? rails | PMIC_ALDO2 : rails & ~PMIC_ALDO2);
}

bool board_confirm_doom_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "cannot identify running Doom partition");
        return false;
    }
    esp_ota_img_states_t state;
    esp_err_t error = esp_ota_get_state_partition(running, &state);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "cannot read Doom OTA state: %s", esp_err_to_name(error));
        return false;
    }
    if (state == ESP_OTA_IMG_VALID) return true;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGE(TAG, "unexpected Doom OTA state: %d", (int)state);
        return false;
    }
    error = esp_ota_mark_app_valid_cancel_rollback();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "cannot confirm Doom boot: %s", esp_err_to_name(error));
        return false;
    }
    ESP_LOGI(TAG, "Doom boot confirmed after first rendered frame");
    return true;
}

void board_return_to_moss(void)
{
    board_audio_power(false);
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    if (!factory) ESP_LOGE(TAG, "factory Moss partition missing");
    else {
        esp_err_t error = esp_ota_set_boot_partition(factory);
        if (error == ESP_OK) {
            ESP_LOGI(TAG, "returning to Moss");
            esp_restart();
        }
        ESP_LOGE(TAG, "cannot select Moss: %s", esp_err_to_name(error));
    }
    /* Rebooting with unchanged otadata would trap the user in Doom. Keep the
     * current app alive for serial recovery if the handoff fails. */
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
