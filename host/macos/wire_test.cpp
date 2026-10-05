#include "WireProtocol.hpp"
#include "../../firmware/sloth_pet/display_stream.h"
#include <cassert>
#include <cstdio>
#include <cstring>

struct WireReceiver : sloth::DisplayStream {
  alignas(uint16_t) uint8_t workspace[sloth::kDisplayMaxPayload];
  WireReceiver() { assert(setDecodeWorkspace(workspace, sizeof(workspace))); }
};

static sloth::DisplayStreamEvent send(sloth::DisplayStream& receiver,
                                     const std::vector<uint8_t>& packet, uint32_t now) {
  auto result = sloth::DisplayStreamEvent::None;
  assert(packet.front() == 0 && packet.back() == 0);
  for (size_t i = 1; i + 1 < packet.size(); ++i) assert(packet[i] != 0);
  for (const uint8_t byte : packet) {
    const auto event = receiver.feed(byte, now);
    if (event != sloth::DisplayStreamEvent::None) result = event;
  }
  return result;
}

static void deltaPlannerAndRle() {
  moss_wire::DeltaFrame image;
  image.resize(480);
  std::vector<uint8_t> pixels(moss_wire::frameBytes, 0x2A);
  moss_wire::Band band;
  assert(image.next(pixels.data(), pixels.size(), 0, band) && band.y == 0 && band.height == 16);
  assert(image.next(pixels.data(), pixels.size(), 0, band)); // Merely sending is not an ACK.
  for (unsigned row = 0; image.next(pixels.data(), pixels.size(), row, band); row = band.y + band.height)
    assert(image.acknowledge(pixels.data(), pixels.size(), band));
  assert(!image.next(pixels.data(), pixels.size(), 0, band));
  pixels[(101 * 480 + 3) * 2] ^= 1;
  pixels[(104 * 480 + 7) * 2] ^= 1;
  pixels[(107 * 480 + 7) * 2] ^= 1;
  assert(image.next(pixels.data(), pixels.size(), 0, band) && band.y == 100 && band.height == 2);
  assert(!image.acknowledge(pixels.data(), pixels.size() - 1, band));
  assert(image.next(pixels.data(), pixels.size(), 0, band) && band.y == 100);
  assert(image.acknowledge(pixels.data(), pixels.size(), band));
  assert(image.next(pixels.data(), pixels.size(), 0, band) && band.y == 104 && band.height == 4);
  assert(image.acknowledge(pixels.data(), pixels.size(), band));
  assert(!image.next(pixels.data(), pixels.size(), 0, band));
  image.invalidate();
  assert(image.next(pixels.data(), pixels.size(), 0, band) && band.y == 0 && band.height == 16);
  assert(!image.next(pixels.data(), pixels.size(), 1, band));
  image.resize(240); pixels.resize(240 * 240 * 2);
  assert(image.next(pixels.data(), pixels.size(), 0, band) && band.y == 0 && band.height == 8);
  band.y = 239; band.height = 1;
  assert(image.acknowledge(pixels.data(), pixels.size(), band));
  band.height = 2;
  assert(!image.acknowledge(pixels.data(), pixels.size(), band));
  image.resize(123);
  assert(image.size() == 0 && !image.next(pixels.data(), pixels.size(), 0, band));

  std::vector<uint8_t> solid(moss_wire::maxPayload, 0x5A);
  const auto compact = moss_wire::rle(solid.data(), solid.size());
  assert(compact.size() == 4 && compact[0] == 0 && compact[1] == 30 && compact[2] == 0x5A && compact[3] == 0x5A);
  for (size_t i = 0; i < solid.size(); i += 2) { solid[i] = static_cast<uint8_t>(i / 2); solid[i + 1] = 0; }
  assert(moss_wire::rle(solid.data(), solid.size()).empty()); // Incompressible pixels use raw.
  const uint8_t equal[] = {1, 2, 1, 2};
  assert(moss_wire::rle(equal, sizeof(equal)).empty()); // Equal size also uses raw.
  assert(moss_wire::rle(nullptr, 4).empty() && moss_wire::rle(equal, 3).empty());
}

