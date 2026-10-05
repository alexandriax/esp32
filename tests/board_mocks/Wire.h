#pragma once
#include "Arduino.h"
struct MockWire {
  void begin(int, int, unsigned) {}
  void setTimeOut(unsigned) {}
  void beginTransmission(int);
  void write(uint8_t);
  int endTransmission(bool = true);
  int requestFrom(int, int) { return 1; }
  int read();
};
extern MockWire Wire;
