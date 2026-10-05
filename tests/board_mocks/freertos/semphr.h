#pragma once
#include "Arduino.h"
using SemaphoreHandle_t = void*;
using BaseType_t = int;
constexpr int pdFALSE = 0, pdTRUE = 1;
#define pdMS_TO_TICKS(value) (value)
SemaphoreHandle_t xSemaphoreCreateBinary();
void xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
