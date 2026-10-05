#pragma once
#include "task.h"
struct MockSemaphore;
using SemaphoreHandle_t = MockSemaphore*;
SemaphoreHandle_t xSemaphoreCreateBinary();
int xSemaphoreGive(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
void vSemaphoreDelete(SemaphoreHandle_t);
