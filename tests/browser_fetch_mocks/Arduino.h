#pragma once
#include <stdint.h>
#include <stdio.h>
#include <time.h>
uint32_t millis();
time_t browser_mock_time(time_t*);
#define time browser_mock_time
struct portMUX_TYPE {};
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
