#include "jpeg_display.h"
#include "src/vendor/jpegdec/JPEGDEC.h"
#include <new>
#include <string.h>

namespace sloth { namespace jpeg_display {
namespace {
constexpr size_t kPixels = 240 * 240, kMaxBytes = 15360;
bool decoding = false;
unsigned be16(const uint8_t* p) { return static_cast<unsigned>(p[0]) * 256 + p[1]; }

bool validRange(const void* data, size_t bytes) {
  return data && bytes <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(data);
}
bool overlaps(const void* a, size_t aBytes, const void* b, size_t bBytes) {
  const uintptr_t x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
  return x <= y ? aBytes > y - x : bBytes > x - y;
}

// Recreate the object after DMA has overwritten this storage. The default
// constructor does not clear it; openRAM already initializes the full decoder.
struct DecoderLease {
  JPEGDEC* decoder;
  explicit DecoderLease(void* workspace) : decoder(new (workspace) JPEGDEC) { decoding = true; }
  ~DecoderLease() { decoder->~JPEGDEC(); decoding = false; }
};

// Limit the library to the small baseline subset emitted by our companion.
// Bound every segment/table before opening the codec; EXIF/progressive/multi-
// scan paths are intentionally excluded. This is header validation, not an
// alternative entropy decoder. Host strips metadata before sending the JPEG.
bool supported(const uint8_t* data, size_t bytes) {
  if (!data || bytes < 256 || bytes > kMaxBytes || data[0] != 0xff || data[1] != 0xd8 ||
      data[bytes-2] != 0xff || data[bytes-1] != 0xd9) return false;
  size_t at = 2;
  unsigned quant = 0, huffman = 0, restart = 0;
  uint8_t selectors[3] = {};
  bool frame = false;
  while (at + 4 <= bytes) {
    if (data[at] != 0xff) return false;
    const unsigned marker = data[at+1], length = be16(data + at + 2);
    if (length < 2 || length > 1024 || length > bytes - at - 2) return false;
    const uint8_t* p = data + at + 4;
    const size_t count = length - 2;
    if (marker == 0xc0) {
      if (frame || count != 15 || p[0] != 8 || be16(p+1) != 240 || be16(p+3) != 240 || p[5] != 3)
        return false;
      for (unsigned c = 0; c < 3; ++c) {
        if (p[6+c*3] != c+1 || p[7+c*3] != (c ? 0x11 : 0x22) || p[8+c*3] > 1) return false;
        selectors[c] = p[8+c*3];
      }
      frame = true;
    } else if (marker == 0xdb) {
      for (size_t i = 0; i < count;) {
        if (count-i < 65 || p[i] > 1 || (quant & (1u << p[i]))) return false;
        quant |= 1u << p[i++];
        for (unsigned n = 0; n < 64; ++n) if (!p[i++]) return false;
      }
    } else if (marker == 0xc4) {
      for (size_t i = 0; i < count;) {
        if (count-i < 17 || (p[i] & 0xeeu)) return false; // DC/AC, table 0 or 1.
        const unsigned table = p[i++], bit = 1u << ((table >> 4) * 2 + (table & 1));
        if (huffman & bit) return false;
        huffman |= bit;
        unsigned symbols = 0;
        int slots = 1;
        for (unsigned bits = 0; bits < 16; ++bits) {
          slots = slots * 2 - p[i+bits];
          if (slots < 0) return false;
          symbols += p[i+bits];
        }
        i += 16;
        if (!symbols || symbols > 256 || symbols > count-i) return false;
        for (unsigned n = 0; n < symbols; ++n) {
          const unsigned value = p[i++];
          if (table < 16 ? value > 11 : ((value & 15) > 10 || (!(value & 15) && value != 0 && value != 0xf0))) return false;
        }
      }
    } else if (marker == 0xdd) {
      if (count != 2) return false;
      restart = be16(p);
    } else if (marker == 0xda) {
      if (!frame || count != 10 || p[0] != 3 || p[7] != 0 || p[8] != 63 || p[9] != 0) return false;
      for (unsigned c = 0; c < 3; ++c) {
        const unsigned tables = p[2+c*2];
        if (p[1+c*2] != c+1 || (tables & 0xeeu) || !(quant & (1u << selectors[c])) ||
            !(huffman & (1u << (tables >> 4))) || !(huffman & (1u << (2 + (tables & 15))))) return false;
      }
      at += 2 + length;
      unsigned restarts = 0;
      while (at < bytes-2) {
        if (data[at++] != 0xff) continue;
        if (at >= bytes-2) return false;
        const unsigned escaped = data[at++];
        if (!escaped) continue;
        if (!restart || escaped != 0xd0 + (restarts & 7)) return false;
        ++restarts;
      }
      return at == bytes-2 && restarts == (restart ? (225-1) / restart : 0);
    } else if (marker != 0xe0) return false;
    at += 2 + length;
  }
  return false;
}
struct Output { uint16_t* pixels; unsigned nextX, nextY, height; bool valid; };
int draw(JPEGDRAW* block) {
  Output& out = *static_cast<Output*>(block->pUser);
  if (!out.valid || !block->pPixels || block->iBpp != 16 || block->x != static_cast<int>(out.nextX) ||
      block->y != static_cast<int>(out.nextY) || block->iWidthUsed <= 0 || block->iWidthUsed > block->iWidth ||
      block->iWidth > MAX_BUFFERED_PIXELS || block->iHeight <= 0 || block->iHeight > 16 ||
      block->x + block->iWidthUsed > 240 || block->y + block->iHeight > 240 ||
      block->iWidth * block->iHeight > MAX_BUFFERED_PIXELS) { out.valid = false; return 0; }
  if (!out.nextX) out.height = static_cast<unsigned>(block->iHeight);
  if (out.height != static_cast<unsigned>(block->iHeight)) { out.valid = false; return 0; }
  for (int y = 0; y < block->iHeight; ++y)
    memcpy(out.pixels + (block->y+y)*240 + block->x, block->pPixels + y*block->iWidth,
           static_cast<size_t>(block->iWidthUsed) * sizeof(uint16_t));
  out.nextX += static_cast<unsigned>(block->iWidthUsed);
  if (out.nextX == 240) { out.nextX = 0; out.nextY += out.height; }
  return 1;
}
} // namespace
size_t workspaceBytes() { return sizeof(JPEGDEC); }
size_t workspaceAlignment() { return alignof(JPEGDEC); }
bool decode(const uint8_t* data, size_t bytes, uint16_t* pixels, size_t pixelCapacity,
            void* workspace, size_t workspaceCapacity) {
  if (decoding || pixelCapacity < kPixels || pixelCapacity > SIZE_MAX / sizeof(uint16_t) ||
      workspaceCapacity < sizeof(JPEGDEC) ||
      reinterpret_cast<uintptr_t>(workspace) % alignof(JPEGDEC) ||
      reinterpret_cast<uintptr_t>(pixels) % alignof(uint16_t) ||
      !validRange(workspace, workspaceCapacity) || !validRange(data, bytes) ||
      !validRange(pixels, pixelCapacity * sizeof(uint16_t)) ||
      overlaps(workspace, workspaceCapacity, data, bytes) ||
      overlaps(workspace, workspaceCapacity, pixels, pixelCapacity * sizeof(uint16_t)) ||
      overlaps(data, bytes, pixels, pixelCapacity * sizeof(uint16_t)) ||
      !supported(data, bytes)) return false;
  DecoderLease lease(workspace);
  JPEGDEC& decoder = *lease.decoder;
  bool result = false;
  if (decoder.openRAM(const_cast<uint8_t*>(data), static_cast<int>(bytes), draw)) {
    if (decoder.getWidth() == 240 && decoder.getHeight() == 240 && decoder.getJPEGType() == JPEG_MODE_BASELINE) {
      Output out = {pixels, 0, 0, 0, true};
      decoder.setUserPointer(&out);
      decoder.setPixelType(RGB565_LITTLE_ENDIAN);
      result = decoder.decode(0, 0, 0) && decoder.getLastError() == JPEG_SUCCESS &&
               out.valid && out.nextY == 240 && out.nextX == 0;
    }
    decoder.close();
  }
  return result;
}
} } // namespace sloth::jpeg_display
