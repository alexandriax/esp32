#pragma once
#include <stddef.h>
#include <stdint.h>

namespace sloth { namespace wifi_networks {
constexpr size_t kMaxNetworks = 12;
enum class State : uint8_t { Off, Scanning, Ready, Joining, Connected, Failed };
enum class Error : uint8_t {
  None, Busy, InvalidCredentials, Radio, Scan, Join, Timeout, Disconnected, Storage
};
struct AccessPoint {
  char ssid[33];
  int16_t rssi;
  bool secured;
  bool supported;
};
struct Snapshot {
  State state;
  Error error;
  uint32_t generation;
  AccessPoint networks[kMaxNetworks];
  uint8_t count;
  char currentSsid[33];
  char savedSsid[33];
  bool hasSaved;
  char address[16];
  int16_t rssi;
};

// The mode owner calls tick(). Commands replace an earlier scan/connection.
// The main loop must stop Remote Display / Radio Explorer and wait for their
// workers before owning this service. Remote Display instead ticks on its worker.
// Connecting Settings does not create or change the paired display identity.
// With all radio owners idle, initialize persistent network infrastructure before
// releasing a large shared canvas. Starts no scan/join; returns with Wi-Fi off.
// Idempotent after success. A busy controller or SDK failure returns false.
bool prepare();
bool scan();
bool connect(const char* ssid, const char* password, bool persist = true);
bool connectSaved();
void tick();
void cancel();
void stop();
bool busy(); // Radio is owned during scanning, joining, or a live connection.
bool connected(); // Last tick's link state; no scan-result snapshot copy.
Snapshot snapshot();
bool hasSaved();
bool savedSsid(char* output, size_t capacity);
bool forgetSaved(); // Durable tombstone; preserves the display TLS identity.
} } // namespace sloth::wifi_networks
