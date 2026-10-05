#pragma once
#include <mutex>
#include <stdint.h>
using TaskHandle_t = void*;
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
#define pdMS_TO_TICKS(ms) (ms)
constexpr int pdTRUE = 1, pdPASS = 1;
int xTaskCreate(void (*entry)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*);
void vTaskDelete(void*);
void vTaskDelay(unsigned);
