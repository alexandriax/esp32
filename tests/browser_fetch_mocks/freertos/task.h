#pragma once
#include <stdint.h>
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);
void vTaskDelay(unsigned);
void vTaskDelete(void*);
uint32_t uxTaskGetStackHighWaterMark(void*);
