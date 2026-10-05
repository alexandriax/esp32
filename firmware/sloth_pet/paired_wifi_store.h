#ifndef SLOTH_PAIRED_WIFI_STORE_H
#define SLOTH_PAIRED_WIFI_STORE_H

#include <stddef.h>
#include <stdint.h>

namespace sloth {
namespace paired_wifi_store {

// Device-owned credentials, provisioned only through trusted USB. The default
// NVS partition is separate from pet_nvs. Flash encryption is not enabled, so
// this record is not encrypted at rest; its CRC detects damage, not tampering.
struct Profile {
  uint16_t keyLength;
  uint16_t certificateLength;
  char ssid[33];
  char password[65];
  char deviceId[33];
  char token[65];
  uint8_t key[256];
  uint8_t certificate[768];
};
enum class Result : uint8_t { Ok, Missing, Invalid, StorageError };

// Call only with the Wi-Fi worker stopped, or from that worker exclusively.
// Failed loads erase output. Invalid records are never silently replaced.
Result load(Profile& profile);
bool save(const Profile& profile);
bool forget();
void clear(Profile& profile);

}  // namespace paired_wifi_store
}  // namespace sloth
#endif
