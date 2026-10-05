#include "display_stream.h"
#include <stdlib.h>
#include "src/vendor/lz4/lz4.h"

namespace sloth {
namespace {

uint16_t read16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
}

uint32_t read32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
         static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

uint64_t read64(const uint8_t* p) {
  return read32(p) | static_cast<uint64_t>(read32(p + 4)) << 32;
}

// Reflected IEEE CRC-32, identical to zlib and the protocol's existing bitwise
// implementation. A read-only 1 KiB table replaces eight conditional steps per
// byte; it needs no initialization, allocation, or additional receiver RAM.
const uint32_t kCrc32Table[256] = {
  0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu, 0xe963a535u, 0x9e6495a3u,
  0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u, 0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u,
  0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu, 0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
  0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu, 0x14015c4fu, 0x63066cd9u, 0xfa0f3d63u, 0x8d080df5u,
  0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u, 0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu,
  0x35b5a8fau, 0x42b2986cu, 0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
  0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u, 0xb8bda50fu,
  0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u, 0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du,
  0x76dc4190u, 0x01db7106u, 0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
  0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu, 0x086d3d2du, 0x91646c97u, 0xe6635c01u,
  0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu, 0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u,
  0x65b0d9c6u, 0x12b7e950u, 0x8bbeb8eau, 0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
  0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu,
  0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u, 0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u,
  0x5005713cu, 0x270241aau, 0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
  0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u, 0xb7bd5c3bu, 0xc0ba6cadu,
  0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au, 0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u,
  0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u, 0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
  0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu, 0xf762575du, 0x806567cbu, 0x196c3671u, 0x6e6b06e7u,
  0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu, 0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u,
  0xd6d6a3e8u, 0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
  0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u, 0x316e8eefu, 0x4669be79u,
  0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u, 0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu,
  0xc5ba3bbeu, 0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
  0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au, 0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u,
  0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u, 0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u,
  0x86d3d2d4u, 0xf1d4e242u, 0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
  0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u,
  0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u, 0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu,
  0xaed16a4au, 0xd9d65adcu, 0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
  0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u, 0xcdd70693u, 0x54de5729u, 0x23d967bfu,
  0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u, 0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du,
};

uint32_t checksum(const uint8_t* data, size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; ++i)
    crc = (crc >> 8) ^ kCrc32Table[(crc ^ data[i]) & 0xffu];
  return crc ^ UINT32_MAX;
}

}  // namespace

DisplayStream::DisplayStream()
    : buffer_(control_), receiveCapacity_(sizeof(control_)), decoded_(nullptr), decodedCapacity_(0), used_(0), nonce_(0), sequence_(0), lastCrc_(0), lastSeen_(0),
      frameStarted_(0), connected_(false), waiting_(false), accepted_(false),
      dropping_(true), lastType_(DisplayPacketType::Query),
      error_(DisplayStreamError::None), rectangle_{0, 0, 0, 0, nullptr, 0, 1, false} {}

DisplayStream::~DisplayStream() { cancel(); }

bool DisplayStream::setDecodeWorkspace(void* bytes, size_t capacity) {
  if (connected_ || waiting_ || used_) return false;
  decoded_ = nullptr;
  decodedCapacity_ = 0;
  if (!bytes && !capacity) return true;
  const uintptr_t start = reinterpret_cast<uintptr_t>(bytes);
  const uintptr_t receiver = reinterpret_cast<uintptr_t>(this);
  if (!bytes || capacity < kDisplayMaxPayload || start % alignof(uint16_t) ||
      capacity > UINTPTR_MAX - start) return false;
  // Difference comparisons avoid overflowing either range's end address.
  if (start <= receiver ? capacity > receiver - start : start - receiver < sizeof(*this))
    return false;
  decoded_ = static_cast<uint8_t*>(bytes);
  decodedCapacity_ = kDisplayMaxPayload; // Larger caller buffers do not enlarge protocol limits.
  return true;
}

void DisplayStream::clearCredentials() {
  volatile uint8_t* bytes = buffer_;
  for (size_t i = 0; i < receiveCapacity_; ++i) bytes[i] = 0;
}

void DisplayStream::discardPartial() {
  used_ = 0;
  dropping_ = true;
  rectangle_ = DisplayRectangle{0, 0, 0, 0, nullptr, 0, 1, false};
}

bool DisplayStream::begin(uint64_t nonce, uint32_t now) {
  if(!nonce)cancel();
  if(nonce && buffer_==control_) {
    buffer_=static_cast<uint8_t*>(malloc(kDisplayReceiveBytes));
    if(!buffer_){buffer_=control_;cancel();return false;}
    receiveCapacity_=kDisplayReceiveBytes;
  }
  nonce_ = nonce;
  sequence_ = lastCrc_ = 0;
  lastSeen_ = now;
  connected_ = accepted_ = false;
  waiting_ = nonce != 0;
  lastType_ = DisplayPacketType::Query;
  error_ = DisplayStreamError::None;
  discardPartial();
  return true;
}

