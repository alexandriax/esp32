#pragma once
struct StaticSemaphore_t { bool held; };
typedef StaticSemaphore_t* SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t*);
void xSemaphoreTake(SemaphoreHandle_t,unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
