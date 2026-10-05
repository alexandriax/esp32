#pragma once
#include <stdint.h>
uint32_t micros();
void yield();
void configTime(long offset,long daylight,const char* first,const char* second);
