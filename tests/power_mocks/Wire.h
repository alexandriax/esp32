#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

struct WireMock {
  uint8_t registers[256]{};
  bool present = true;
  bool nackAfterWrite = false;
  bool ignoreIrqEnableWrites = false;
  bool ignoreIrqClear = false;
  int operation = 0, failAt = 0;
  unsigned dataWrites = 0;
  uint16_t timeout = 100;
  uint16_t observedTimeout = 0;
  uint8_t address = 0, reg = 0;
  std::vector<uint8_t> command, reply;
  struct Write { uint8_t reg, value; };
  std::vector<Write> writes;
  size_t position = 0;

  bool failure() { observedTimeout = timeout; return ++operation == failAt; }
  uint16_t getTimeOut() { return timeout; }
  void setTimeOut(uint16_t value) { timeout = value; }
  void beginTransmission(uint8_t value) { address = value; command.clear(); }
  size_t write(uint8_t value) { command.push_back(value); return 1; }
  uint8_t endTransmission(bool = true) {
    if (!present || address != 0x34) return 2;
    const bool failed = failure();
    if (failed && !nackAfterWrite) return 4;
    if (command.empty()) return 4;
    reg = command[0];
    if (command.size() > 1) {
      ++dataWrites;
      for (size_t i = 1; i < command.size(); ++i) {
        const uint8_t at = static_cast<uint8_t>(reg + i - 1);
        const uint8_t value = command[i];
        writes.push_back({at, value});
        if (at == 0x41 && ignoreIrqEnableWrites) continue;
        if (at >= 0x48 && at <= 0x4A) {
          if (!ignoreIrqClear) registers[at] &= ~value; // W1C interrupt status.
        } else {
          registers[at] = value;
        }
      }
    }
    return failed ? 4 : 0;
  }
  size_t requestFrom(uint8_t source, size_t size, bool) {
    reply.clear();
    position = 0;
    if (!present || source != 0x34 || failure()) return 0;
    for (size_t i = 0; i < size; ++i) reply.push_back(registers[reg + i]);
    return size;
  }
  int read() { return position < reply.size() ? reply[position++] : -1; }
};

extern WireMock Wire;