static void croppedAcknowledgedPixelsAndCodecs() {
  for (unsigned size : {480u, 240u}) {
    moss_wire::DeltaFrame image; image.resize(size);
    std::vector<uint8_t> pixels(size * size * 2, 0);
    moss_wire::Band band;
    assert(image.next(pixels.data(), pixels.size(), 0, band));
    assert(band.x == 0 && band.width == size); // Unknown pixels always receive a full row.
    moss_wire::Band narrow = band; narrow.x = 2; narrow.width = 2;
    assert(image.acknowledge(pixels.data(), pixels.size(), narrow));
    assert(image.next(pixels.data(), pixels.size(), 0, band) && band.width == size);
    for (unsigned row = 0; image.next(pixels.data(), pixels.size(), row, band); row = band.y + band.height)
      assert(image.acknowledge(pixels.data(), pixels.size(), band));
    pixels[((size - 1) * size + size - 1) * 2] = 9;
    assert(image.next(pixels.data(), pixels.size(), 0, band));
    const unsigned step = size == 480 ? 2 : 1;
    assert(band.x == size - step && band.y == size - step && band.width == step && band.height == step);
    const auto packed = moss_wire::pack(pixels.data(), pixels.size(), size, band);
    assert(packed.size() == step * step * 2 && packed[packed.size() - 2] == 9);
    assert(image.acknowledge(pixels.data(), pixels.size(), band));
    assert(!image.next(pixels.data(), pixels.size(), 0, band));
    pixels[10 * 2] = 1; pixels[(size - 10) * 2] = 2;
    narrow.y = 0; narrow.height = step; narrow.x = 10; narrow.width = step;
    assert(image.acknowledge(pixels.data(), pixels.size(), narrow));
    assert(image.next(pixels.data(), pixels.size(), 0, band));
    assert(band.x == size - 10 && band.width == step); // ACK changes only its rectangle.
    moss_wire::Band full;
    assert(image.next(pixels.data(), pixels.size(), 0, full, false) && full.x == 0 && full.width == size);
    if (size == 480) { narrow.width = 1; assert(!image.acknowledge(pixels.data(), pixels.size(), narrow)); }
    narrow.x = size; assert(moss_wire::pack(pixels.data(), pixels.size(), size, narrow).empty());
  }
  std::vector<uint8_t> repeated(moss_wire::maxPayload);
  for (size_t i = 0; i < repeated.size(); ++i) repeated[i] = static_cast<uint8_t>(i * 37);
  auto encoded = moss_wire::encodePixels(repeated.data(), repeated.size(), 480, 15);
  assert(encoded.type == moss_wire::Lz4Rect && encoded.payload.size() < repeated.size() / 4);
  std::vector<uint8_t> decoded(repeated.size());
  assert(LZ4_decompress_safe(reinterpret_cast<const char *>(encoded.payload.data()),
                            reinterpret_cast<char *>(decoded.data()), (int)encoded.payload.size(), (int)decoded.size()) == (int)decoded.size());
  assert(decoded == repeated);
  auto legacy = moss_wire::encodePixels(repeated.data(), repeated.size(), 480, 3);
  assert(legacy.type == moss_wire::Rect && legacy.payload == repeated);
  std::fill(repeated.begin(), repeated.end(), 0x55);
  encoded = moss_wire::encodePixels(repeated.data(), repeated.size(), 480, 15);
  assert(encoded.type == moss_wire::RleRect && encoded.payload.size() == 4); // Smallest codec wins.
  encoded = moss_wire::encodePixels(repeated.data(), repeated.size(), 480, moss_wire::Lz4);
  assert(encoded.type == moss_wire::Lz4Rect);
  assert(moss_wire::encodePixels(nullptr, 4, 480, 15).payload.empty());
  assert(moss_wire::encodePixels(repeated.data(), 3, 480, 15).payload.empty());
  assert(moss_wire::encodePixels(repeated.data(), repeated.size(), 123, 15).payload.empty());
}