void DisplayStream::cancel() {
  clearCredentials();
  if(buffer_!=control_)free(buffer_);
  buffer_=control_;receiveCapacity_=sizeof(control_);
  connected_ = waiting_ = false;
  discardPartial();
}

DisplayStreamEvent DisplayStream::reject(DisplayStreamError error) {
  error_ = error;
  return DisplayStreamEvent::Rejected;
}

void DisplayStream::accept(DisplayPacketType type, uint32_t sequence,
                           uint32_t crc, uint32_t now) {
  lastType_ = type;
  sequence_ = sequence;
  lastCrc_ = crc;
  lastSeen_ = now;
  accepted_ = true;
  error_ = DisplayStreamError::None;
}

DisplayStreamEvent DisplayStream::poll(uint32_t now) {
  if (connected_ && static_cast<uint32_t>(now - lastSeen_) >= kDisplayHostTimeoutMs) {
    cancel();
    error_ = DisplayStreamError::None;
    return DisplayStreamEvent::Timeout;
  }
  if (used_ && static_cast<uint32_t>(now - frameStarted_) >= kDisplayFrameTimeoutMs) {
    discardPartial();
    return reject(DisplayStreamError::FrameTimeout);
  }
  return DisplayStreamEvent::None;
}

DisplayStreamEvent DisplayStream::feed(uint8_t byte, uint32_t now) {
  // The previous rectangle expires on the very next feed call. Once empty it
  // stays empty until packet() accepts another rectangle, so payload bytes do
  // not need to rewrite the same structure thousands of times per strip.
  if (rectangle_.pixels)
    rectangle_ = DisplayRectangle{0, 0, 0, 0, nullptr, 0, 1, false};
  const DisplayStreamEvent expired = poll(now);
  if (expired != DisplayStreamEvent::None) {
    // A delimiter immediately after expiry can resynchronize the next frame.
    if (byte == 0) dropping_ = false;
    return expired;
  }
  if (byte != 0) {
    if (dropping_) return DisplayStreamEvent::None;
    if (used_ == receiveCapacity_) {
      discardPartial();
      // Inactive apps accept only small controls; pixel packets never cause
      // an allocation or extend the lease after cancellation.
      return reject(!waiting_&&!connected_?DisplayStreamError::WrongState:DisplayStreamError::FrameTooLarge);
    }
    if (used_ == 0) frameStarted_ = now;
    buffer_[used_++] = byte;
    return DisplayStreamEvent::None;
  }
  if (dropping_) {
    dropping_ = false;
    used_ = 0;
    return DisplayStreamEvent::None;
  }
  if (used_ == 0) return DisplayStreamEvent::None;

  const size_t encodedLength = used_;
  used_ = 0;
  size_t source = 0, destination = 0;
  while (source < encodedLength) {
    const uint8_t code = buffer_[source++];
    const size_t run = static_cast<size_t>(code) - 1;
    if (run > encodedLength - source) return reject(DisplayStreamError::BadCobs);
    if (destination + run > kDisplayMaxDecodedBytes)
      return reject(DisplayStreamError::FrameTooLarge);
    for (size_t i = 0; i < run; ++i) buffer_[destination++] = buffer_[source++];
    if (code != 255 && source < encodedLength) {
      if (destination == kDisplayMaxDecodedBytes)
        return reject(DisplayStreamError::FrameTooLarge);
      buffer_[destination++] = 0;
    }
  }
  return packet(destination, now);
}

