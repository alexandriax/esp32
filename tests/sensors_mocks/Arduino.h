#pragma once
#include <stdint.h>
#include <stddef.h>
#define INPUT_PULLUP 2
#define OUTPUT 1
#define HIGH 1
#define LOW 0
extern unsigned elapsedDelay;
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void delay(unsigned ms) { elapsedDelay += ms; }
struct SerialMock { template <typename... T> void printf(const char*, T...) {} };
extern SerialMock Serial;
