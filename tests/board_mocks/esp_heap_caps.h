#pragma once
#include "Arduino.h"
constexpr int MALLOC_CAP_DMA = 1;
void* heap_caps_malloc(size_t, int);