DisplayStreamEvent DisplayStream::packet(size_t length, uint32_t now) {
  if (length < kDisplayHeaderBytes + 4) return reject(DisplayStreamError::BadLength);
  if (buffer_[0] != 'M' || buffer_[1] != 'O' || buffer_[2] != 'S' || buffer_[3] != 'D')
    return reject(DisplayStreamError::BadMagic);
  if (buffer_[4] != 1) return reject(DisplayStreamError::BadVersion);
  if (buffer_[6] || buffer_[7] || buffer_[30] || buffer_[31])
    return reject(DisplayStreamError::Reserved);
  const uint16_t payloadBytes = read16(buffer_ + 28);
  if (payloadBytes > kDisplayMaxPayload || length != kDisplayHeaderBytes + payloadBytes + 4)
    return reject(DisplayStreamError::BadLength);
  const uint32_t crc = read32(buffer_ + length - 4);
  if (checksum(buffer_, length - 4) != crc) return reject(DisplayStreamError::BadChecksum);
  if (buffer_[5] > static_cast<uint8_t>(DisplayPacketType::AudioPCM))
    return reject(DisplayStreamError::UnknownType);
  const DisplayPacketType type = static_cast<DisplayPacketType>(buffer_[5]);
  const uint64_t nonce = read64(buffer_ + 8);
  const uint32_t sequence = read32(buffer_ + 16);
  const uint16_t x = read16(buffer_ + 20), y = read16(buffer_ + 22);
  const uint16_t width = read16(buffer_ + 24), height = read16(buffer_ + 26);
  const uint8_t* payload = buffer_ + kDisplayHeaderBytes;

  const bool scaled = type == DisplayPacketType::ScaledRectangle ||
                      type == DisplayPacketType::ScaledRleRectangle ||
                      type == DisplayPacketType::ScaledLz4Rectangle;
  const bool lz4 = type == DisplayPacketType::Lz4Rectangle ||
                   type == DisplayPacketType::ScaledLz4Rectangle;
  uint16_t renderedBytes = payloadBytes;
  const bool rle = type == DisplayPacketType::RleRectangle ||
                   type == DisplayPacketType::ScaledRleRectangle;
  if (type == DisplayPacketType::ScaledJpegFrame) {
    if (x || y || width != 240 || height != 240) return reject(DisplayStreamError::BadCoordinates);
    if (payloadBytes < 256 || payload[0] != 0xff || payload[1] != 0xd8 ||
        payload[payloadBytes-2] != 0xff || payload[payloadBytes-1] != 0xd9)
      return reject(DisplayStreamError::BadPayload);
  } else if (type == DisplayPacketType::Rectangle || scaled || rle || lz4) {
    if (scaled) {
      if (!width || static_cast<uint32_t>(x) + width > kDisplayWidth / 2 || height < 1 || height > 8 ||
          static_cast<uint32_t>(y) + height > kDisplayHeight / 2)
        return reject(DisplayStreamError::BadCoordinates);
    } else if (!width || static_cast<uint32_t>(x) + width > kDisplayWidth || ((x | y | width | height) & 1) ||
               height < 2 || height > 16 || static_cast<uint32_t>(y) + height > kDisplayHeight) {
      return reject(DisplayStreamError::BadCoordinates);
    }
    const uint32_t pixelCount = static_cast<uint32_t>(width) * height;
    if (lz4) {
      // Validate output size now, but do not modify the shared framebuffer
      // until session/state/sequence checks below establish an eligible frame.
      if (!payloadBytes || !decoded_ || pixelCount * 2 > decodedCapacity_)
        return reject(DisplayStreamError::BadPayload);
      renderedBytes = static_cast<uint16_t>(pixelCount * 2);
    } else if (rle) {
      if (payloadBytes == 0 || payloadBytes % 4 != 0)
        return reject(DisplayStreamError::BadPayload);
      uint32_t remaining = pixelCount;
      for (size_t at = 0; at < payloadBytes; at += 4) {
        const uint16_t count = read16(payload + at);
        if (count == 0 || count > remaining) return reject(DisplayStreamError::BadPayload);
        remaining -= count;
      }
      if (remaining != 0) return reject(DisplayStreamError::BadPayload);
    } else if (payloadBytes != pixelCount * 2) {
      return reject(DisplayStreamError::BadPayload);
    }
  } else {
    if (x || y || width || height) return reject(DisplayStreamError::BadCoordinates);
    if (type == DisplayPacketType::Hello) {
      if (payloadBytes != 8 || read16(payload) != kDisplayWidth ||
          read16(payload + 2) != kDisplayHeight || read16(payload + 4) != kDisplayMaxPayload ||
          payload[6] != 1 || payload[7] != 0)
        return reject(DisplayStreamError::BadCapabilities);
    } else if (type == DisplayPacketType::AudioConfig) {
      if (payloadBytes != 4 || payload[0] != 1 || payload[1] > 1 || payload[2] > 60 || payload[3])
        return reject(DisplayStreamError::BadPayload);
    } else if (type == DisplayPacketType::AudioPCM) {
      if (!payloadBytes || payloadBytes > 3200 || (payloadBytes & 1))
        return reject(DisplayStreamError::BadPayload);
    } else if (type == DisplayPacketType::ConfigureWiFi) {
      if (payloadBytes < 2 || !payload[0] || payload[0] > 32 || payload[1] > 63 ||
          (payload[1] && payload[1] < 8) || payloadBytes != 2u + payload[0] + payload[1])
        return reject(DisplayStreamError::BadPayload);
      for (unsigned i = 2; i < payloadBytes; ++i)
        if (!payload[i]) return reject(DisplayStreamError::BadPayload);
    } else if (type == DisplayPacketType::HostStatus) {
      if (payloadBytes != 1 || (payload[0] != 1 && payload[0] != 2))
        return reject(DisplayStreamError::BadPayload);
    } else if (payloadBytes != 0) {
      return reject(DisplayStreamError::BadPayload);
    }
  }

  if (type == DisplayPacketType::Query) {
    if (nonce != 0) return reject(DisplayStreamError::WrongSession);
    if (sequence != 0) return reject(DisplayStreamError::BadSequence);
    error_ = DisplayStreamError::None;
    return DisplayStreamEvent::Query;
  }
  if (nonce != nonce_) return reject(DisplayStreamError::WrongSession);
  if (type == DisplayPacketType::Release) {
    cancel();
    error_ = DisplayStreamError::None;
    return DisplayStreamEvent::Released;
  }
  if (type == DisplayPacketType::Stop && accepted_ && lastType_ == type &&
      sequence == sequence_ && crc == lastCrc_) {
    error_ = DisplayStreamError::None;
    return DisplayStreamEvent::Stopped;
  }
  if (!waiting_ && !connected_) return reject(DisplayStreamError::WrongState);

  if (type == DisplayPacketType::AudioConfig || type == DisplayPacketType::AudioPCM) {
    if (!connected_) return reject(DisplayStreamError::WrongState);
    if (sequence != 0) return reject(DisplayStreamError::BadSequence);
    // Audio is bounded and independent of acknowledged image sequences. It
    // cannot retire an image retry or keep an abandoned visual session alive.
    error_ = DisplayStreamError::None;
    return type == DisplayPacketType::AudioConfig ? DisplayStreamEvent::AudioConfigured
                                                 : DisplayStreamEvent::AudioSamples;
  }
  if (type == DisplayPacketType::ConfigureWiFi) {
    if (!waiting_ || connected_) return reject(DisplayStreamError::WrongState);
    if (sequence != 0) return reject(DisplayStreamError::BadSequence);
    error_ = DisplayStreamError::None;
    return DisplayStreamEvent::WifiConfigure;
  }
  if (type == DisplayPacketType::HostStatus) {
    if (sequence != 0) return reject(DisplayStreamError::BadSequence);
    error_ = DisplayStreamError::None;
    return payload[0] == 1 ? DisplayStreamEvent::PermissionNeeded : DisplayStreamEvent::HostError;
  }
  if (connected_ && accepted_ && sequence == sequence_ && type == lastType_ && crc == lastCrc_) {
    lastSeen_ = now;
    error_ = DisplayStreamError::None;
    return DisplayStreamEvent::Duplicate;
  }
  if (waiting_) {
    if (type != DisplayPacketType::Hello && type != DisplayPacketType::Stop)
      return reject(DisplayStreamError::WrongState);
    if (sequence != 0) return reject(DisplayStreamError::BadSequence);
  } else {
    if (type == DisplayPacketType::Hello) return reject(DisplayStreamError::WrongState);
    if (sequence_ == UINT32_MAX || sequence != sequence_ + 1)
      return reject(DisplayStreamError::BadSequence);
  }
  if (lz4) {
    // Independent raw LZ4 block; no frame header/dictionary. Safe decode bounds
    // every input read/back-reference/output write and must produce exactly the
    // advertised RGB565 rectangle before acceptance refreshes the host lease.
    if (LZ4_decompress_safe(reinterpret_cast<const char*>(payload),
                            reinterpret_cast<char*>(decoded_), payloadBytes,
                            static_cast<int>(renderedBytes)) != static_cast<int>(renderedBytes))
      return reject(DisplayStreamError::BadPayload);
    payload = decoded_;
  }
  accept(type, sequence, crc, now);
  if (type == DisplayPacketType::Hello) {
    connected_ = true;
    waiting_ = false;
    return DisplayStreamEvent::Ready;
  }
  if (type == DisplayPacketType::Stop) {
    cancel();
    return DisplayStreamEvent::Stopped;
  }
  if (type == DisplayPacketType::Ping) return DisplayStreamEvent::Pong;
  if (type == DisplayPacketType::ScaledJpegFrame) {
    rectangle_ = DisplayRectangle{0, 0, 240, 240, payload, payloadBytes, 2, false};
    return DisplayStreamEvent::JpegFrame;
  }
  rectangle_ = DisplayRectangle{x, y, width, height, payload, renderedBytes,
                                 static_cast<uint8_t>(scaled ? 2 : 1), rle};
  return DisplayStreamEvent::Rectangle;
}

}  // namespace sloth