static void croppedLz4WireInteroperability() {
  using E = sloth::DisplayStreamEvent;
  WireReceiver receiver;
  const uint64_t nonce = 0xA111222233334444ULL;
  receiver.begin(nonce, 0);
  const uint8_t hello[] = {0xE0, 1, 0xE0, 1, 0, 0x3C, 1, 0};
  assert(send(receiver, moss_wire::packet(moss_wire::Hello, nonce, 0, 0, 0, hello, sizeof(hello)), 1) == E::Ready);
  uint32_t sequence = 0;
  for (unsigned size : {480u, 240u}) {
    const unsigned step = size == 480 ? 2 : 1;
    const unsigned width = size / 2, height = step * 8;
    std::vector<uint8_t> pixels(width * height * 2);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i * 17);
    const auto encoded = moss_wire::encodePixels(pixels.data(), pixels.size(), size, 15);
    assert(encoded.type == (size == 480 ? moss_wire::Lz4Rect : moss_wire::ScaledLz4Rect));
    const auto packet = moss_wire::packet(encoded.type, nonce, ++sequence, 2 * step, height,
                                         encoded.payload.data(), encoded.payload.size(), 3 * step, width);
    assert(send(receiver, packet, 2 + sequence) == E::Rectangle);
    const auto &rect = receiver.rectangle();
    assert(rect.x == 3 * step && rect.y == 2 * step && rect.width == width && rect.height == height);
    assert(rect.scale == (size == 480 ? 1 : 2) && !rect.rle);
    assert(rect.pixels == receiver.workspace);
    assert(rect.bytes == pixels.size() && !memcmp(rect.pixels, pixels.data(), pixels.size()));
    assert(send(receiver, packet, 3 + sequence) == E::Duplicate);
    auto damaged = packet; damaged[damaged.size() - 3] ^= 0x20; // Corrupt CRC, preserving COBS structure.
    assert(send(receiver, damaged, 4 + sequence) == E::Rejected);
    assert(receiver.error() == sloth::DisplayStreamError::BadChecksum);
  }
  const uint8_t malformed[] = {0, 0, 0};
  assert(send(receiver, moss_wire::packet(moss_wire::Lz4Rect, nonce, sequence + 1, 0, 2,
              malformed, sizeof(malformed), 0, 2), 10) == E::Rejected);
  assert(receiver.error() == sloth::DisplayStreamError::BadPayload);
}

static void extensionInteroperability() {
  using E = sloth::DisplayStreamEvent;
  WireReceiver receiver;
  const uint64_t nonce = 0x1111222233334444ULL;
  receiver.begin(nonce, 0);
  const uint8_t hello[] = {0xE0, 1, 0xE0, 1, 0, 0x3C, 1, 0};
  assert(send(receiver, moss_wire::packet(moss_wire::Hello, nonce, 0, 0, 0, hello, sizeof(hello)), 1) == E::Ready);
  std::vector<uint8_t> pixels(480 * 2 * 2, 0x56);
  const auto compressed = moss_wire::rle(pixels.data(), pixels.size());
  assert(send(receiver, moss_wire::packet(moss_wire::RleRect, nonce, 1, 100, 2,
                                        compressed.data(), compressed.size()), 2) == E::Rectangle);
  assert(receiver.rectangle().scale == 1 && receiver.rectangle().rle);
  assert(receiver.rectangle().y == 100 && receiver.rectangle().width == 480);
  pixels.resize(240 * 8 * 2);
  for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i);
  assert(send(receiver, moss_wire::packet(moss_wire::ScaledRect, nonce, 2, 13, 8,
                                        pixels.data(), pixels.size()), 3) == E::Rectangle);
  assert(receiver.rectangle().scale == 2 && !receiver.rectangle().rle);
  assert(receiver.rectangle().y == 13 && receiver.rectangle().width == 240 && receiver.rectangle().height == 8);
  pixels.assign(240 * 2, 0xAB);
  const auto small = moss_wire::rle(pixels.data(), pixels.size());
  const auto packet = moss_wire::packet(moss_wire::ScaledRleRect, nonce, 3, 239, 1, small.data(), small.size());
  assert(send(receiver, packet, 4) == E::Rectangle);
  assert(receiver.rectangle().scale == 2 && receiver.rectangle().rle && receiver.rectangle().y == 239);
  assert(send(receiver, packet, 5) == E::Duplicate);
}

