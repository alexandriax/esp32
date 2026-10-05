#ifndef SLOTH_PET_PET_RECORD_H
#define SLOTH_PET_PET_RECORD_H

#include <stddef.h>
#include <stdint.h>

#include "pet_state.h"

namespace sloth {

constexpr size_t kRecordSize = 28;
constexpr size_t kLegacyRecordSize = 24;

// Version 3, all multibyte integers little-endian:
//   0..3   "SLTH" magic
//   4      version (3 for new saves; 1 and 2 remain readable)
//   5      reserved (0)
//   6..9   fullness, happiness, energy, sleeping
//   10..15 fullness_seconds, happiness_seconds, energy_seconds (uint16 each)
//   16..19 active_seconds (uint32)
//   20..23 lastUpdatedUtc, Unix seconds (uint32; 0 means unknown)
//   24..27 CRC-32/ISO-HDLC over bytes 0..23 (uint32)
// The serialized format never includes compiler-dependent struct padding.
// Legacy v1/v2 records have no timestamp and put their CRC at 20..23 instead.
// Their food/joy periods were 600/900 seconds; v3 uses 180/300 seconds.
// Version 1 used a 30-second sleeping-energy clock; v2/v3 use 3 seconds.
namespace record_detail {

inline void write16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

inline void write32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

inline uint16_t read16(const uint8_t* data) {
  return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
      (static_cast<uint16_t>(data[1]) << 8));
}

inline uint32_t read32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
      (static_cast<uint32_t>(data[1]) << 8) |
      (static_cast<uint32_t>(data[2]) << 16) |
      (static_cast<uint32_t>(data[3]) << 24);
}

inline uint32_t checksum(const uint8_t* data, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
  }
  return crc ^ UINT32_MAX;
}

}  // namespace record_detail

// Caller provides at least kRecordSize writable bytes. A null output is ignored.
// Pass a validated snapshot (normally PetState::snapshot()); decode also validates.
inline void encodeRecord(const Snapshot& saved, uint32_t lastUpdatedUtc, uint8_t* out) {
  if (!out) return;
  out[0] = 'S'; out[1] = 'L'; out[2] = 'T'; out[3] = 'H';
  out[4] = 3;
  out[5] = 0;
  out[6] = saved.fullness;
  out[7] = saved.happiness;
  out[8] = saved.energy;
  out[9] = saved.sleeping;
  record_detail::write16(out + 10, saved.fullness_seconds);
  record_detail::write16(out + 12, saved.happiness_seconds);
  record_detail::write16(out + 14, saved.energy_seconds);
  record_detail::write32(out + 16, saved.active_seconds);
  record_detail::write32(out + 20, lastUpdatedUtc);
  record_detail::write32(out + 24, record_detail::checksum(out, 24));
}

// Requires exactly one v1/v2 (24 bytes) or v3 (28 bytes) record.
// Failure leaves both output arguments unchanged. Successful legacy loads return
// lastUpdatedUtc=0, so the caller cannot infer time away before clock support.
// Rejects null input, truncated/extended data, unknown versions, corruption,
// invalid state, and nonzero timestamps outside 2000-01-01 through 2099-12-31 UTC.
// Legacy clocks are validated before proportional migration, preserving needs.
inline bool decodeRecord(const uint8_t* data, size_t size, Snapshot& out,
                         uint32_t& lastUpdatedUtc) {
  if (!data || size < 5) return false;
  const uint8_t version = data[4];
  const bool legacy = version == 1 || version == 2;
  if ((!legacy && version != 3) || size != (legacy ? kLegacyRecordSize : kRecordSize)) {
    return false;
  }
  const size_t checksum_offset = legacy ? 20 : 24;
  if (data[0] != 'S' || data[1] != 'L' || data[2] != 'T' || data[3] != 'H' ||
      data[5] != 0 || record_detail::read32(data + checksum_offset) !=
          record_detail::checksum(data, checksum_offset)) {
    return false;
  }
  const uint32_t saved_utc = legacy ? 0 : record_detail::read32(data + 20);
  if (saved_utc != 0 && (saved_utc < 946684800u || saved_utc > 4102444799u)) return false;
  Snapshot saved = {};
  saved.fullness = data[6];
  saved.happiness = data[7];
  saved.energy = data[8];
  saved.sleeping = data[9];
  saved.fullness_seconds = record_detail::read16(data + 10);
  saved.happiness_seconds = record_detail::read16(data + 12);
  saved.energy_seconds = record_detail::read16(data + 14);
  saved.active_seconds = record_detail::read32(data + 16);
  if (legacy) {
    const uint16_t old_energy_period = saved.sleeping ? (version == 1 ? 30 : 3) : 600;
    if (saved.fullness > 100 || saved.happiness > 100 || saved.energy > 100 ||
        saved.sleeping > 1 || saved.fullness_seconds >= 600 ||
        saved.happiness_seconds >= 900 || saved.energy_seconds >= old_energy_period) {
      return false;
    }
    saved.fullness_seconds = static_cast<uint16_t>(saved.fullness_seconds * 180u / 600u);
    saved.happiness_seconds = static_cast<uint16_t>(saved.happiness_seconds * 300u / 900u);
    if (version == 1 && saved.sleeping) {
      saved.energy_seconds = static_cast<uint16_t>(saved.energy_seconds / 10);
    }
  }
  PetState validator;
  if (!validator.restore(saved)) return false;
  out = saved;
  lastUpdatedUtc = saved_utc;
  return true;
}

}  // namespace sloth

#endif  // SLOTH_PET_PET_RECORD_H
