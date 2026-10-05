#include "../firmware/sloth_pet/display_stream.h"
#include "../firmware/sloth_pet/src/vendor/lz4/lz4.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace sloth;
typedef std::vector<uint8_t> Bytes;
typedef DisplayStreamEvent Event;
typedef DisplayPacketType Type;
typedef DisplayStreamError Error;
static const uint64_t kNonce = UINT64_C(0x0123456789abcdef);
static_assert(sizeof(DisplayStream) <= 512, "Idle receiver must not retain image packet storage");

// Every ordinary fixture owns its own output storage, exactly as firmware
// borrows its framebuffer. Helpers accept the production base type.
struct TestStream : DisplayStream {
  alignas(uint16_t) uint8_t workspace[kDisplayMaxPayload];
  TestStream() { assert(setDecodeWorkspace(workspace, sizeof(workspace))); }
};

static void put16(Bytes& b, size_t at, uint16_t value) {
  b[at] = static_cast<uint8_t>(value); b[at + 1] = static_cast<uint8_t>(value >> 8);
}
static void put32(Bytes& b, size_t at, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(value >> (8 * i));
}
static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = UINT32_MAX;
  while (n--) {
    crc ^= *p++;
    for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
static void seal(Bytes& b) { put32(b, b.size() - 4, crc32(b.data(), b.size() - 4)); }
static Bytes raw(Type type, uint64_t nonce, uint32_t seq, const Bytes& payload = Bytes(),
                 uint16_t y = 0, uint16_t height = 0) {
  Bytes b(36 + payload.size(), 0);
  memcpy(b.data(), "MOSD", 4);
  b[4] = 1; b[5] = static_cast<uint8_t>(type);
  for (unsigned i = 0; i < 8; ++i) b[8 + i] = static_cast<uint8_t>(nonce >> (8 * i));
  put32(b, 16, seq);
  const bool scaled = type == Type::ScaledRectangle || type == Type::ScaledRleRectangle || type == Type::ScaledLz4Rectangle;
  put16(b, 22, y); put16(b, 24, height ? (scaled ? 240 : 480) : 0); put16(b, 26, height);
  put16(b, 28, static_cast<uint16_t>(payload.size()));
  if (!payload.empty()) memcpy(b.data() + 32, payload.data(), payload.size());
  seal(b);
  return b;
}
static Bytes wire(const Bytes& decoded) {
  Bytes b(2, 0);
  size_t codeAt = 1;
  uint8_t code = 1;
  for (size_t i = 0; i < decoded.size(); ++i) {
    if (decoded[i] == 0) {
      b[codeAt] = code; codeAt = b.size(); b.push_back(0); code = 1;
    } else {
      b.push_back(decoded[i]);
      if (++code == 255) {
        b[codeAt] = code; codeAt = b.size(); b.push_back(0); code = 1;
      }
    }
  }
  b[codeAt] = code; b.push_back(0);
  return b;
}
static Bytes hello(uint64_t nonce = kNonce) {
  const uint8_t data[] = {0xe0, 1, 0xe0, 1, 0, 0x3c, 1, 0};
  return raw(Type::Hello, nonce, 0, Bytes(data, data + sizeof(data)));
}
static Event receive(DisplayStream& stream, const Bytes& b, uint32_t now = 0) {
  Event event = Event::None;
  for (size_t i = 0; i < b.size(); ++i) {
    const Event next = stream.feed(b[i], now);
    if (next != Event::None) event = next;
  }
  return event;
}
static Event send(DisplayStream& stream, const Bytes& b, uint32_t now = 0) {
  return receive(stream, wire(b), now);
}
static void connect(DisplayStream& stream, uint32_t now = 0) {
  stream.begin(kNonce, now);
  assert(send(stream, hello(), now) == Event::Ready);
  assert(stream.connected() && !stream.waiting());
}
static Bytes fromHex(const char* text) {
  Bytes b;
  while (*text) {
    unsigned v;
    assert(sscanf(text, "%2x", &v) == 1);
    b.push_back(static_cast<uint8_t>(v)); text += 2;
  }
  return b;
}

static void discoveryAndGoldenHandshake() {
  TestStream stream;
  assert(send(stream, raw(Type::Query, 0, 0)) == Event::Query);
  assert(!stream.connected() && !stream.waiting());
  assert(send(stream, hello()) == Event::Rejected);
  assert(stream.error() == Error::WrongSession);
  assert(send(stream, raw(Type::Release, 0, 99)) == Event::Released);
  stream.begin(kNonce, 0);
  assert(stream.waiting());
  assert(stream.poll(3000000) == Event::None);  // No host required until HELLO.
  // Independent Python struct/zlib fixture, including both framing delimiters.
  const Bytes golden = fromHex("00074d4f534401010109efcdab896745230101010101010101010101010208010105e001e001033c0105019f587f00");
  assert(wire(hello()) == golden);
  assert(receive(stream, golden, 3000000) == Event::Ready);
  assert(stream.sequence() == 0 && stream.lastType() == Type::Hello);
  assert(receive(stream, golden, 3000100) == Event::Duplicate);
  assert(stream.lastType() == Type::Hello);
  assert(send(stream, raw(Type::Query, 0, 0), 3000100) == Event::Query);
  assert(stream.sequence() == 0 && stream.connected());
}

static void rectanglesAndSequences() {
  TestStream stream;
  connect(stream);
  Bytes pixels(kDisplayMaxPayload);
  for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i);
  const Bytes rectangle = raw(Type::Rectangle, kNonce, 1, pixels, 464, 16);
  assert(send(stream, rectangle, 10) == Event::Rectangle);
  const DisplayRectangle& result = stream.rectangle();
  assert(result.x == 0 && result.y == 464 && result.width == 480 && result.height == 16);
  assert(result.bytes == pixels.size());
  assert(result.scale == 1 && !result.rle);
  assert(memcmp(result.pixels, pixels.data(), pixels.size()) == 0);
  assert(stream.sequence() == 1);
  assert(send(stream, rectangle, 20) == Event::Duplicate);
  assert(stream.rectangle().pixels == nullptr);  // Duplicates must never redraw.
  Bytes different = rectangle;
  different[32] ^= 1; seal(different);
  assert(send(stream, different, 30) == Event::Rejected);
  assert(stream.error() == Error::BadSequence);
  assert(send(stream, raw(Type::Ping, kNonce, 3), 40) == Event::Rejected);
  assert(stream.error() == Error::BadSequence);
  assert(send(stream, raw(Type::Ping, kNonce, 2), 50) == Event::Pong);
  assert(send(stream, rectangle, 60) == Event::Rejected);  // Not the latest packet.
  assert(send(stream, hello(), 70) == Event::Rejected);
  assert(send(stream, raw(Type::Rectangle, kNonce, 3, Bytes(1920, 0xff), 478, 2), 80) == Event::Rectangle);
  assert(stream.rectangle().pixels[1919] == 0xff);
  assert(stream.feed(0, 81) == Event::None);
  assert(stream.rectangle().pixels == nullptr);
}

