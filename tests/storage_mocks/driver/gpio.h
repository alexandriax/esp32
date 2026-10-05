#pragma once
#include "../storage_mock.h"
constexpr int GPIO_MODE_OUTPUT=2;
esp_err_t gpio_set_level(int,int);
esp_err_t gpio_set_direction(int,int);
