#pragma once
#include <stddef.h>
using esp_err_t = int;
constexpr int ESP_OK = 0, I2S_NUM_AUTO = -1, I2S_ROLE_MASTER = 1;
constexpr int I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_SLOT_MODE_MONO = 1;
constexpr int I2S_STD_SLOT_LEFT = 1, I2S_GPIO_UNUSED = -1;
constexpr int GPIO_NUM_19 = 19, GPIO_NUM_20 = 20, GPIO_NUM_22 = 22, GPIO_NUM_23 = 23;
struct MockChannel {};
using i2s_chan_handle_t = MockChannel*;
struct i2s_chan_config_t { int id, role; unsigned dma_desc_num=0, dma_frame_num=0; bool auto_clear_after_cb=false; };
struct MockClock { unsigned sample_rate_hz; };
struct MockSlot { int data_bit_width, slot_mode; int slot_mask=0; };
struct MockGPIO { int mclk, bclk, ws, dout, din; };
struct i2s_std_config_t { MockClock clk_cfg; MockSlot slot_cfg; MockGPIO gpio_cfg; };
#define I2S_CHANNEL_DEFAULT_CONFIG(i,r) {i,r}
#define I2S_STD_CLK_DEFAULT_CONFIG(s) {s}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(b,m) {b,m}
esp_err_t i2s_new_channel(const i2s_chan_config_t*,i2s_chan_handle_t*,i2s_chan_handle_t*);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t*);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_del_channel(i2s_chan_handle_t);
esp_err_t i2s_channel_write(i2s_chan_handle_t,const void*,size_t,size_t*,unsigned);
