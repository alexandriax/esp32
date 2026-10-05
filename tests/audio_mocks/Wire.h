#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
struct WireMock {
  uint8_t registers[128][256]{};
  bool present[128]{};
  uint8_t touch[10]{};
  bool shortRead=false;
  uint16_t timeout=100;
  uint8_t failAddress=0, failRegister=0;
  unsigned failWrites=0;
  unsigned failValue=256; // Optional filter;256 matches any byte.
  struct Write { uint8_t address, reg, value; };
  std::vector<Write> writes;
  uint8_t address=0;
  std::vector<uint8_t> command;
  std::vector<uint8_t> reply;
  size_t position=0;
  uint16_t getTimeOut() { return timeout; }
  void setTimeOut(uint16_t ms) { timeout=ms; }
  void beginTransmission(uint8_t a) { address=a; command.clear(); }
  size_t write(uint8_t v) { command.push_back(v); return 1; }
  size_t write(const uint8_t* b, size_t n) { command.insert(command.end(),b,b+n); return n; }
  uint8_t endTransmission(bool stop=true) {
    if (!present[address]) return 2;
    if (stop && address != 0x5A && command.size()==2) {
      if (failWrites && address==failAddress && command[0]==failRegister && (failValue==256 || command[1]==failValue)) { --failWrites; return 3; }
      writes.push_back(Write{address,command[0],command[1]});
      registers[address][command[0]]=command[1];
    }
    return 0;
  }
  size_t requestFrom(uint8_t a, size_t n, bool) {
    reply.clear(); position=0;
    if (!present[a]) return 0;
    if (shortRead) { shortRead=false; return 0; }
    if (a==0x5A) reply.insert(reply.end(),touch,touch+n);
    else for (size_t i=0;i<n;++i) reply.push_back(registers[a][command[0]+i]);
    return n;
  }
  int read() { return position<reply.size() ? reply[position++] : -1; }
};
extern WireMock Wire;
