#include "paired_wifi_store.h"
#include <nvs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <zlib.h>

using namespace sloth::paired_wifi_store;

// This fake checks our transaction calls, not the SDK's flash implementation.
// It deliberately accepts synthetic DER; actual key/cert validation belongs to
// TLS::prepare() on the ESP32. These fixtures contain no real credentials.
static std::vector<unsigned char> disk, pending;
static bool exists = false, opened = false;
static int fault = 0;
static unsigned writes = 0, commits = 0;

esp_err_t nvs_open_from_partition(const char* partition, const char* ns, int, nvs_handle_t* handle) {
  assert(!opened);
  assert(strcmp(partition, "nvs") == 0 && strcmp(ns, "moss-wifi") == 0);
  if (fault == 1) return 9;
  opened = true;
  *handle = 7;
  return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* data, size_t* length) {
  assert(opened && handle == 7 && !strcmp(key, "profile"));
  if (fault == 2) return 9;
  if (!exists) return ESP_ERR_NVS_NOT_FOUND;
  if (data) {
    assert(*length >= disk.size());
    memcpy(data, disk.data(), disk.size());
  }
  *length = disk.size();
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t length) {
  assert(opened && handle == 7 && !strcmp(key, "profile"));
  ++writes;
  if (fault == 3) return 9;
  const auto* bytes = static_cast<const unsigned char*>(data);
  pending.assign(bytes, bytes + length);
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
  assert(opened && handle == 7);
  ++commits;
  if (fault == 4) return 9;
  disk = pending;
  exists = !disk.empty();
  return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) {
  assert(opened && handle == 7 && !strcmp(key, "profile"));
  if (fault == 5) return 9;
  if (!exists) return ESP_ERR_NVS_NOT_FOUND;
  pending.clear();
  return ESP_OK;
}
void nvs_close(nvs_handle_t handle) {
  assert(opened && handle == 7);
  opened = false;
  pending.clear();
}

static bool zeroed(const Profile& profile) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(&profile);
  for (size_t i = 0; i < sizeof(profile); ++i) if (bytes[i]) return false;
  return true;
}
static void fixCRC() {
  // Independent zlib implementation prevents the record decoder's checksum
  // rejection from masking the semantic-invalid fields tested below.
  const uint32_t value = static_cast<uint32_t>(
      crc32(0, disk.data(), static_cast<uInt>(disk.size() - 4)));
  for (unsigned i = 0; i < 4; ++i) disk[disk.size() - 4 + i] = value >> (8 * i);
}

int main() {
  Profile original{}, loaded;
  memset(&loaded, 0xa5, sizeof(loaded));
  assert(load(loaded) == Result::Missing && zeroed(loaded));
  strcpy(original.ssid, "synthetic-network");
  strcpy(original.password, "synthetic-password");
  memset(original.deviceId, 'a', 32);
  memset(original.token, 'b', 64);
  original.keyLength = 121;
  original.certificateLength = 372;
  for (unsigned i = 0; i < original.keyLength; ++i) original.key[i] = i;
  for (unsigned i = 0; i < original.certificateLength; ++i) original.certificate[i] = i * 3;
  assert(save(original) && writes == 1 && commits == 1 && disk.size() == 1240);
  assert(load(loaded) == Result::Ok && !memcmp(&original, &loaded, sizeof(original)));
  const auto good = disk;

  for (size_t i = 0; i < good.size(); ++i) {
    disk = good;
    disk[i] ^= 1;
    memset(&loaded, 0xa5, sizeof(loaded));
    assert(load(loaded) == Result::Invalid && zeroed(loaded));
  }
  for (size_t length = 0; length < good.size(); ++length) {
    disk = good;
    disk.resize(length);
    assert(load(loaded) == Result::Invalid && zeroed(loaded));
  }
  disk = good;
  disk.push_back(0);
  assert(load(loaded) == Result::Invalid && zeroed(loaded));

  // Magic, version, size, USB provenance, public ID and secret hex alphabet.
  const size_t badOffsets[] = {0, 4, 6, 8, 16 + 33 + 65, 16 + 33 + 65 + 33};
  for (size_t offset : badOffsets) {
    disk = good;
    disk[offset] = 0xff;
    fixCRC();
    assert(load(loaded) == Result::Invalid && zeroed(loaded));
  }
  for (size_t offset : {size_t(12), size_t(14)}) {
    disk = good;
    disk[offset] = disk[offset + 1] = 0xff;
    fixCRC();
    assert(load(loaded) == Result::Invalid && zeroed(loaded));
  }
  disk = good;
  memset(disk.data() + 16, 'x', 33);
  fixCRC();
  assert(load(loaded) == Result::Invalid && zeroed(loaded));
  disk = good;
  memset(disk.data() + 49, 'x', 65);
  fixCRC();
  assert(load(loaded) == Result::Invalid && zeroed(loaded));

  disk = good;
  for (int error = 1; error <= 2; ++error) {
    fault = error;
    memset(&loaded, 0xa5, sizeof(loaded));
    assert(load(loaded) == Result::StorageError && zeroed(loaded));
  }
  fault = 0;
  strcpy(original.ssid, "updated-network");
  for (int error : {1, 3, 4}) {
    fault = error;
    assert(!save(original) && disk == good);
  }
  fault = 0;
  assert(save(original));
  assert(load(loaded) == Result::Ok && !memcmp(&original, &loaded, sizeof(original)));
  assert(!strcmp(loaded.token, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  original.keyLength = 257;
  const auto saved = disk;
  assert(!save(original) && disk == saved);
  original.keyLength = 121;
  fault = 5;
  assert(!forget() && disk == saved);
  fault = 0;
  assert(forget() && !exists);
  assert(forget());
  assert(load(loaded) == Result::Missing && zeroed(loaded));
  clear(original);
  assert(zeroed(original));
  puts("paired Wi-Fi store: round-trip, corruption, bounds, provenance, failures and clearing passed");
}
