#pragma once
#include "Arduino.h"
constexpr int SPI2_HOST = 2, SPI_DMA_CH_AUTO = 0;
struct spi_bus_config_t {
  int sclk_io_num, data0_io_num, data1_io_num, data2_io_num, data3_io_num;
  size_t max_transfer_sz;
};
esp_err_t spi_bus_initialize(int, const spi_bus_config_t*, int);