static void audioWireInteroperability() {
  using E = sloth::DisplayStreamEvent;
  using T = sloth::DisplayPacketType;
  using Error = sloth::DisplayStreamError;
  static_assert(moss_wire::AudioConfig == 14 && moss_wire::AudioPCM == 15, "Audio wire IDs");
  WireReceiver receiver;
  const uint64_t nonce = 0xA111222233334444ULL;
  receiver.begin(nonce, 0);
  const uint8_t config[] = {1, 1, 60, 0};
  const auto configuration = moss_wire::packet(moss_wire::AudioConfig, nonce, 0, 0, 0, config, sizeof(config));
  assert(send(receiver, configuration, 0) == E::Rejected && receiver.error() == Error::WrongState);
  const uint8_t hello[] = {0xE0, 1, 0xE0, 1, 0, 0x3C, 1, 0};
  assert(send(receiver, moss_wire::packet(moss_wire::Hello, nonce, 0, 0, 0, hello, sizeof(hello)), 1) == E::Ready);
  const uint8_t pixel[] = {0x34, 0x12};
  const auto rectangle = moss_wire::packet(moss_wire::ScaledRect, nonce, 1, 2, 1, pixel, sizeof(pixel), 3, 1);
  assert(send(receiver, rectangle, 2) == E::Rectangle);
  assert(send(receiver, configuration, 3) == E::AudioConfigured);
  assert(receiver.audioBytes() == sizeof(config) && !memcmp(receiver.audioPayload(), config, sizeof(config)));
  std::vector<uint8_t> pcm(3200);
  for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<uint8_t>(i * 37);
  const auto audio = moss_wire::packet(moss_wire::AudioPCM, nonce, 0, 0, 0, pcm.data(), pcm.size());
  assert(send(receiver, audio, 4) == E::AudioSamples);
  assert(receiver.audioBytes() == pcm.size() && !memcmp(receiver.audioPayload(), pcm.data(), pcm.size()));
  assert(receiver.sequence() == 1 && receiver.lastType() == T::ScaledRectangle);
  assert(send(receiver, rectangle, 5) == E::Duplicate && !receiver.rectangle().pixels);
  assert(send(receiver, moss_wire::packet(moss_wire::AudioPCM, nonce + 1, 0, 0, 0, pcm.data(), pcm.size()), 6) == E::Rejected);
  assert(receiver.error() == Error::WrongSession);
  assert(send(receiver, moss_wire::packet(moss_wire::AudioPCM, nonce, 1, 0, 0, pcm.data(), pcm.size()), 7) == E::Rejected);
  assert(receiver.error() == Error::BadSequence);
  // The host builder always clears geometry for non-rectangle packet types.
  assert(moss_wire::packet(moss_wire::AudioPCM, nonce, 0, 1, 2, pcm.data(), pcm.size(), 3, 4) == audio);
  assert(send(receiver, moss_wire::packet(moss_wire::AudioPCM, nonce, 0, 0, 0, pcm.data(), pcm.size() - 1), 9) == E::Rejected);
  assert(receiver.error() == Error::BadPayload);
  pcm.resize(3202);
  assert(send(receiver, moss_wire::packet(moss_wire::AudioPCM, nonce, 0, 0, 0, pcm.data(), pcm.size()), 10) == E::Rejected);
  assert(receiver.error() == Error::BadPayload);
  assert(send(receiver, moss_wire::packet(moss_wire::AudioPCM, nonce, 0), 11) == E::Rejected);
  assert(receiver.error() == Error::BadPayload);
  const uint8_t tooLoud[] = {1, 1, 61, 0};
  assert(send(receiver, moss_wire::packet(moss_wire::AudioConfig, nonce, 0, 0, 0, tooLoud, sizeof(tooLoud)), 12) == E::Rejected);
  assert(receiver.error() == Error::BadPayload && receiver.sequence() == 1);
  assert(send(receiver, configuration, 3004) == E::AudioConfigured);
  assert(receiver.poll(3005) == E::Timeout); // Last visual activity was retry at t=5.
}