static void malformedFramesAndResync() {
  TestStream stream;
  connect(stream);
  Bytes sample = raw(Type::Ping, kNonce, 1);
  struct Mutation { size_t index; uint8_t value; Error error; };
  const Mutation cases[] = {
    {0, 'N', Error::BadMagic}, {4, 2, Error::BadVersion}, {5, 16, Error::UnknownType},
    {6, 1, Error::Reserved}, {7, 1, Error::Reserved}, {30, 1, Error::Reserved},
    {31, 1, Error::Reserved}, {8, 0, Error::WrongSession}, {16, 0, Error::BadSequence},
    {20, 1, Error::BadCoordinates}, {22, 1, Error::BadCoordinates},
    {24, 1, Error::BadCoordinates}, {26, 1, Error::BadCoordinates}, {28, 1, Error::BadLength}
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    Bytes b = sample; b[cases[i].index] = cases[i].value; seal(b);
    assert(send(stream, b) == Event::Rejected);
    assert(stream.error() == cases[i].error);
    assert(stream.sequence() == 0 && stream.connected());
    assert(send(stream, raw(Type::Query, 0, 0)) == Event::Query);
  }
  sample.back() ^= 1;
  assert(send(stream, sample) == Event::Rejected && stream.error() == Error::BadChecksum);
  const uint8_t badCobs[] = {0, 255, 1, 0};
  assert(receive(stream, Bytes(badCobs, badCobs + sizeof(badCobs))) == Event::Rejected);
  assert(stream.error() == Error::BadCobs);
  assert(send(stream, Bytes(35, 1)) == Event::Rejected && stream.error() == Error::BadLength);
  Bytes overflow(kDisplayReceiveBytes + 1, 1);
  overflow.insert(overflow.begin(), 0); overflow.push_back(0);
  assert(receive(stream, overflow) == Event::Rejected && stream.error() == Error::FrameTooLarge);
  assert(send(stream, raw(Type::Ping, kNonce, 1)) == Event::Pong);
}

static void rectangleConstraints() {
  TestStream stream;
  connect(stream);
  Bytes valid = raw(Type::Rectangle, kNonce, 1, Bytes(1920, 0), 0, 2);
  const unsigned offsets[] = {20, 22, 24, 26};
  for (unsigned i = 0; i < 4; ++i) {
    Bytes bad = valid; put16(bad, offsets[i], 1); seal(bad);
    assert(send(stream, bad) == Event::Rejected && stream.error() == Error::BadCoordinates);
  }
  const uint16_t invalidHeight[] = {0, 1, 3, 17, 18, 65534};
  for (size_t i = 0; i < sizeof(invalidHeight) / sizeof(invalidHeight[0]); ++i) {
    Bytes bad = valid; put16(bad, 26, invalidHeight[i]); seal(bad);
    assert(send(stream, bad) == Event::Rejected && stream.error() == Error::BadCoordinates);
  }
  Bytes bad = valid; put16(bad, 22, 480); seal(bad);
  assert(send(stream, bad) == Event::Rejected && stream.error() == Error::BadCoordinates);
  assert(send(stream, raw(Type::Rectangle, kNonce, 1, Bytes(1918), 0, 2)) == Event::Rejected);
  assert(stream.error() == Error::BadPayload);
  assert(send(stream, raw(Type::Ping, kNonce, 1, Bytes(1))) == Event::Rejected);
  assert(stream.error() == Error::BadPayload);
  assert(send(stream, valid) == Event::Rectangle);
}

static void handshakeStatusAndCleanup() {
  TestStream stream;
  stream.begin(kNonce, 0);
  Bytes bad = hello(); bad[38] = 2; seal(bad);
  assert(send(stream, bad) == Event::Rejected && stream.error() == Error::BadCapabilities);
  assert(send(stream, raw(Type::Rectangle, kNonce, 1, Bytes(1920), 0, 2)) == Event::Rejected);
  assert(stream.error() == Error::WrongState);
  assert(send(stream, raw(Type::HostStatus, kNonce, 0, Bytes(1, 1)), 10000) == Event::PermissionNeeded);
  assert(stream.poll(100000) == Event::None && stream.waiting());
  assert(send(stream, raw(Type::HostStatus, kNonce, 0, Bytes(1, 2)), 100000) == Event::HostError);
  assert(send(stream, raw(Type::HostStatus, kNonce, 0, Bytes(1, 3)), 100000) == Event::Rejected);
  assert(send(stream, raw(Type::Stop, kNonce, 0), 100000) == Event::Stopped);
  assert(stream.sequence() == 0 && !stream.waiting() && !stream.connected());
  assert(send(stream, hello(), 100000) == Event::Rejected && stream.error() == Error::WrongState);
  assert(send(stream, raw(Type::Stop, kNonce, 0), 100000) == Event::Stopped);
  assert(send(stream, raw(Type::Release, kNonce, 900), 100000) == Event::Released);

  connect(stream, 100000);
  assert(send(stream, raw(Type::Ping, kNonce, 1), 100001) == Event::Pong);
  assert(send(stream, raw(Type::Stop, kNonce, 2), 100002) == Event::Stopped);
  assert(stream.sequence() == 2 && stream.nonce() == kNonce);
  assert(send(stream, raw(Type::Release, kNonce, 0), 100003) == Event::Released);
  stream.begin(kNonce + 1, 100004);
  assert(send(stream, raw(Type::Release, kNonce, 100), 100005) == Event::Rejected);
  assert(stream.waiting());  // Stale cleanup can never release a new session.
  assert(send(stream, raw(Type::Release, kNonce + 1, UINT32_MAX), 100006) == Event::Released);
}

