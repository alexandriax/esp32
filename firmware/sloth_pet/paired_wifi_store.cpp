#include "paired_wifi_store.h"

#include <string.h>
#include <mbedtls/platform_util.h>
#include <nvs.h>

namespace sloth {
namespace paired_wifi_store {
namespace {
constexpr char kNamespace[] = "moss-wifi";
constexpr char kKey[] = "profile";
constexpr size_t kRecordBytes = 1240;
constexpr uint32_t kMagic = 0x5057534d; // MSWP, little endian.
constexpr uint32_t kUSBProvenance = 0x31425355; // USB1.
static_assert(kRecordBytes == 16 + sizeof(Profile::ssid) + sizeof(Profile::password) +
              sizeof(Profile::deviceId) + sizeof(Profile::token) + sizeof(Profile::key) +
              sizeof(Profile::certificate) + 4, "Update the versioned record layout together");

void put16(uint8_t* p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
void put32(uint8_t* p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = n >> (8 * i);
}
uint32_t get32(const uint8_t* p) {
  uint32_t n = 0;
  for (unsigned i = 0; i < 4; ++i) n |= uint32_t(p[i]) << (8 * i);
  return n;
}
uint32_t crc(const uint8_t* bytes, size_t length) {
  uint32_t value = 0xffffffffu;
  for (size_t i = 0; i < length; ++i) {
    value ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
  }
  return ~value;
}
bool hexString(const char* text, size_t length) {
  for (size_t i = 0; i < length; ++i)
    if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return false;
  return text[length] == 0;
}
bool valid(const Profile& p) {
  return p.keyLength && p.keyLength <= sizeof(p.key) &&
         p.certificateLength && p.certificateLength <= sizeof(p.certificate) &&
         p.ssid[0] && strnlen(p.ssid, sizeof(p.ssid)) < sizeof(p.ssid) &&
         strnlen(p.password, sizeof(p.password)) < sizeof(p.password) &&
         hexString(p.deviceId, 32) && hexString(p.token, 64);
}
void encode(const Profile& p, uint8_t* bytes) {
  put32(bytes, kMagic);
  put16(bytes + 4, 1);
  put16(bytes + 6, kRecordBytes);
  put32(bytes + 8, kUSBProvenance);
  put16(bytes + 12, p.keyLength);
  put16(bytes + 14, p.certificateLength);
  size_t at = 16;
#define COPY_FIELD(field) memcpy(bytes + at, p.field, sizeof(p.field)); at += sizeof(p.field)
  COPY_FIELD(ssid); COPY_FIELD(password); COPY_FIELD(deviceId); COPY_FIELD(token);
  COPY_FIELD(key); COPY_FIELD(certificate);
#undef COPY_FIELD
  put32(bytes + at, crc(bytes, at));
}
bool decode(const uint8_t* bytes, Profile& p) {
  if (get32(bytes) != kMagic || get16(bytes + 4) != 1 ||
      get16(bytes + 6) != kRecordBytes || get32(bytes + 8) != kUSBProvenance ||
      get32(bytes + kRecordBytes - 4) != crc(bytes, kRecordBytes - 4)) return false;
  p.keyLength = get16(bytes + 12);
  p.certificateLength = get16(bytes + 14);
  size_t at = 16;
#define COPY_FIELD(field) memcpy(p.field, bytes + at, sizeof(p.field)); at += sizeof(p.field)
  COPY_FIELD(ssid); COPY_FIELD(password); COPY_FIELD(deviceId); COPY_FIELD(token);
  COPY_FIELD(key); COPY_FIELD(certificate);
#undef COPY_FIELD
  return valid(p);
}
}  // namespace

void clear(Profile& profile) { mbedtls_platform_zeroize(&profile, sizeof(profile)); }

Result load(Profile& profile) {
  clear(profile);
  nvs_handle_t handle;
  esp_err_t result = nvs_open_from_partition("nvs", kNamespace, NVS_READONLY, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return Result::Missing;
  if (result != ESP_OK) return Result::StorageError;
  size_t length = 0;
  result = nvs_get_blob(handle, kKey, nullptr, &length);
  if (result != ESP_OK || length != kRecordBytes) {
    nvs_close(handle);
    return result == ESP_ERR_NVS_NOT_FOUND ? Result::Missing :
           result == ESP_OK ? Result::Invalid : Result::StorageError;
  }
  uint8_t bytes[kRecordBytes];
  result = nvs_get_blob(handle, kKey, bytes, &length);
  nvs_close(handle);
  const bool accepted = result == ESP_OK && length == kRecordBytes && decode(bytes, profile);
  mbedtls_platform_zeroize(bytes, sizeof(bytes));
  if (!accepted) clear(profile);
  return accepted ? Result::Ok : result == ESP_OK ? Result::Invalid : Result::StorageError;
}

bool save(const Profile& profile) {
  if (!valid(profile)) return false;
  uint8_t bytes[kRecordBytes];
  encode(profile, bytes);
  nvs_handle_t handle;
  esp_err_t result = nvs_open_from_partition("nvs", kNamespace, NVS_READWRITE, &handle);
  if (result == ESP_OK) {
    // One blob replacement is the transaction: no independent credential/key
    // fields can be mixed after a power loss. Never erase any NVS partition.
    result = nvs_set_blob(handle, kKey, bytes, sizeof(bytes));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
  }
  mbedtls_platform_zeroize(bytes, sizeof(bytes));
  return result == ESP_OK;
}

bool forget() {
  nvs_handle_t handle;
  esp_err_t result = nvs_open_from_partition("nvs", kNamespace, NVS_READWRITE, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result != ESP_OK) return false;
  result = nvs_erase_key(handle, kKey);
  if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
  else if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}
}  // namespace paired_wifi_store
}  // namespace sloth
