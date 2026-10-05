#ifndef SLOTH_JPEG_DISPLAY_H
#define SLOTH_JPEG_DISPLAY_H
#include <stddef.h>
#include <stdint.h>
namespace sloth { namespace jpeg_display {
// Opaque temporary decoder storage; its size/alignment vary by target ABI.
size_t workspaceBytes();
size_t workspaceAlignment();
// Main-loop-only decoder, no allocation, display calls, or retained input.
// Accepts a bounded baseline 240x240 RGB JPEG using ordinary 4:2:0 sampling.
// Caller supplies the existing pet framebuffer (at least 240*240 uint16_t).
// Workspace is exclusively borrowed for this call, aligned as above, and must
// not overlap the input or output. It may contain arbitrary prior DMA bytes.
// No decoder object remains alive afterward, including on malformed input.
// Pixels are RGB565 in native little-endian order. On false, contents are
// unspecified: caller must not present them or ACK the frame.
bool decode(const uint8_t* data, size_t bytes, uint16_t* pixels, size_t pixelCapacity,
            void* workspace, size_t workspaceCapacity);
} }
#endif
