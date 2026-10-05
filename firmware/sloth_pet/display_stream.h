#ifndef SLOTH_DISPLAY_STREAM_H
#define SLOTH_DISPLAY_STREAM_H

#include <stddef.h>
#include <stdint.h>

namespace sloth {

constexpr uint16_t kDisplayWidth = 480;
constexpr uint16_t kDisplayHeight = 480;
constexpr uint16_t kDisplayMaxPayload = 15360;
constexpr size_t kDisplayHeaderBytes = 32;
constexpr size_t kDisplayMaxDecodedBytes = kDisplayHeaderBytes + kDisplayMaxPayload + 4;
constexpr size_t kDisplayReceiveBytes = kDisplayMaxDecodedBytes + kDisplayMaxDecodedBytes / 254 + 1;
constexpr uint32_t kDisplayFrameTimeoutMs = 500;
constexpr uint32_t kDisplayHostTimeoutMs = 3000;

enum class DisplayPacketType : uint8_t {
  Query = 0, Hello = 1, Rectangle = 2, Ping = 3, Stop = 4, Release = 5, HostStatus = 6,
  RleRectangle = 7, ScaledRectangle = 8, ScaledRleRectangle = 9, ConfigureWiFi = 10,
  Lz4Rectangle = 11, ScaledLz4Rectangle = 12, ScaledJpegFrame = 13, AudioConfig = 14, AudioPCM = 15
};

enum class DisplayStreamEvent : uint8_t {
  None, Query, Ready, Rectangle, Duplicate, Pong, Stopped, Released,
  PermissionNeeded, HostError, Timeout, Rejected, WifiConfigure, JpegFrame, AudioConfigured, AudioSamples
};

// Stable numeric codes suitable for diagnostic NACK lines. Rejections never
// advance the accepted sequence or refresh the connected host's lease.
enum class DisplayStreamError : uint8_t {
  None = 0, FrameTooLarge = 1, FrameTimeout = 2, BadCobs = 3,
  BadLength = 4, BadMagic = 5, BadVersion = 6, Reserved = 7,
  BadChecksum = 8, UnknownType = 9, WrongSession = 10,
  WrongState = 11, BadSequence = 12, BadCoordinates = 13,
  BadCapabilities = 14, BadPayload = 15
};

struct DisplayRectangle {
  uint16_t x, y, width, height;  // Logical coordinates; multiply by scale for panel.
  const uint8_t* pixels;  // Raw RGB565 LE (including decompressed LZ4) or RLE runs; valid until the next feed().
  uint16_t bytes;  // Encoded payload bytes, including run counts when rle=true.
  uint8_t scale;   // 1=native 480x480; 2=logical 240x240, nearest-neighbor 2x2.
  bool rle;       // Four-byte runs: {count LE16, pixel RGB565 LE16}.
};

// COBS + CRC receiver only: it never draws, allocates, emits serial data, or
// interprets legacy commands. The caller must latch a global binary transport
// lock at the first zero byte, and release that lock only on Released/reset.
// begin() is called exclusively after explicit local user consent to display.
class DisplayStream {
 public:
  DisplayStream();
  ~DisplayStream();
  DisplayStream(const DisplayStream&)=delete;
  DisplayStream& operator=(const DisplayStream&)=delete;
  size_t receiveBytes() const {return buffer_==control_?0:kDisplayReceiveBytes;}
  // Borrow at least kDisplayMaxPayload writable bytes for LZ4 output. Bind only
  // while idle (not waiting/connected or receiving a partial frame); the caller
  // keeps the aligned storage alive until unbound. It must not overlap this
  // receiver. begin()/cancel() retain the binding. nullptr + 0 unbinds; invalid
  // idle bindings clear it and return false. Busy calls leave it unchanged.
  // The pet framebuffer can be reused because only session-eligible Remote Display
  // rectangles write here, and their presentation finishes before next feed().
  bool setDecodeWorkspace(void* bytes, size_t capacity);
  bool begin(uint64_t nonce, uint32_t now);  // Nonzero nonce required.
  void cancel();  // Retains nonce/sequence for STOP notification and RELEASE.
  DisplayStreamEvent feed(uint8_t byte, uint32_t now);
  DisplayStreamEvent poll(uint32_t now);

  uint64_t nonce() const { return nonce_; }
  uint32_t sequence() const { return sequence_; }
  bool connected() const { return connected_; }
  bool waiting() const { return waiting_; }
  DisplayPacketType lastType() const { return lastType_; }
  DisplayStreamError error() const { return error_; }
  const DisplayRectangle& rectangle() const { return rectangle_; }
  // Length-prefixed UTF-8 SSID/password, valid until next feed; never log.
  const uint8_t* wifiCredentials() const { return buffer_ + kDisplayHeaderBytes; }
  void clearCredentials();
  // Auxiliary audio payload is valid only immediately after its matching event.
  const uint8_t* audioPayload() const { return buffer_ + kDisplayHeaderBytes; }
  uint16_t audioBytes() const { return buffer_[28] | (static_cast<uint16_t>(buffer_[29]) << 8); }

 private:
  DisplayStreamEvent reject(DisplayStreamError error);
  DisplayStreamEvent packet(size_t length, uint32_t now);
  void accept(DisplayPacketType type, uint32_t sequence, uint32_t crc, uint32_t now);
  void discardPartial();

  // COBS decodes in place. LZ4 borrows the caller's idle framebuffer/workspace
  // instead of reserving a second permanent 15 KiB stripe. No allocations.
  alignas(uint16_t) uint8_t control_[256]{};
  uint8_t* buffer_;
  size_t receiveCapacity_;
  uint8_t* decoded_;
  size_t decodedCapacity_;
  size_t used_;
  uint64_t nonce_;
  uint32_t sequence_, lastCrc_, lastSeen_, frameStarted_;
  bool connected_, waiting_, accepted_, dropping_;
  DisplayPacketType lastType_;
  DisplayStreamError error_;
  DisplayRectangle rectangle_;
};

}  // namespace sloth
#endif
