#pragma once
#include <stdint.h>
#include <mutex>
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
constexpr int pdPASS=1,ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=257,ESP_ERR_TIMEOUT=263;
inline unsigned pdMS_TO_TICKS(unsigned value){return value;}
uint32_t millis();
void vTaskDelay(unsigned);
void vTaskDelete(void*);
int xTaskCreate(void (*)(void*),const char*,unsigned,void*,unsigned,void*);