static void timeoutBoundariesAndRollover() {
  TestStream stream;
  const uint32_t start = UINT32_MAX - 1000;
  connect(stream, start);
  assert(stream.poll(start + 2999u) == Event::None);
  assert(stream.poll(start + 3000u) == Event::Timeout);
  assert(stream.poll(start + 6000u) == Event::None);
  assert(!stream.connected() && stream.nonce() == kNonce);
  assert(send(stream, raw(Type::Release, kNonce, 9), start + 6000u) == Event::Released);

  connect(stream, 0);
  assert(send(stream, raw(Type::HostStatus, kNonce, 0, Bytes(1, 1)), 2900) == Event::PermissionNeeded);
  assert(send(stream, raw(Type::Query, 0, 0), 2999) == Event::Query);
  assert(stream.poll(3000) == Event::Timeout);  // Neither out-of-band message renews lease.
  connect(stream, 0);
  assert(send(stream, raw(Type::Ping, kNonce, 1), 2900) == Event::Pong);
  assert(send(stream, raw(Type::Ping, kNonce, 1), 5800) == Event::Duplicate);
  assert(stream.poll(8799) == Event::None);
  assert(stream.poll(8800) == Event::Timeout);

  stream.begin(kNonce, start);
  const Bytes packet = wire(hello());
  assert(stream.feed(0, start) == Event::None);
  assert(stream.feed(packet[1], start) == Event::None);
  assert(stream.feed(packet[2], start + 499u) == Event::None);
  assert(stream.poll(start + 500u) == Event::Rejected && stream.error() == Error::FrameTimeout);
  assert(stream.poll(start + 501u) == Event::None);
  Bytes tail(packet.begin() + 3, packet.end());
  assert(receive(stream, tail, start + 501u) == Event::None);
  assert(receive(stream, packet, start + 502u) == Event::Ready);
  assert(stream.feed(0, start + 3502u) == Event::Timeout);  // feed itself enforces the lease.
}

static void cancelDuringFrameAndBinaryNoise() {
  TestStream stream;
  connect(stream);
  const Bytes encoded = wire(raw(Type::Rectangle, kNonce, 1, Bytes(1920, 'f'), 0, 2));
  for (size_t i = 0; i < 100; ++i) assert(stream.feed(encoded[i], 1) == Event::None);
  stream.cancel();
  assert(!stream.connected() && stream.nonce() == kNonce);
  Bytes tail(encoded.begin() + 100, encoded.end());
  assert(receive(stream, tail, 2) == Event::None);
  assert(receive(stream, encoded, 3) == Event::Rejected && stream.error() == Error::WrongState);
  uint32_t random = 12345;
  for (unsigned i = 0; i < 200000; ++i) {
    random = random * 1664525u + 1013904223u;
    const Event event = stream.feed(static_cast<uint8_t>(random >> 24), 4);
    assert(event == Event::None || event == Event::Rejected);
  }
  // A fresh leading delimiter survives any leftover noise/in-flight pixels.
  assert(send(stream, raw(Type::Release, kNonce, 42), 5) == Event::Released);
  stream.begin(0, 6);  // Reserved nonce never grants display permission.
  assert(!stream.waiting());
  assert(send(stream, hello(0), 6) == Event::Rejected && stream.error() == Error::WrongState);
}

static Bytes run(uint16_t count, uint16_t pixel = 0xf800) {
  Bytes b(4);
  put16(b, 0, count); put16(b, 2, pixel);
  return b;
}

static void extendedRectanglesAndRetries() {
  TestStream stream;
  connect(stream);
  // Independent struct/zlib fixture: bottom logical row, all red, scaled RLE.
  const Bytes golden = fromHex("00074d4f53440109010aefcdab8967452301010101010102ef02f002010204010102f00106f8fbc4bfad00");
  const Bytes bottom = raw(Type::ScaledRleRectangle, kNonce, 1, run(240), 239, 1);
  assert(wire(bottom) == golden);
  assert(receive(stream, golden, 10) == Event::Rectangle);
  const DisplayRectangle& result = stream.rectangle();
  assert(result.scale == 2 && result.rle && result.width == 240 && result.height == 1);
  assert(result.x == 0 && result.y == 239 && result.bytes == 4);
  const uint8_t expected[] = {240, 0, 0, 0xf8};
  assert(memcmp(result.pixels, expected, sizeof(expected)) == 0);
  assert(receive(stream, golden, 20) == Event::Duplicate);
  assert(stream.lastType() == Type::ScaledRleRectangle && stream.rectangle().pixels == nullptr);
  // Format can change per stripe, with no extra handshake or sequence reset.
  Bytes pixels(240 * 8 * 2);
  for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i * 71);
  const Bytes scaled = raw(Type::ScaledRectangle, kNonce, 2, pixels, 232, 8);
  assert(send(stream, scaled, 30) == Event::Rectangle);
  assert(stream.rectangle().scale == 2 && !stream.rectangle().rle);
  assert(stream.rectangle().bytes == 3840 && stream.rectangle().height == 8);
  assert(memcmp(stream.rectangle().pixels, pixels.data(), pixels.size()) == 0);
  assert(send(stream, scaled, 40) == Event::Duplicate);
  const Bytes native = raw(Type::RleRectangle, kNonce, 3, run(7680, 0x07e0), 464, 16);
  assert(send(stream, native, 50) == Event::Rectangle);
  assert(stream.rectangle().scale == 1 && stream.rectangle().rle);
  assert(stream.rectangle().height == 16 && stream.rectangle().bytes == 4);
  assert(send(stream, native, 60) == Event::Duplicate);
  assert(send(stream, bottom, 70) == Event::Rejected && stream.error() == Error::BadSequence);
  Bytes changed = native; changed[34] ^= 1; seal(changed);
  assert(send(stream, changed, 80) == Event::Rejected && stream.error() == Error::BadSequence);
  // Legacy native raw still follows an extended packet in the same session.
  assert(send(stream, raw(Type::Rectangle, kNonce, 4, Bytes(1920), 478, 2), 90) == Event::Rectangle);
  assert(stream.rectangle().scale == 1 && !stream.rectangle().rle);

  // Maximum encoded RLE payload: 3840 differently colored two-pixel runs.
  Bytes runs(kDisplayMaxPayload);
  for (size_t at = 0; at < runs.size(); at += 4) {
    put16(runs, at, 2); put16(runs, at + 2, static_cast<uint16_t>(at));
  }
  assert(send(stream, raw(Type::RleRectangle, kNonce, 5, runs, 0, 16), 100) == Event::Rectangle);
  assert(stream.rectangle().bytes == kDisplayMaxPayload);
  assert(memcmp(stream.rectangle().pixels, runs.data(), runs.size()) == 0);
  // Runs may span row boundaries; counts describe row-major pixels, not rows.
  Bytes spanning = run(241, 0x1234);
  const Bytes final = run(239, 0xabcd);
  spanning.insert(spanning.end(), final.begin(), final.end());
  assert(send(stream, raw(Type::ScaledRleRectangle, kNonce, 6, spanning, 1, 2), 110) == Event::Rectangle);
  assert(stream.rectangle().y == 1 && stream.rectangle().height == 2);
}

