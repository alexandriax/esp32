#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#include "../../firmware/sloth_pet/src/vendor/lz4/lz4.h"

namespace moss_wire {
enum Type : uint8_t { Query = 0, Hello = 1, Rect = 2, Ping = 3, Stop = 4, Release = 5, HostStatus = 6,
                      RleRect = 7, ScaledRect = 8, ScaledRleRect = 9, ConfigureWiFi = 10,
                      Lz4Rect = 11, ScaledLz4Rect = 12, ScaledJpegFrame = 13, AudioConfig = 14, AudioPCM = 15 };
enum Capability : uint32_t { Rle = 1, Scale2 = 2, Lz4 = 4, CropRects = 8, Jpeg240 = 16, WirelessControl = 32, Audio = 64 };
constexpr size_t frameBytes = 480 * 480 * 2;
constexpr size_t maxPayload = 480 * 16 * 2;
inline bool rectangleType(Type type) {
  return type == Rect || type == RleRect || type == ScaledRect || type == ScaledRleRect || type == Lz4Rect || type == ScaledLz4Rect || type == ScaledJpegFrame;
}
inline bool scaledType(Type type) { return type == ScaledRect || type == ScaledRleRect || type == ScaledLz4Rect || type == ScaledJpegFrame; }
inline size_t bytesForSize(unsigned size) { return size == 240 || size == 480 ? size * size * 2 : 0; }

struct Band { unsigned y = 0, height = 0, x = 0, width = 0; };

// A per-band acknowledged image, not the newest submitted/captured frame.
// next() is read-only. Call acknowledge() only after the exact packet's ACK;
// retries and superseded capture frames must not advance this baseline.
class DeltaFrame {
 public:
  void resize(unsigned size) {
    if (size == size_) return;
    size_ = bytesForSize(size) ? size : 0;
    pixels_.assign(bytesForSize(size_), 0);
    known_.assign(size_, false);
  }
  void invalidate() { std::fill(known_.begin(), known_.end(), false); }
  unsigned size() const { return size_; }
  bool next(const uint8_t* pixels, size_t length, unsigned start, Band& band, bool crop = true) const {
    if (!pixels || !size_ || length != pixels_.size()) return false;
    const unsigned step = size_ == 480 ? 2 : 1, maximum = step * 8;
    if (start >= size_ || start % step) return false;
    unsigned y = start;
    while (y < size_ && !dirty(pixels, y, step)) y += step;
    if (y == size_) return false;
    band.y = y;
    band.height = step;
    while (band.height < maximum && y + band.height < size_ && dirty(pixels, y + band.height, step))
      band.height += step;
    band.x = 0; band.width = size_;
    if (!crop) return true;
    unsigned left = size_, right = 0;
    for (unsigned row = band.y; row < band.y + band.height; ++row) {
      // Unknown rows need every pixel; cropped ACKs never make unknown pixels known.
      if (!known_[row]) return true;
      for (unsigned x = 0; x < size_; ++x) {
        const size_t at = (row * size_ + x) * 2;
        if (pixels[at] != pixels_[at] || pixels[at + 1] != pixels_[at + 1]) {
          left = std::min(left, x); right = std::max(right, x + 1);
        }
      }
    }
    if (left < right) {
      band.x = left / step * step;
      band.width = (right + step - 1) / step * step - band.x;
    }
    return true;
  }
  bool acknowledge(const uint8_t* pixels, size_t length, Band band) {
    const unsigned step = size_ == 480 ? 2 : 1;
    if (!pixels || !size_ || length != pixels_.size() || !band.height ||
        band.y % step || band.height % step || band.height > step * 8 ||
        band.y >= size_ || band.height > size_ - band.y || !band.width ||
        band.x % step || band.width % step || band.x >= size_ || band.width > size_ - band.x) return false;
    for (unsigned y = band.y; y < band.y + band.height; ++y) {
      const size_t start = (y * size_ + band.x) * 2;
      std::memcpy(pixels_.data() + start, pixels + start, band.width * 2);
      if (band.x == 0 && band.width == size_) known_[y] = true;
    }
    return true;
  }
 private:
  bool dirty(const uint8_t* pixels, unsigned y, unsigned height) const {
    for (unsigned row = y; row < y + height; ++row) if (!known_[row]) return true;
    const size_t start = y * size_ * 2;
    return std::memcmp(pixels + start, pixels_.data() + start, height * size_ * 2) != 0;
  }
  unsigned size_ = 0;
  std::vector<uint8_t> pixels_;
  std::vector<bool> known_;
};

// Fixed runs: nonzero little-endian uint16 count, then RGB565 little-endian
// pixel. An empty result means invalid input or no compression benefit.
inline std::vector<uint8_t> rle(const uint8_t* pixels, size_t length) {
  std::vector<uint8_t> out;
  if (!pixels || !length || length % 2 || length > maxPayload) return out;
  out.reserve(length);
  for (size_t start = 0; start < length;) {
    size_t end = start + 2;
    while (end < length && (end - start) / 2 < UINT16_MAX &&
           pixels[end] == pixels[start] && pixels[end + 1] == pixels[start + 1]) end += 2;
    const unsigned count = static_cast<unsigned>((end - start) / 2);
    out.push_back(static_cast<uint8_t>(count));
    out.push_back(static_cast<uint8_t>(count >> 8));
    out.push_back(pixels[start]); out.push_back(pixels[start + 1]);
    if (out.size() >= length) return {};
    start = end;
  }
  return out;
}
// Pack only transmitted pixels; source is always the retained immutable full frame.
inline std::vector<uint8_t> pack(const uint8_t* frame, size_t length, unsigned size, Band band) {
  const unsigned step = size == 480 ? 2 : 1;
  if (!frame || !bytesForSize(size) || length != bytesForSize(size) ||
      !band.width || !band.height || band.x >= size || band.y >= size ||
      band.width > size - band.x || band.height > size - band.y ||
      band.height > step * 8 || band.x % step || band.y % step || band.width % step || band.height % step) return {};
  std::vector<uint8_t> packed(band.width * band.height * 2);
  for (unsigned row = 0; row < band.height; ++row)
    std::memcpy(packed.data() + row * band.width * 2, frame + ((band.y + row) * size + band.x) * 2, band.width * 2);
  return packed;
}

struct EncodedPixels { Type type = Rect; std::vector<uint8_t> payload; };
// Standard raw LZ4 blocks from upstream liblz4; never Apple's framed variant.
// Each codec must beat the current best size. Raw wins ties and remains fallback.
inline EncodedPixels encodePixels(const uint8_t* pixels, size_t length, unsigned size, uint32_t capabilities) {
  EncodedPixels result;
  result.type = size == 240 ? ScaledRect : Rect;
  if (!pixels || !length || length % 2 || length > maxPayload || !bytesForSize(size)) return result;
  result.payload.assign(pixels, pixels + length);
  if (capabilities & Rle) {
    auto runs = rle(pixels, length);
    if (!runs.empty() && runs.size() < result.payload.size()) {
      result.payload.swap(runs); result.type = size == 240 ? ScaledRleRect : RleRect;
    }
  }
  if ((capabilities & Lz4) && result.payload.size() > 1) {
    std::vector<uint8_t> compressed(result.payload.size() - 1);
    const int bytes = LZ4_compress_default(reinterpret_cast<const char*>(pixels),
        reinterpret_cast<char*>(compressed.data()), static_cast<int>(length), static_cast<int>(compressed.size()));
    if (bytes > 0) {
      compressed.resize(static_cast<size_t>(bytes)); result.payload.swap(compressed);
      result.type = size == 240 ? ScaledLz4Rect : Lz4Rect;
    }
  }
  return result;
}
inline uint32_t crc32(const uint8_t *bytes, size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
inline void put(std::vector<uint8_t>& bytes, size_t offset, uint64_t value, unsigned length) {
  for (unsigned i = 0; i < length; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
inline std::vector<uint8_t> packet(Type type, uint64_t session, uint32_t sequence,
                                 uint16_t y = 0, uint16_t height = 0,
                                 const uint8_t *payload = nullptr, size_t length = 0,
                                 uint16_t x = 0, uint16_t width = 0) {
  if (length > maxPayload || (length && !payload)) return {};
  std::vector<uint8_t> raw(32 + length + 4, 0);
  raw[0] = 'M'; raw[1] = 'O'; raw[2] = 'S'; raw[3] = 'D'; raw[4] = 1; raw[5] = type;
  put(raw, 8, session, 8); put(raw, 16, sequence, 4);
  if (rectangleType(type)) {
    put(raw, 20, x, 2); put(raw, 22, y, 2);
    put(raw, 24, width ? width : (scaledType(type) ? 240 : 480), 2); put(raw, 26, height, 2);
  }
  put(raw, 28, length, 2);
  for (size_t i = 0; i < length; ++i) raw[32 + i] = payload[i];
  put(raw, raw.size() - 4, crc32(raw.data(), raw.size() - 4), 4);
  // A leading delimiter interrupts any abandoned partial packet. Firmware
  // stays in binary quarantine until a valid Release ends the session.
  std::vector<uint8_t> encoded;
  encoded.reserve(raw.size() + raw.size() / 254 + 3);
  encoded.push_back(0);
  size_t codeAt = encoded.size();
  encoded.push_back(0);
  uint8_t code = 1;
  for (uint8_t byte : raw) {
    if (!byte) {
      encoded[codeAt] = code; codeAt = encoded.size(); encoded.push_back(0); code = 1;
    } else {
      encoded.push_back(byte);
      if (++code == 0xFF) {
        encoded[codeAt] = code; codeAt = encoded.size(); encoded.push_back(0); code = 1;
      }
    }
  }
  encoded[codeAt] = code;
  encoded.push_back(0);
  return encoded;
}
}  // namespace moss_wire