int main() {
  audioWireInteroperability();
  deltaPlannerAndRle();
  croppedAcknowledgedPixelsAndCodecs();
  croppedLz4WireInteroperability();
  extensionInteroperability();
  using E = sloth::DisplayStreamEvent;
  const uint8_t check[] = "123456789";
  assert(moss_wire::crc32(check, 9) == 0xCBF43926u);
  WireReceiver receiver;
  const uint64_t nonce = 0x0123456789ABCDEFULL;
  receiver.begin(nonce, 10);
  uint8_t status = 1;
  assert(send(receiver, moss_wire::packet(moss_wire::HostStatus, nonce, 0, 0, 0, &status, 1), 20) == E::PermissionNeeded);
  assert(receiver.waiting() && !receiver.connected());
  const uint8_t hello[] = {0xE0, 1, 0xE0, 1, 0, 0x3C, 1, 0};
  assert(send(receiver, moss_wire::packet(moss_wire::Hello, nonce, 0, 0, 0, hello, sizeof(hello)), 10000) == E::Ready);
  std::vector<uint8_t> pixels(moss_wire::maxPayload);
  for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i);
  for (unsigned row = 0; row < 480; row += 16) {
    const uint32_t seq = row / 16 + 1;
    const auto packet = moss_wire::packet(moss_wire::Rect, nonce, seq, row, 16, pixels.data(), pixels.size());
    assert(send(receiver, packet, 10100 + row) == E::Rectangle);
    const auto& rect = receiver.rectangle();
    assert(rect.x == 0 && rect.y == row && rect.width == 480 && rect.height == 16 && rect.bytes == pixels.size());
    assert(std::memcmp(rect.pixels, pixels.data(), pixels.size()) == 0);
    assert(send(receiver, packet, 10101 + row) == E::Duplicate);
  }
  assert(send(receiver, moss_wire::packet(moss_wire::Ping, nonce, 31), 11000) == E::Pong);
  // Device button exit can interrupt a host packet. A fresh delimiter RELEASE
  // must still unlock the receiver; its sequence is intentionally ignored.
  const auto partial = moss_wire::packet(moss_wire::Rect, nonce, 32, 0, 16, pixels.data(), pixels.size());
  for (size_t i = 0; i < 500; ++i) receiver.feed(partial[i], 11100);
  receiver.cancel();
  assert(send(receiver, moss_wire::packet(moss_wire::Release, nonce, 999), 11200) == E::Released);
  receiver.begin(nonce + 1, 12000);
  assert(send(receiver, moss_wire::packet(moss_wire::Stop, nonce + 1, 0), 12001) == E::Stopped);
  assert(send(receiver, moss_wire::packet(moss_wire::Release, nonce + 1, 1), 12002) == E::Released);
  std::puts("Host COBS/CRC protocol interoperates with firmware: acknowledged dirty bands, RLE/scaled rectangles, full frame, retries, heartbeat, button exit, permission cancel");
}