static void extendedRectangleValidation() {
  const Type types[] = {Type::RleRectangle, Type::ScaledRectangle, Type::ScaledRleRectangle};
  for (size_t typeIndex = 0; typeIndex < sizeof(types) / sizeof(types[0]); ++typeIndex) {
    const Type type = types[typeIndex];
    const bool scaled = type != Type::RleRectangle;
    const bool encoded = type != Type::ScaledRectangle;
    const uint16_t count = scaled ? 240 : 960;
    const uint16_t height = scaled ? 1 : 2;
    const Bytes data = encoded ? run(count) : Bytes(count * 2, 0x7c);
    const Bytes valid = raw(type, kNonce, 1, data, 0, height);
    TestStream stream;
    stream.begin(kNonce, 0);
    assert(send(stream, valid) == Event::Rejected && stream.error() == Error::WrongState);
    assert(send(stream, hello()) == Event::Ready);
    struct BadGeometry { size_t offset; uint16_t value; };
    const BadGeometry geometry[] = {
      {20, 1}, {22, static_cast<uint16_t>(scaled ? 240 : 480)},
      {24, static_cast<uint16_t>(scaled ? 480 : 482)}, {26, 0},
      {26, static_cast<uint16_t>(scaled ? 9 : 18)}, {22, UINT16_MAX}
    };
    for (size_t i = 0; i < sizeof(geometry) / sizeof(geometry[0]); ++i) {
      Bytes bad = valid; put16(bad, geometry[i].offset, geometry[i].value); seal(bad);
      assert(send(stream, bad) == Event::Rejected && stream.error() == Error::BadCoordinates);
      assert(stream.sequence() == 0 && stream.rectangle().pixels == nullptr);
    }
    if (!scaled) {
      Bytes bad = valid; put16(bad, 22, 1); seal(bad);
      assert(send(stream, bad) == Event::Rejected && stream.error() == Error::BadCoordinates);
      bad = valid; put16(bad, 26, 3); seal(bad);
      assert(send(stream, bad) == Event::Rejected && stream.error() == Error::BadCoordinates);
    }
    Bytes corrupt = valid; corrupt.back() ^= 1;
    assert(send(stream, corrupt) == Event::Rejected && stream.error() == Error::BadChecksum);
    Bytes wrongNonce = valid; wrongNonce[8] ^= 1; seal(wrongNonce);
    assert(send(stream, wrongNonce) == Event::Rejected && stream.error() == Error::WrongSession);
    const Bytes shortData(data.begin(), data.end() - 1);
    assert(send(stream, raw(type, kNonce, 1, shortData, 0, height)) == Event::Rejected);
    assert(stream.error() == Error::BadPayload);
    if (encoded) {
      const uint16_t badCount[] = {0, static_cast<uint16_t>(count - 1),
                                 static_cast<uint16_t>(count + 1), UINT16_MAX};
      for (size_t i = 0; i < sizeof(badCount) / sizeof(badCount[0]); ++i) {
        assert(send(stream, raw(type, kNonce, 1, run(badCount[i]), 0, height)) == Event::Rejected);
        assert(stream.error() == Error::BadPayload);
      }
      assert(send(stream, raw(type, kNonce, 1, Bytes(), 0, height)) == Event::Rejected);
      assert(stream.error() == Error::BadPayload);
      Bytes extra = data;
      const Bytes one = run(1);
      extra.insert(extra.end(), one.begin(), one.end());
      assert(send(stream, raw(type, kNonce, 1, extra, 0, height)) == Event::Rejected);
      assert(stream.error() == Error::BadPayload);  // Count total, not just each run.
    }
    assert(send(stream, valid, 2900) == Event::Rectangle);
    assert(stream.poll(5899) == Event::None);  // All formats renew the same lease.
    assert(stream.poll(5900) == Event::Timeout);
    assert(send(stream, valid, 5901) == Event::Rejected && stream.error() == Error::WrongState);
    assert(send(stream, raw(Type::Release, kNonce, 100), 5902) == Event::Released);
  }
}

static void wifiProvisioning() {
  TestStream stream;
  stream.begin(kNonce, 0);
  const Bytes credentials = {4, 8, 'M','o','s','s', 't','e','s','t','p','a','s','s'};
  assert(send(stream, raw(Type::ConfigureWiFi, kNonce, 0, credentials)) == Event::WifiConfigure);
  assert(stream.waiting() && !stream.connected());
  assert(memcmp(stream.wifiCredentials(), credentials.data(), credentials.size()) == 0);
  stream.clearCredentials();
  for (unsigned i = 0; i < credentials.size(); ++i) assert(stream.wifiCredentials()[i] == 0);
  assert(send(stream, raw(Type::ConfigureWiFi, kNonce + 1, 0, credentials)) == Event::Rejected);
  assert(stream.error() == Error::WrongSession);
  assert(send(stream, raw(Type::ConfigureWiFi, kNonce, 1, credentials)) == Event::Rejected);
  assert(stream.error() == Error::BadSequence);
  for (const Bytes& bad : {Bytes{}, Bytes{1}, Bytes{0, 0}, Bytes{33,0}, Bytes{1,7,'x'},
                          Bytes{1,0,0}, Bytes{1,0,'x','y'}, Bytes{1,64,'x'}}) {
    assert(send(stream, raw(Type::ConfigureWiFi, kNonce, 0, bad)) == Event::Rejected);
    assert(stream.error() == Error::BadPayload);
  }
  Bytes maximum(2 + 32 + 63, 'x'); maximum[0] = 32; maximum[1] = 63;
  assert(send(stream, raw(Type::ConfigureWiFi, kNonce, 0, maximum)) == Event::WifiConfigure);
  assert(send(stream, raw(Type::ConfigureWiFi, kNonce, 0, Bytes{1,0,'x'})) == Event::WifiConfigure);
  assert(send(stream, hello()) == Event::Ready);
  assert(send(stream, raw(Type::ConfigureWiFi, kNonce, 0, credentials)) == Event::Rejected);
  assert(stream.error() == Error::WrongState);
  assert(send(stream, raw(Type::Release, kNonce, 0)) == Event::Released);
}

