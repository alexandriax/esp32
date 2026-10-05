#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

using std::min;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NOT_FOUND = 1;
constexpr int ESP_ERR_NO_MEM = 2, ESP_ERR_TIMEOUT = 3;
constexpr int ESP_ERR_INVALID_STATE = 4, ESP_ERR_INVALID_ARG = 5;
#define ESP_ERROR_CHECK(value) do { if ((value) != ESP_OK) throw std::runtime_error("ESP error"); } while (0)
#define IRAM_ATTR
constexpr int INPUT_PULLUP = 1;
void pinMode(int, int);
void delay(unsigned ms);
struct MockSerial {
  template <typename... Args> void printf(const char*, Args...) {}
  void println(const char*) {}
};
extern MockSerial Serial;
