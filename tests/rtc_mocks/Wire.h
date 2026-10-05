#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

// Small in-memory PCF85063 transport with per-operation fault injection.
struct WireMock {
  uint8_t registers[256]{};
  bool present = true;
  bool ignoreControlWrites = false;
  bool corruptCalendarWrites = false;
  bool oscillatorStopped = false;
  int operation = 0, failAt = 0;
  uint16_t timeout = 100;
  uint8_t address = 0;
  std::vector<uint8_t> command, reply;
  size_t position = 0;

  bool failure() { return ++operation == failAt; }
  uint16_t getTimeOut() { return timeout; }
  void setTimeOut(uint16_t value) { timeout = value; }
  void beginTransmission(uint8_t value) { address = value; command.clear(); }
  size_t write(uint8_t value) { command.push_back(value); return 1; }
  size_t write(const uint8_t* bytes, size_t size) {
    command.insert(command.end(), bytes, bytes + size);
    return size;
  }
  uint8_t endTransmission(bool stop = true) {
    if (!present || address != 0x51) return 2;
    if (failure()) {
      // A calendar burst can partially reach the peripheral before a failure.
      if (stop && command.size() == 8) registers[4] = command[1];
      return 4;
    }
    if (stop && command.size() > 1) {
      if (command[0] == 0 && ignoreControlWrites) return 0;
      for (size_t i = 1; i < command.size(); ++i) {
        registers[command[0] + i - 1] = command[i];
      }
      if (command[0] == 4 && corruptCalendarWrites) registers[5] ^= 1;
      if (oscillatorStopped) registers[4] |= 0x80;
    }
    return 0;
  }
  size_t requestFrom(uint8_t source, size_t size, bool) {
    reply.clear();
    position = 0;
    if (!present || source != 0x51 || failure()) return 0;
    for (size_t i = 0; i < size; ++i) reply.push_back(registers[command[0] + i]);
    return size;
  }
  int read() { return position < reply.size() ? reply[position++] : -1; }
};

extern WireMock Wire;