static uint32_t randomWord(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

static void randomizedChecksumCompatibility() {
  // Packet CRCs come from the independent bit-at-a-time reference above. Mix
  // every permitted raw strip height, payload bytes, sequence values and COBS
  // zero runs; corrupt a payload bit without resealing to prove rejection.
  TestStream stream;
  connect(stream);
  uint32_t random = UINT32_C(0xa630d275);
  for (uint32_t sequence = 1; sequence <= 96; ++sequence) {
    const bool scaled = (sequence & 1) != 0;
    const uint16_t height = static_cast<uint16_t>((1 + randomWord(random) % 8) * (scaled ? 1 : 2));
    const uint16_t width = scaled ? 240 : 480;
    Bytes pixels(static_cast<size_t>(width) * height * 2);
    for (size_t i = 0; i < pixels.size(); ++i)
      pixels[i] = static_cast<uint8_t>(randomWord(random));
    const Bytes rectangle = raw(scaled ? Type::ScaledRectangle : Type::Rectangle,
                                kNonce, sequence, pixels, 0, height);
    Bytes corrupted = rectangle;
    corrupted[32 + randomWord(random) % pixels.size()] ^= static_cast<uint8_t>(1u << (randomWord(random) % 8));
    assert(send(stream, corrupted, sequence) == Event::Rejected);
    assert(stream.error() == Error::BadChecksum && stream.sequence() == sequence - 1);
    assert(send(stream, rectangle, sequence) == Event::Rectangle);
    assert(stream.rectangle().bytes == pixels.size());
    assert(memcmp(stream.rectangle().pixels, pixels.data(), pixels.size()) == 0);
    // Even an ordinary non-delimiter payload byte invalidates the prior view.
    assert(stream.feed(1, sequence) == Event::None);
    const DisplayRectangle& cleared = stream.rectangle();
    assert(!cleared.pixels && !cleared.x && !cleared.y && !cleared.width &&
           !cleared.height && !cleared.bytes && cleared.scale == 1 && !cleared.rle);
    assert(send(stream, rectangle, sequence) == Event::Duplicate);
    assert(!stream.rectangle().pixels);
  }
}

static Bytes compressBlock(const Bytes& pixels) {
  Bytes encoded(static_cast<size_t>(LZ4_compressBound(static_cast<int>(pixels.size()))));
  const int count = LZ4_compress_default(reinterpret_cast<const char*>(pixels.data()),
      reinterpret_cast<char*>(encoded.data()), static_cast<int>(pixels.size()), static_cast<int>(encoded.size()));
  assert(count > 0);
  encoded.resize(static_cast<size_t>(count));
  return encoded;
}

static Bytes cropped(Type type, uint32_t sequence, const Bytes& payload,
                     uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
  Bytes packet = raw(type, kNonce, sequence, payload, y, height);
  put16(packet, 20, x); put16(packet, 24, width); seal(packet);
  return packet;
}

static void decodeWorkspaceOwnership() {
  struct Guarded {
    uint32_t before = UINT32_C(0xdec0beef);
    alignas(uint16_t) uint8_t bytes[kDisplayMaxPayload];
    uint32_t after = UINT32_C(0x12345678);
  } first, second;
  memset(first.bytes, 0xa5, sizeof(first.bytes));
  memset(second.bytes, 0x5a, sizeof(second.bytes));
  const Bytes pixels(kDisplayMaxPayload, 0x37);
  const Bytes packet = cropped(Type::Lz4Rectangle, 1, compressBlock(pixels), 0, 0, 480, 16);
  DisplayStream stream; // Deliberately no workspace: non-LZ4 framing still works.
  connect(stream);
  assert(send(stream, packet, 100) == Event::Rejected && stream.error() == Error::BadPayload);
  assert(stream.sequence() == 0 && !stream.rectangle().pixels);
  assert(send(stream, raw(Type::Rectangle, kNonce, 1, Bytes(1920, 0x33), 0, 2), 101) == Event::Rectangle);
  assert(send(stream, raw(Type::AudioConfig, kNonce, 0, Bytes{1, 1, 30, 0}), 102) == Event::AudioConfigured);
  Bytes jpeg(256, 0x22); jpeg[0]=0xff; jpeg[1]=0xd8; jpeg[254]=0xff; jpeg[255]=0xd9;
  assert(send(stream, cropped(Type::ScaledJpegFrame, 2, jpeg, 0, 0, 240, 240), 103) == Event::JpegFrame);
  // Busy changes cannot revoke or replace a binding midway through a session.
  assert(!stream.setDecodeWorkspace(first.bytes, sizeof(first.bytes)));
  stream.cancel();
  assert(!stream.setDecodeWorkspace(first.bytes, sizeof(first.bytes)-1));
  connect(stream);
  assert(send(stream, packet) == Event::Rejected && stream.error() == Error::BadPayload);
  stream.cancel();
  assert(!stream.setDecodeWorkspace(nullptr, sizeof(first.bytes)));
  assert(!stream.setDecodeWorkspace(first.bytes, 0));
  assert(!stream.setDecodeWorkspace(first.bytes+1, sizeof(first.bytes)));
  assert(!stream.setDecodeWorkspace(first.bytes, SIZE_MAX));
  assert(stream.setDecodeWorkspace(first.bytes, sizeof(first.bytes)));
  stream.begin(kNonce, 0);
  assert(!stream.setDecodeWorkspace(second.bytes, sizeof(second.bytes))); // Waiting is busy too.
  assert(send(stream, packet, 10) == Event::Rejected && stream.error() == Error::WrongState);
  for (uint8_t byte : first.bytes) assert(byte == 0xa5); // Waiting must not damage a menu framebuffer.
  assert(send(stream, hello()) == Event::Ready);
  Bytes wrong = packet; wrong[8] ^= 1; seal(wrong);
  assert(send(stream, wrong) == Event::Rejected && stream.error() == Error::WrongSession);
  wrong = packet; put32(wrong, 16, 2); seal(wrong);
  assert(send(stream, wrong) == Event::Rejected && stream.error() == Error::BadSequence);
  for (uint8_t byte : first.bytes) assert(byte == 0xa5);
  assert(send(stream, packet) == Event::Rectangle && stream.rectangle().pixels == first.bytes);
  assert(memcmp(first.bytes, pixels.data(), pixels.size()) == 0);
  for (uint8_t byte : second.bytes) assert(byte == 0x5a);
  // A retry does not write scratch again, and still cannot expose a stale view.
  memset(first.bytes, 0x6c, sizeof(first.bytes));
  assert(send(stream, packet) == Event::Duplicate && !stream.rectangle().pixels);
  for (uint8_t byte : first.bytes) assert(byte == 0x6c);
  stream.cancel();
  connect(stream); // begin/cancel preserve the binding.
  assert(send(stream, packet) == Event::Rectangle && stream.rectangle().pixels == first.bytes);
  stream.cancel();
  // Even an idle query partially in flight must not change workspace ownership.
  const Bytes query = wire(raw(Type::Query, 0, 0));
  stream.feed(0, 0); stream.feed(query[1], 0);
  assert(!stream.setDecodeWorkspace(second.bytes, sizeof(second.bytes)));
  stream.cancel();
  assert(stream.setDecodeWorkspace(second.bytes, sizeof(second.bytes)));
  connect(stream);
  assert(send(stream, packet) == Event::Rectangle && stream.rectangle().pixels == second.bytes);
  assert(memcmp(second.bytes, pixels.data(), pixels.size()) == 0);
  stream.cancel();
  assert(stream.setDecodeWorkspace(nullptr, 0));
  connect(stream);
  assert(send(stream, packet, 2999) == Event::Rejected && stream.error() == Error::BadPayload);
  assert(stream.poll(3000) == Event::Timeout); // Missing workspace never extends a lease.
  assert(first.before == UINT32_C(0xdec0beef) && first.after == UINT32_C(0x12345678));
  assert(second.before == UINT32_C(0xdec0beef) && second.after == UINT32_C(0x12345678));

  struct Adjacent {
    alignas(uint16_t) uint8_t before[kDisplayMaxPayload];
    DisplayStream receiver;
    alignas(uint16_t) uint8_t after[kDisplayMaxPayload];
  } adjacent;
  auto& receiver = adjacent.receiver;
  assert(receiver.setDecodeWorkspace(adjacent.before, sizeof(adjacent.before))); // Exact adjacency is safe.
  assert(!receiver.setDecodeWorkspace(adjacent.before+2, sizeof(adjacent.before))); // Crosses into receiver.
  assert(!receiver.setDecodeWorkspace(&receiver, sizeof(receiver)));
  assert(!receiver.setDecodeWorkspace(const_cast<uint8_t*>(receiver.wifiCredentials()), kDisplayMaxPayload));
  assert(!receiver.setDecodeWorkspace(reinterpret_cast<uint8_t*>(&receiver)+sizeof(receiver)-2, kDisplayMaxPayload));
  assert(receiver.setDecodeWorkspace(adjacent.after, sizeof(adjacent.after)));
  connect(receiver);
  assert(send(receiver, packet) == Event::Rectangle && receiver.rectangle().pixels == adjacent.after);
  assert(memcmp(adjacent.after, pixels.data(), pixels.size()) == 0);
}

static void lz4GoldenAndBoundaries() {
  // Independent raw-block fixture: literal A, offset 1 / match 474, five final
  // literals. This tests overlapping back-references without using the encoder.
  const Bytes golden = {0x1f, 'A', 1, 0, 255, 200, 0x50, 'A', 'A', 'A', 'A', 'A'};
  TestStream stream;
  connect(stream);
  assert(send(stream, cropped(Type::Lz4Rectangle, 1, golden, 360, 478, 120, 2)) == Event::Rectangle);
  assert(stream.rectangle().x == 360 && stream.rectangle().y == 478);
  assert(stream.rectangle().width == 120 && stream.rectangle().height == 2);
  assert(stream.rectangle().bytes == 480 && !stream.rectangle().rle && stream.rectangle().scale == 1);
  for (unsigned i = 0; i < 480; ++i) assert(stream.rectangle().pixels[i] == 'A');
  assert(send(stream, cropped(Type::Lz4Rectangle, 1, golden, 360, 478, 120, 2)) == Event::Duplicate);
  assert(!stream.rectangle().pixels);
  assert(send(stream, cropped(Type::ScaledLz4Rectangle, 2, golden, 0, 239, 240, 1)) == Event::Rectangle);
  assert(stream.rectangle().scale == 2 && stream.rectangle().bytes == 480);

  Bytes pixels(kDisplayMaxPayload);
  for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>((i / 128) % 32);
  const Bytes block = compressBlock(pixels);
  assert(block.size() < kDisplayMaxPayload);
  assert(send(stream, cropped(Type::Lz4Rectangle, 3, block, 0, 0, 480, 16)) == Event::Rectangle);
  assert(stream.rectangle().bytes == kDisplayMaxPayload);
  assert(memcmp(stream.rectangle().pixels, pixels.data(), pixels.size()) == 0);
  // The decoded view does not alias the COBS/provisioning receive buffer.
  const uint8_t* view = stream.rectangle().pixels;
  stream.clearCredentials();
  assert(memcmp(view, pixels.data(), pixels.size()) == 0);
  assert(stream.feed(1, 0) == Event::None && !stream.rectangle().pixels);

  // A near-maximum compressed literal payload and an odd cropped scaled width.
  pixels.resize(478 * 16 * 2);
  uint32_t random = UINT32_C(0x8d4f710b);
  for (uint8_t& pixel : pixels) pixel = static_cast<uint8_t>(randomWord(random));
  const Bytes literalBlock = compressBlock(pixels);
  assert(literalBlock.size() <= kDisplayMaxPayload && literalBlock.size() > 15000);
  assert(send(stream, cropped(Type::Lz4Rectangle, 4, literalBlock, 2, 464, 478, 16)) == Event::Rectangle);
  assert(memcmp(stream.rectangle().pixels, pixels.data(), pixels.size()) == 0);
  pixels.resize(239 * 8 * 2);
  assert(send(stream, cropped(Type::ScaledLz4Rectangle, 5, compressBlock(pixels), 1, 232, 239, 8)) == Event::Rectangle);
  assert(stream.rectangle().x == 1 && stream.rectangle().width == 239);
  assert(memcmp(stream.rectangle().pixels, pixels.data(), pixels.size()) == 0);
}

static void lz4RejectionAndLease() {
  const Bytes pixels(480, 'A');
  const Bytes validBlock = compressBlock(pixels);
  const Bytes valid = cropped(Type::ScaledLz4Rectangle, 1, validBlock, 0, 0, 240, 1);
  TestStream stream;
  connect(stream, 100);
  std::vector<Bytes> invalid = {Bytes{}, Bytes{0}, Bytes{0xf0}, Bytes{0xf0,255,255},
      Bytes{0x1f,'A',0xff,0xff,255,200,0x50,'A','A','A','A','A'},
      compressBlock(Bytes(479, 'A')), compressBlock(Bytes(481, 'A'))};
  for (size_t n = 0; n < validBlock.size(); ++n)
    invalid.emplace_back(validBlock.begin(), validBlock.begin() + n);
  Bytes trailing = validBlock; trailing.push_back(0); invalid.push_back(trailing);
  for (const Bytes& block : invalid) {
    assert(send(stream, cropped(Type::ScaledLz4Rectangle, 1, block, 0, 0, 240, 1), 3000) == Event::Rejected);
    assert(stream.error() == Error::BadPayload && stream.sequence() == 0 && !stream.rectangle().pixels);
  }
  assert(stream.poll(3099) == Event::None);
  assert(stream.poll(3100) == Event::Timeout); // Malformed blocks never renew lease.
  connect(stream);
  Bytes corrupt = valid; corrupt.back() ^= 1;
  assert(send(stream, corrupt) == Event::Rejected && stream.error() == Error::BadChecksum);
  Bytes wrongSession = valid; wrongSession[8] ^= 1; seal(wrongSession);
  assert(send(stream, wrongSession) == Event::Rejected && stream.error() == Error::WrongSession);
  Bytes wrongSequence = valid; put32(wrongSequence, 16, 2); seal(wrongSequence);
  assert(send(stream, wrongSequence) == Event::Rejected && stream.error() == Error::BadSequence);
  assert(send(stream, valid) == Event::Rectangle);
  assert(send(stream, valid) == Event::Duplicate);
  assert(send(stream, raw(Type::Ping, kNonce, 2)) == Event::Pong);
}

static void croppedAllFormats() {
  const Type types[] = {Type::Rectangle, Type::RleRectangle, Type::Lz4Rectangle,
                       Type::ScaledRectangle, Type::ScaledRleRectangle, Type::ScaledLz4Rectangle};
  for (Type type : types) {
    const bool scaled = type == Type::ScaledRectangle || type == Type::ScaledRleRectangle || type == Type::ScaledLz4Rectangle;
    const bool rle = type == Type::RleRectangle || type == Type::ScaledRleRectangle;
    const bool lz4 = type == Type::Lz4Rectangle || type == Type::ScaledLz4Rectangle;
    const uint16_t size = scaled ? 1 : 2, limit = scaled ? 240 : 480;
    Bytes pixels(size * size * 2, 0x7f);
    const Bytes data = rle ? run(size * size, 0x7f7f) : lz4 ? compressBlock(pixels) : pixels;
    TestStream stream;
    connect(stream);
    const Bytes valid = cropped(type, 1, data, limit - size, limit - size, size, size);
    const uint16_t bad[][2] = {{20,limit},{22,limit},{24,0},{24,limit},{26,0},{26,static_cast<uint16_t>(scaled ? 9 : 18)},
                              {20,UINT16_MAX},{22,UINT16_MAX},{24,UINT16_MAX},{26,UINT16_MAX}};
    for (const auto& mutation : bad) {
      Bytes packet = valid; put16(packet, mutation[0], mutation[1]); seal(packet);
      assert(send(stream, packet) == Event::Rejected && stream.error() == Error::BadCoordinates);
      assert(!stream.rectangle().pixels && stream.sequence() == 0);
    }
    if (!scaled) {
      for (unsigned field : {20u, 22u, 24u, 26u}) {
        Bytes packet = valid; put16(packet, field, 1); seal(packet);
        assert(send(stream, packet) == Event::Rejected && stream.error() == Error::BadCoordinates);
      }
    }
    assert(send(stream, valid) == Event::Rectangle);
    assert(stream.rectangle().x == limit - size && stream.rectangle().width == size);
    assert(stream.rectangle().bytes == data.size() || lz4);
    if (!rle) assert(memcmp(stream.rectangle().pixels, pixels.data(), pixels.size()) == 0);
  }
}

static void lz4BoundedMalformedFuzz() {
  struct Guarded { uint64_t before; TestStream stream; uint64_t after; } guarded;
  guarded.before = UINT64_C(0x1abcdefa0abcdef1); guarded.after = ~guarded.before;
  uint32_t random = UINT32_C(0x6784ca90);
  for (unsigned trial = 0; trial < 5000; ++trial) {
    connect(guarded.stream);
    const uint16_t width = static_cast<uint16_t>(1 + randomWord(random) % 240);
    const uint16_t height = static_cast<uint16_t>(1 + randomWord(random) % 8);
    Bytes block(1 + randomWord(random) % 512);
    for (uint8_t& byte : block) byte = static_cast<uint8_t>(randomWord(random));
    if ((trial & 3) == 0) {
      block = compressBlock(Bytes(width * height * 2, static_cast<uint8_t>(trial)));
      block[randomWord(random) % block.size()] ^= static_cast<uint8_t>(1u << (randomWord(random) % 8));
    }
    const Event event = send(guarded.stream, cropped(Type::ScaledLz4Rectangle, 1, block, 0, 0, width, height));
    assert(event == Event::Rejected || event == Event::Rectangle);
    if (event == Event::Rejected) {
      assert(guarded.stream.error() == Error::BadPayload && guarded.stream.sequence() == 0);
      assert(!guarded.stream.rectangle().pixels);
    } else {
      assert(guarded.stream.rectangle().bytes == width * height * 2 && !guarded.stream.rectangle().rle);
    }
    assert(guarded.before == UINT64_C(0x1abcdefa0abcdef1) && guarded.after == ~guarded.before);
  }
}

static void jpegFrameProtocol() {
  // Framing validates magic/size; the presentation layer separately decodes
  // and validates the baseline image before drawing or acknowledging it.
  Bytes jpeg(256, 0x22); jpeg[0]=0xff; jpeg[1]=0xd8; jpeg[254]=0xff; jpeg[255]=0xd9;
  const Bytes valid=cropped(Type::ScaledJpegFrame,1,jpeg,0,0,240,240);
  TestStream stream; connect(stream);
  const uint16_t geometry[][2]={{20,1},{22,1},{24,239},{24,480},{26,8},{26,241}};
  for (const auto& change:geometry) {
    Bytes bad=valid; put16(bad,change[0],change[1]); seal(bad);
    assert(send(stream,bad,2999)==Event::Rejected && stream.error()==Error::BadCoordinates);
    assert(stream.sequence()==0 && !stream.rectangle().pixels);
  }
  for (size_t at:{size_t(0),size_t(1),size_t(254),size_t(255)}) {
    Bytes bad=jpeg; bad[at]^=1;
    assert(send(stream,cropped(Type::ScaledJpegFrame,1,bad,0,0,240,240),2999)==Event::Rejected);
    assert(stream.error()==Error::BadPayload && stream.sequence()==0);
  }
  assert(stream.poll(3000)==Event::Timeout);
  connect(stream);
  assert(send(stream,cropped(Type::ScaledJpegFrame,1,Bytes(255),0,0,240,240))==Event::Rejected);
  assert(stream.error()==Error::BadPayload);
  assert(send(stream,valid)==Event::JpegFrame);
  const auto& rect=stream.rectangle();
  assert(rect.x==0 && rect.y==0 && rect.width==240 && rect.height==240 && rect.bytes==256 && rect.scale==2 && !rect.rle);
  assert(memcmp(rect.pixels,jpeg.data(),jpeg.size())==0);
  assert(send(stream,valid)==Event::Duplicate && !stream.rectangle().pixels);
  assert(send(stream,raw(Type::Ping,kNonce,2))==Event::Pong);
  jpeg.resize(kDisplayMaxPayload,0x22);jpeg[jpeg.size()-2]=0xff;jpeg.back()=0xd9;
  assert(send(stream,cropped(Type::ScaledJpegFrame,3,jpeg,0,0,240,240))==Event::JpegFrame);
  assert(stream.rectangle().bytes==kDisplayMaxPayload);
  stream.cancel();
  assert(send(stream,valid)==Event::Rejected && stream.error()==Error::WrongState);
}

static void audioAuxiliaryPackets() {
  const Bytes configuration{1, 1, 35, 0};
  const Bytes samples{0, 0, 0xff, 0x7f, 0, 0x80, 0xff, 0xff};
  for (Type type : {Type::AudioConfig, Type::AudioPCM}) {
    const Bytes payload = type == Type::AudioConfig ? configuration : samples;
    TestStream stream;
    stream.begin(kNonce, 0);
    assert(send(stream, raw(type, kNonce, 0, payload)) == Event::Rejected);
    assert(stream.error() == Error::WrongState);
    connect(stream);
    assert(send(stream, raw(type, kNonce + 1, 0, payload)) == Event::Rejected);
    assert(stream.error() == Error::WrongSession);
    for (uint32_t sequence : {1u, UINT32_MAX}) {
      assert(send(stream, raw(type, kNonce, sequence, payload)) == Event::Rejected);
      assert(stream.error() == Error::BadSequence);
    }
    for (size_t offset : {size_t(20), size_t(22), size_t(24), size_t(26)}) {
      Bytes bad = raw(type, kNonce, 0, payload);
      put16(bad, offset, 1); seal(bad);
      assert(send(stream, bad) == Event::Rejected && stream.error() == Error::BadCoordinates);
    }
    assert(stream.sequence() == 0 && stream.lastType() == Type::Hello && stream.connected());
    stream.cancel();
    assert(send(stream, raw(type, kNonce, 0, payload)) == Event::Rejected);
    assert(stream.error() == Error::WrongState);
  }

  TestStream stream;
  connect(stream);
  const Bytes invalidConfigs[] = {
    {}, {1}, {1, 1}, {1, 1, 35}, {1, 1, 35, 0, 0},
    {0, 1, 35, 0}, {2, 1, 35, 0}, {1, 2, 35, 0},
    {1, 1, 61, 0}, {1, 1, 255, 0}, {1, 1, 35, 1}
  };
  for (const Bytes& payload : invalidConfigs) {
    assert(send(stream, raw(Type::AudioConfig, kNonce, 0, payload)) == Event::Rejected);
    assert(stream.error() == Error::BadPayload && stream.sequence() == 0);
  }
  for (size_t length : {size_t(0), size_t(1), size_t(3), size_t(3199), size_t(3201), size_t(3202)}) {
    assert(send(stream, raw(Type::AudioPCM, kNonce, 0, Bytes(length))) == Event::Rejected);
    assert(stream.error() == Error::BadPayload && stream.lastType() == Type::Hello);
  }
  const Bytes image = raw(Type::Rectangle, kNonce, 1, Bytes(1920, 0x5a), 0, 2);
  assert(send(stream, image, 100) == Event::Rectangle);
  for (uint8_t enabled : {uint8_t(0), uint8_t(1)}) {
    for (uint8_t volume : {uint8_t(0), uint8_t(60)}) {
      const Bytes payload{1, enabled, volume, 0};
      assert(send(stream, raw(Type::AudioConfig, kNonce, 0, payload), 200) == Event::AudioConfigured);
      assert(stream.audioBytes() == payload.size());
      assert(!memcmp(stream.audioPayload(), payload.data(), payload.size()));
      assert(!stream.rectangle().pixels);
    }
  }
  for (size_t length : {size_t(2), size_t(8), size_t(3200)}) {
    Bytes payload(length);
    for (size_t i = 0; i < length; ++i) payload[i] = samples[i % samples.size()];
    assert(send(stream, raw(Type::AudioPCM, kNonce, 0, payload), 250) == Event::AudioSamples);
    assert(stream.audioBytes() == payload.size());
    assert(!memcmp(stream.audioPayload(), payload.data(), payload.size()));
    assert(stream.sequence() == 1 && stream.lastType() == Type::Rectangle);
  }
  assert(send(stream, image, 300) == Event::Duplicate && !stream.rectangle().pixels);
  assert(send(stream, raw(Type::Ping, kNonce, 2), 301) == Event::Pong);

  // Auxiliary traffic cannot extend the three-second visual-session lease.
  connect(stream, 100);
  assert(send(stream, raw(Type::AudioConfig, kNonce, 0, configuration), 2000) == Event::AudioConfigured);
  assert(send(stream, raw(Type::AudioPCM, kNonce, 0, samples), 3099) == Event::AudioSamples);
  assert(stream.sequence() == 0 && stream.lastType() == Type::Hello);
  assert(stream.poll(3100) == Event::Timeout && !stream.connected());
}

int main() {
  {DisplayStream idle;assert(!idle.receiveBytes());
   assert(idle.begin(kNonce,0));assert(idle.receiveBytes()==kDisplayReceiveBytes);
   idle.cancel();assert(!idle.receiveBytes());
   assert(send(idle,raw(Type::Query,0,0))==Event::Query);
   for(unsigned i=0;i<25;++i){assert(idle.begin(kNonce,i));idle.cancel();assert(!idle.receiveBytes());}
  }
  decodeWorkspaceOwnership();
  audioAuxiliaryPackets();
  jpegFrameProtocol();
  lz4GoldenAndBoundaries();
  lz4RejectionAndLease();
  croppedAllFormats();
  lz4BoundedMalformedFuzz();
  randomizedChecksumCompatibility();
  wifiProvisioning();
  discoveryAndGoldenHandshake();
  rectanglesAndSequences();
  malformedFramesAndResync();
  rectangleConstraints();
  handshakeStatusAndCleanup();
  timeoutBoundariesAndRollover();
  cancelDuringFrameAndBinaryNoise();
  extendedRectanglesAndRetries();
  extendedRectangleValidation();
  printf("display stream tests passed (%zu-byte receiver)\n", sizeof(DisplayStream));
}
