#import <Foundation/Foundation.h>
#import "USBTransport.h"
#import "WiFiChannel.h"
#import "DevicePairingStore.h"
#import "DeviceDiscovery.h"
#include "WireProtocol.hpp"
#include "../../firmware/sloth_pet/display_stream.h"
#include "../../firmware/sloth_pet/jpeg_display.h"
#include "../../firmware/sloth_pet/volume_overlay.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <vector>
#include <cstring>
#include <util.h>
#include <unistd.h>

// Exercise the real host state machine against the real firmware parser using
// only a pseudo-terminal. No USB inventory, virtual display or capture access.
@interface MossUSBTransport (TestAccess)
- (void)openPort:(NSString *)path;
- (void)readAvailable;
- (void)tick;
- (void)invalidateFrameBaseline;
- (void)line:(NSString *)line;
- (void)logPerformance:(NSArray<NSNumber *> *)values;
- (void)logAudioPerformance:(NSArray<NSNumber *> *)values;
- (void)networkLine:(NSString *)line;
- (void)serialDisconnected:(NSString *)reason;
- (void)discovered:(MossDiscoveredDevice *)device;
- (void)failWiFi:(NSString *)reason;
- (MossWiFiChannel *)newWiFiChannel;
@end

@interface TestPairingStore : MossDevicePairingStore
@property(nonatomic, strong) NSMutableDictionary<NSString *, MossDevicePairing *> *records;
@property(nonatomic) unsigned reads, saves;
@property(nonatomic) BOOL denyReads;
@end
@implementation TestPairingStore
- (instancetype)init { if ((self = [super init])) _records = [NSMutableDictionary dictionary]; return self; }
- (NSArray<NSString *> *)deviceIDs:(NSError **)error { (void)error; return _records.allKeys; }
- (MossDevicePairing *)pairingForDevice:(NSString *)deviceID error:(NSError **)error {
  ++_reads;
  if (_denyReads) {
    if (error) *error = [NSError errorWithDomain:@"MossTestKeychain" code:1 userInfo:nil];
    return nil;
  }
  return _records[deviceID];
}
- (BOOL)saveDevice:(NSString *)deviceID fingerprint:(NSData *)fingerprint token:(NSString *)token error:(NSError **)error {
  (void)error; ++_saves;
  MossDevicePairing *pairing = [[MossDevicePairing alloc] init];
  [pairing setValue:[deviceID copy] forKey:@"deviceID"];
  [pairing setValue:[fingerprint copy] forKey:@"fingerprint"];
  [pairing setValue:[token copy] forKey:@"token"];
  _records[deviceID] = pairing; return YES;
}
- (BOOL)forgetDevice:(NSString *)deviceID error:(NSError **)error {
  (void)error; [_records removeObjectForKey:deviceID]; return YES;
}
@end
@class TestWiFiChannel;

@interface TestUSBTransport : MossUSBTransport
@property(nonatomic) unsigned performanceLogs, audioPerformanceLogs;
@property(nonatomic, copy) NSArray<NSNumber *> *lastPerformance;
@property(nonatomic, strong) TestWiFiChannel *nextChannel;
@end
@implementation TestUSBTransport
- (MossWiFiChannel *)newWiFiChannel { assert(_nextChannel); return (MossWiFiChannel *)_nextChannel; }
- (void)logAudioPerformance:(NSArray<NSNumber *> *)values {
  ++_audioPerformanceLogs; [super logAudioPerformance:values];
}
- (void)logPerformance:(NSArray<NSNumber *> *)values {
  ++_performanceLogs;
  _lastPerformance = [values copy];
  [super logPerformance:values];
}
@end

// Substitute only the TLS socket; packets still traverse the real firmware parser.
@interface TestWiFiChannel : MossWiFiChannel
@property(nonatomic) BOOL online;
@property(nonatomic) unsigned connects;
@property(nonatomic) BOOL failNextSend;
@property(nonatomic) BOOL holdCompletion;
@property(nonatomic) unsigned sends;
@property(nonatomic) NSUInteger maximumBytes;
@property(nonatomic, copy) void (^pendingCompletion)(NSError *);
@property(nonatomic, strong) NSMutableArray<NSData *> *packets;
@end
@implementation TestWiFiChannel
- (instancetype)init { if ((self = [super init])) { _online = YES; _packets = [NSMutableArray array]; } return self; }
- (BOOL)ready { return _online; }
- (void)connectHost:(NSString *)host port:(NSString *)port fingerprint:(NSData *)fingerprint token:(NSString *)token completion:(void (^)(NSError *))completion {
  assert([host isEqualToString:@"127.0.0.1"] && [port isEqualToString:@"8443"] && fingerprint.length == 32 && token.length == 64);
  ++_connects; _online = YES; completion(nil);
}
- (void)stop { _online = NO; _pendingCompletion = nil; [_packets removeAllObjects]; }
- (void)sendPacket:(NSData *)packet completion:(void (^)(NSError *))completion {
  if (_failNextSend) {
    _failNextSend = NO;
    completion([NSError errorWithDomain:@"MossTest" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Synthetic TLS send failure"}]);
    return;
  }
  assert(_online && !_pendingCompletion);
  ++_sends; _maximumBytes = MAX(_maximumBytes, packet.length);
  assert(packet.length <= 65536);
  [_packets addObject:[packet copy]];
  if (_holdCompletion) _pendingCompletion = [completion copy];
  else completion(nil);
}
@end

struct Fixture {
  MossUSBTransport *host;
  TestWiFiChannel *wifi;
  TestPairingStore *pairings;
  bool wirelessControl = false, dropAudioAck = false, dropAudioConfigAck = false;
  unsigned audioConfigurations = 0, audioPackets = 0;
  std::vector<uint8_t> audioConfiguredVolumes;
  std::vector<uint8_t> audioBytes;
  unsigned wifiConfigurations = 0, usbRectangles = 0, wifiRectangles = 0, pings = 0, rejectedReplays = 0;
  bool truncateFirstBatch = false, truncated = false;
  sloth::DisplayStream receiver;
  alignas(uint16_t) uint8_t decodeWorkspace[sloth::kDisplayMaxPayload];
  int master = -1;
  uint64_t nonce = 0x1020304050607080ULL;
  uint32_t now = 100;
  unsigned requests = 0, ready = 0, ended = 0, rectangles = 0, duplicates = 0, releases = 0;
  size_t wireBytes = 0;
  unsigned rawRects = 0, rleRects = 0, lz4Rects = 0, croppedRects = 0, scaledRects = 0, jpegFrames = 0;
  std::vector<uint16_t> screen = std::vector<uint16_t>(480 * 480);
  std::vector<moss_wire::Band> bands;
  bool dropFirstAck = false, dropped = false, accept = true;
  explicit Fixture(bool autoAccept = true) : accept(autoAccept) {
    int slave;
    char path[256];
    assert(openpty(&master, &slave, path, nullptr, nullptr) == 0);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    close(slave);
    host = [[TestUSBTransport alloc] init];
    pairings = [[TestPairingStore alloc] init];
    [host setValue:pairings forKey:@"pairings"];
    host.requestHandler = ^(uint64_t session) { ++requests; if (accept) [host acceptRequest:session]; };
    host.readyHandler = ^(uint64_t session) { assert(session == nonce); ++ready; };
    host.endedHandler = ^(NSString *reason) { assert(reason.length); ++ended; };
    [host openPort:[NSString stringWithUTF8String:path]];
    assert([[host valueForKey:@"fd"] intValue] >= 0);
    [host setValue:@YES forKey:@"running"];
    assert(receiver.setDecodeWorkspace(decodeWorkspace, sizeof(decodeWorkspace)));
    receiver.begin(nonce, now);
  }
  ~Fixture() {
    host.requestHandler = nil; host.readyHandler = nil; host.endedHandler = nil; host.capabilitiesHandler = nil; host.wifiSetupHandler = nil; host.audioVolumeHandler = nil;
    [host stop]; close(master);
  }
  void line(const char *kind, uint32_t sequence) {
    char text[128];
    const int size = snprintf(text, sizeof(text), "DISPLAY %s %016llx %u\n", kind,
                             static_cast<unsigned long long>(nonce), sequence);
    if (wirelessControl) {
      NSString *line = [[NSString alloc] initWithBytes:text length:size - 1 encoding:NSASCIIStringEncoding];
      [host networkLine:line];
    } else assert(write(master, text, size) == size);
  }
  void request(bool wireless = false) {
    char text[128];
    const int size = snprintf(text, sizeof(text), "DISPLAY %s %016llx 480 480 15360\n", wireless ? "WIFI_REQUEST" : "REQUEST",
                             static_cast<unsigned long long>(nonce));
    assert(write(master, text, size) == size);
    [host readAvailable];
  }
  void capabilities(uint32_t flags) {
    line("CAPS", flags);
    [host readAvailable];
  }
  unsigned frames() { return [[host valueForKey:@"framesSent"] unsignedIntValue]; }
  void apply(const sloth::DisplayRectangle& rect) {
    const unsigned scale = rect.scale;
    auto putPixel = [&](unsigned pixel, uint16_t color) {
      const unsigned x = (rect.x + pixel % rect.width) * scale;
      const unsigned y = (rect.y + pixel / rect.width) * scale;
      for (unsigned yy = 0; yy < scale; ++yy)
        for (unsigned xx = 0; xx < scale; ++xx) screen[(y + yy) * 480 + x + xx] = color;
    };
    unsigned pixel = 0;
    for (unsigned i = 0; i < rect.bytes;) {
      unsigned count = 1;
      if (rect.rle) { count = rect.pixels[i] | (rect.pixels[i + 1] << 8); i += 2; }
      const uint16_t color = rect.pixels[i] | (rect.pixels[i + 1] << 8); i += 2;
      while (count--) putPixel(pixel++, color);
    }
    assert(pixel == static_cast<unsigned>(rect.width) * rect.height);
    moss_wire::Band band; band.y = rect.y; band.height = rect.height; band.x = rect.x; band.width = rect.width; bands.push_back(band);
    if (receiver.lastType() == sloth::DisplayPacketType::Lz4Rectangle || receiver.lastType() == sloth::DisplayPacketType::ScaledLz4Rectangle) {
      assert(rect.pixels == decodeWorkspace);
      ++lz4Rects;
    }
    else if (rect.rle) ++rleRects; else ++rawRects;
    if (rect.width * rect.scale < 480) ++croppedRects;
    if (scale == 2) ++scaledRects;
  }
  void matches(NSData *data, unsigned size) {
    assert(data.length == size * size * 2);
    const auto* pixels = static_cast<const uint8_t*>(data.bytes);
    for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 480; ++x) {
      const unsigned index = ((y * size / 480) * size + x * size / 480) * 2;
      assert(screen[y * 480 + x] == (pixels[index] | (pixels[index + 1] << 8)));
    }
  }
  void feed(const uint8_t *bytes, size_t size, bool wireless) {
    for (size_t i = 0; i < size; ++i) {
      const auto event = receiver.feed(bytes[i], now);
      using E = sloth::DisplayStreamEvent;
      switch (event) {
        case E::Ready: assert(wireless == wirelessControl); line("READY", receiver.sequence()); break;
        case E::Rectangle:
          ++rectangles;
          if (wireless) ++wifiRectangles; else ++usbRectangles;
          apply(receiver.rectangle());
          if (dropFirstAck && !dropped &&
              (!wireless || receiver.sequence() == [[host valueForKey:@"waitingSequence"] unsignedIntValue])) {
            dropped = true; break;
          }
          line("ACK", receiver.sequence()); break;
        case E::JpegFrame: {
          assert(wireless);
          ++rectangles; ++wifiRectangles; ++jpegFrames;
          const auto &jpeg = receiver.rectangle();
          std::vector<uint16_t> decoded(240 * 240);
          const size_t scratchBytes = sloth::jpeg_display::workspaceBytes();
          std::vector<std::max_align_t> scratch((scratchBytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
          assert(reinterpret_cast<uintptr_t>(scratch.data()) % sloth::jpeg_display::workspaceAlignment() == 0);
          assert(sloth::jpeg_display::decode(jpeg.pixels, jpeg.bytes, decoded.data(), decoded.size(),
                                            scratch.data(), scratch.size() * sizeof(std::max_align_t)));
          for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 480; ++x)
            screen[y * 480 + x] = decoded[(y / 2) * 240 + x / 2];
          if (dropFirstAck && !dropped) { dropped = true; break; }
          line("ACK", receiver.sequence()); break;
        }
        case E::Duplicate:
          ++duplicates;
          line(receiver.lastType() == sloth::DisplayPacketType::Hello ? "READY" : "ACK", receiver.sequence()); break;
        case E::Pong: assert(wireless == wirelessControl); ++pings; line("ACK", receiver.sequence()); break;
        case E::PermissionNeeded: case E::HostError: line("ACK", receiver.sequence()); break;
        case E::Stopped: line("STOP", receiver.sequence()); break;
        case E::Released: assert(wireless == wirelessControl); ++releases; line("RELEASED", receiver.sequence()); break;
        case E::AudioConfigured: {
          assert(wireless && wirelessControl); ++audioConfigurations;
          const uint8_t *config = receiver.audioPayload();
          assert(receiver.audioBytes() == 4 && config[0] == 1 && config[1] <= 1 && config[2] <= 60 && config[3] == 0);
          audioConfiguredVolumes.push_back(config[2]);
          if (!dropAudioConfigAck) [host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO %016llx %u %u 0", (unsigned long long)nonce, config[1], config[2]]];
          break;
        }
        case E::AudioSamples: {
          assert(wireless && wirelessControl); ++audioPackets;
          const uint8_t *samples = receiver.audioPayload();
          audioBytes.insert(audioBytes.end(), samples, samples + receiver.audioBytes());
          if (!dropAudioAck) [host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO_ACK %016llx", (unsigned long long)nonce]];
          break;
        }
        case E::WifiConfigure: {
          assert(!wireless);
          ++wifiConfigurations;
          const uint8_t *credentials = receiver.wifiCredentials();
          assert(credentials[0] == 11 && credentials[1] == 14);
          assert(!memcmp(credentials + 2, "TestNetwork", 11));
          assert(!memcmp(credentials + 13, "dummy-password", 14));
          receiver.clearCredentials();
          for (unsigned j = 0; j < 27; ++j) assert(credentials[j] == 0);
          break;
        }
        case E::Rejected:
          if (wireless && receiver.error() == sloth::DisplayStreamError::BadSequence) {
            ++rejectedReplays; line("NACK", receiver.sequence()); break;
          }
          fprintf(stderr, "Unexpected rejected packet: error=%u\n", static_cast<unsigned>(receiver.error()));
          assert(false); break;
        case E::None: case E::Query: break;
        default: fprintf(stderr, "Firmware rejected transport packet: event=%u error=%u\n",
                         static_cast<unsigned>(event), static_cast<unsigned>(receiver.error())); assert(false);
      }
    }
  }
  void attachWiFi() {
    wifi = [[TestWiFiChannel alloc] init];
    [host setValue:wifi forKey:@"wifi"];
    [host setValue:@0 forKey:@"wifiDeadline"];
    [host acceptRequest:nonce];
  }
  void pump() {
    [host setValue:@([NSProcessInfo processInfo].systemUptime) forKey:@"lastDiscovery"];
    [host tick];
    uint8_t bytes[32768];
    for (;;) {
      const ssize_t size = read(master, bytes, sizeof(bytes));
      if (size <= 0) break;
      wireBytes += static_cast<size_t>(size);
      feed(bytes, static_cast<size_t>(size), false);
    }
    while (wifi.packets.count) {
      NSData *packet = wifi.packets.firstObject;
      [wifi.packets removeObjectAtIndex:0];
      wireBytes += packet.length;
      const auto *bytes = static_cast<const uint8_t *>(packet.bytes);
      size_t length = packet.length;
      if (truncateFirstBatch && !truncated) {
        // Deliver exactly two complete COBS packets of a four-strip batch.
        unsigned complete = 0; bool inside = false;
        for (size_t i = 0; i < length; ++i) {
          if (bytes[i]) inside = true;
          else if (inside) {
            inside = false;
            if (++complete == 2) { length = i + 1; break; }
          }
        }
        assert(complete == 2 && length < packet.length); truncated = true;
      }
      feed(bytes, length, true);
    }
    [host readAvailable];
    ++now;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.001, false);
  }
  void until(const std::function<bool()>& condition, unsigned maximum = 3000) {
    while (!condition() && maximum--) pump();
    assert(condition());
  }
};

static void finishFrame(Fixture& test, NSData* frame) {
  const unsigned next = test.frames() + 1;
  [test.host submitFrame:frame];
  test.until([&] { return test.frames() == next; });
}

static void paint(NSMutableData* data, unsigned size, unsigned x, unsigned y,
                  unsigned width, unsigned height, uint16_t color) {
  auto* pixels = static_cast<uint8_t*>(data.mutableBytes);
  for (unsigned yy = y; yy < y + height; ++yy) for (unsigned xx = x; xx < x + width; ++xx) {
    assert(xx < size && yy < size);
    const unsigned p = (yy * size + xx) * 2;
    pixels[p] = static_cast<uint8_t>(color); pixels[p + 1] = static_cast<uint8_t>(color >> 8);
  }
}

static void changedBandsAndAcknowledgedBaseline() {
  Fixture test;
  test.request();
  test.until([&] { return test.ready == 1; });
  assert(test.host.capabilities == 0 && test.host.transferSize == 480);
  test.host.transferSize = 240;
  assert(test.host.transferSize == 480); // Old firmware never receives scaled packets.
  NSMutableData* image = [NSMutableData dataWithLength:moss_wire::frameBytes];
  finishFrame(test, image);
  const unsigned initial = test.rectangles;
  assert(initial == 30 && test.rawRects == 30 && !test.rleRects);
  finishFrame(test, image);
  assert(test.rectangles == initial); // Unchanged frames cause no rectangle traffic.
  paint(image, 480, 20, 101, 4, 1, 0x1234);
  paint(image, 480, 30, 106, 4, 2, 0x2345);
  finishFrame(test, image);
  assert(test.rectangles == initial + 2);
  assert(test.bands[initial].y == 100 && test.bands[initial].height == 2);
  assert(test.bands[initial + 1].y == 106 && test.bands[initial + 1].height == 2);
  test.matches(image, 480);

  // Lose one ACK, then queue newer captures. Only the retransmission may
  // acknowledge this band; baseline and capture snapshots must remain distinct.
  test.dropFirstAck = true; test.dropped = false;
  paint(image, 480, 10, 201, 8, 1, 0x3456);
  NSData* first = [image copy];
  [test.host submitFrame:image];
  test.until([&] { return test.dropped; });
  paint(image, 480, 10, 201, 8, 1, 0x4567);
  [test.host submitFrame:image];
  paint(image, 480, 10, 201, 8, 1, 0x5678);
  NSData* newest = [image copy];
  [test.host submitFrame:image];
  paint(image, 480, 10, 201, 8, 1, 0x6789); // Caller mutation must not change submitted bytes.
  const unsigned complete = test.frames();
  test.line("ACK", test.receiver.sequence() + 100); [test.host readAvailable];
  assert(test.frames() == complete && [[test.host valueForKey:@"awaiting"] boolValue]);
  [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
  test.until([&] { return test.duplicates == 1; });
  test.matches(first, 480);
  test.until([&] { return test.frames() == complete + 2; });
  test.matches(newest, 480);
  finishFrame(test, newest);
  assert(test.rectangles == initial + 4); // No queued middle frame or duplicate redraw.
}

static void negotiatedCompressionAndResolutionChanges() {
  Fixture test;
  __block unsigned capabilitiesChanged = 0;
  __block uint32_t lastCapabilities = 0;
  test.host.capabilitiesHandler = ^(uint32_t flags) {
    assert(flags == 3 || flags == 0); lastCapabilities = flags; ++capabilitiesChanged;
  };
  test.request();
  const uint64_t realNonce = test.nonce;
  ++test.nonce; test.capabilities(3); test.nonce = realNonce;
  assert(test.host.capabilities == 0); // Capabilities cannot cross sessions.
  test.capabilities(3); test.capabilities(3);
  test.until([&] { return test.ready == 1; });
  assert(capabilitiesChanged == 1 && test.host.capabilities == 3);
  NSMutableData* native = [NSMutableData dataWithLength:moss_wire::frameBytes];
  paint(native, 480, 0, 0, 480, 480, 0x1357);
  finishFrame(test, native);
  assert(test.rleRects == 30 && !test.rawRects && !test.scaledRects);
  test.matches(native, 480);
  test.host.transferSize = 240;
  assert(test.host.transferSize == 240);
  [test.host submitFrame:native];
  assert([test.host valueForKey:@"latestFrame"] == nil); // Old capture callback size is discarded.
  NSMutableData* fast = [NSMutableData dataWithLength:240 * 240 * 2];
  paint(fast, 240, 0, 0, 240, 240, 0x2468);
  finishFrame(test, fast);
  assert(test.scaledRects == 30);
  test.matches(fast, 240);
  const unsigned before = test.rectangles;
  paint(fast, 240, 3, 37, 1, 1, 0xFFFF);
  finishFrame(test, fast);
  assert(test.rectangles == before + 1 && test.bands.back().y == 37 && test.bands.back().height == 1);
  test.matches(fast, 240);

  // Change resolution while an old band awaits ACK. Finish that exact band,
  // then redraw the entire newly selected resolution with a fresh baseline.
  test.dropFirstAck = true; test.dropped = false;
  paint(fast, 240, 5, 91, 1, 1, 0xABCD);
  [test.host submitFrame:fast];
  test.until([&] { return test.dropped; });
  test.host.transferSize = 480;
  [test.host submitFrame:native];
  [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
  const unsigned complete = test.frames();
  test.until([&] { return test.frames() == complete + 1; });
  test.matches(native, 480);
  assert(test.bands.back().y == 464 && test.bands.back().height == 16);

  // A fresh session starts without inherited compression/scale capabilities.
  [test.host endSession:@"test reconnect"];
  test.until([&] { return !test.host.session; });
  assert(capabilitiesChanged == 2 && lastCapabilities == 0);
  assert(test.host.capabilities == 0 && test.host.transferSize == 480);
  test.host.transferSize = 240;
  assert(test.host.transferSize == 480);
  test.host.capabilitiesHandler = nil;
  ++test.nonce; test.receiver.begin(test.nonce, test.now); test.request();
  assert(test.host.capabilities == 0 && test.host.transferSize == 480);
  test.until([&] { return test.ready == 2; });
  const unsigned raw = test.rawRects;
  finishFrame(test, native);
  assert(test.rawRects == raw + 30);
}

static NSMutableData* syntheticDesktop(unsigned size, unsigned scenario, unsigned frame) {
  NSMutableData* data = [NSMutableData dataWithLength:size * size * 2];
  auto* pixels = static_cast<uint8_t*>(data.mutableBytes);
  for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x) {
    const unsigned xx = x * 480 / size, yy = y * 480 / size;
    uint16_t color = 0x10E3;
    if (scenario == 3) { // Every pixel changes; compression must fall back to raw.
      uint32_t random = (xx + yy * 480 + 1) * 0x9E3779B9u ^ frame * 0x85EBCA6Bu;
      random ^= random >> 16; random *= 0x7FEB352Du; random ^= random >> 15;
      color = static_cast<uint16_t>(random);
    } else {
      if (yy < 36) color = 0x2104;
      else if (xx < 80) color = 0x3186;
      else if (xx > 94 && xx < 458 && yy > 56 && yy < 455) {
        const unsigned row = yy + (scenario == 2 ? frame * 7 : 0);
        color = 0xEF7D;
        if (row % 18 < 3 && xx < 160 + ((row / 18) % 5) * 55) color = 0x7BEF;
      }
      if (yy >= 12 && yy < 24 && xx >= 392 && xx < 392 + 8 + (scenario == 0 ? frame * 8 : 0)) color = 0xFFFF;
      if (scenario == 1) {
        const unsigned cx = 200 + frame * 28, cy = 160 + frame * 24;
        if (yy >= cy && yy < cy + 14 && xx >= cx && xx < cx + 1 + (yy - cy) / 2) color = 0xFFFF;
      }
    }
    const unsigned i = (y * size + x) * 2;
    pixels[i] = static_cast<uint8_t>(color); pixels[i + 1] = static_cast<uint8_t>(color >> 8);
  }
  return data;
}

static void benchmarkDesktopChanges() {
  const char* scenarios[] = {"clock", "cursor", "scroll", "full-motion"};
  const char* modes[] = {"native-raw-full", "native-delta-rle", "fast240-delta-rle"};
  // Three warm updates follow an initial acknowledged frame. These timings
  // measure parser/state-machine/PTY overhead, never real USB frame rates.
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    size_t reference = 0;
    for (unsigned mode = 0; mode < 3; ++mode) {
      Fixture test;
      test.request();
      if (mode) test.capabilities(3);
      test.until([&] { return test.ready == 1; });
      const unsigned size = mode == 2 ? 240 : 480;
      test.host.transferSize = size;
      finishFrame(test, syntheticDesktop(size, scenario, 0));
      const size_t initialBytes = test.wireBytes;
      const unsigned initialRects = test.rectangles;
      const NSTimeInterval started = [NSProcessInfo processInfo].systemUptime;
      for (unsigned frame = 1; frame <= 3; ++frame) {
        if (!mode) [test.host invalidateFrameBaseline]; // Reference: always transmit the whole raw frame.
        NSData* data = syntheticDesktop(size, scenario, frame);
        finishFrame(test, data);
        test.matches(data, size); // The actual firmware parser drives the reconstructed panel.
      }
      const size_t bytes = test.wireBytes - initialBytes;
      const unsigned rects = test.rectangles - initialRects;
      const double milliseconds = ([NSProcessInfo processInfo].systemUptime - started) * 1000;
      if (!mode) reference = bytes;
      printf("BENCH PTY %-11s %-18s updates=3 bytes=%zu rects=%u wall_ms=%.1f raw_ratio=%.4f\n",
             scenarios[scenario], modes[mode], bytes, rects, milliseconds, double(bytes) / reference);
      if (mode && scenario != 3) assert(bytes < reference / 5);
      if (mode == 2 && scenario == 3) assert(bytes < reference / 3);
      if (mode == 1 && scenario == 3) assert(bytes <= reference + 100);
    }
  }
}

// Independent reference: move each source pixel clockwise into its destination.
static NSData *turnFrame(NSData *source, unsigned size) {
  NSMutableData *out = [NSMutableData dataWithLength:source.length];
  const auto *src = static_cast<const uint16_t *>(source.bytes);
  auto *dst = static_cast<uint16_t *>(out.mutableBytes);
  for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x)
    dst[x * size + size - 1 - y] = src[y * size + x];
  return out;
}

static void rotationWhileIdleAndAwaitingAck() {
  for (bool wireless : {false, true}) for (unsigned size : {480u, 240u}) for (uint32_t flags : {3u, 15u}) {
    Fixture test;
    test.request(wireless); test.capabilities(flags);
    if (wireless) {
      [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
      test.until([&] { return test.wifiConfigurations == 1; });
      assert(!test.receiver.connected() && test.ready == 0);
      test.attachWiFi();
    }
    test.until([&] { return test.ready == 1; });
    test.host.transferSize = size;
    NSMutableData *source = [NSMutableData dataWithLength:size * size * 2];
    paint(source, size, 7, 13, 29, 41, 0x1357);
    paint(source, size, size - 23, size - 37, 11, 19, 0x2468);
    finishFrame(test, source);
    NSData *expected = source;
    for (unsigned turn = 1; turn <= 4; ++turn) {
      const unsigned completed = test.frames();
      test.line("ROTATION", turn & 3); [test.host readAvailable];
      // No additional capture frame: retained source must redraw a static desktop.
      test.until([&] { return test.frames() == completed + 1; });
      expected = turnFrame(expected, size);
      test.matches(expected, size);
      const unsigned rectangles = test.rectangles;
      test.line("ROTATION", turn & 3); [test.host readAvailable];
      test.line("ROTATION", 4); [test.host readAvailable]; // Out of range.
      ++test.nonce; test.line("ROTATION", 1); --test.nonce; [test.host readAvailable];
      for (unsigned i = 0; i < 5; ++i) test.pump();
      assert(test.rectangles == rectangles && test.ready == 1);
    }
    test.dropFirstAck = true;
    paint(source, size, 0, 0, size, 1, 0xBEEF);
    [test.host submitFrame:source];
    test.until([&] { return test.dropped; });
    const unsigned completed = test.frames();
    // Rapid presses while an old strip awaits ACK must keep the newest angle.
    test.line("ROTATION", 1); test.line("ROTATION", 2); test.line("ROTATION", 3);
    [test.host readAvailable];
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
    test.until([&] { return test.frames() == completed + 1; });
    test.matches(turnFrame(turnFrame(turnFrame(source, size), size), size), size);
    assert(test.duplicates == 1 && test.ready == 1 && test.ended == 0);
    if (wireless) {
      assert(!test.usbRectangles && test.wifiRectangles == test.rectangles);
      [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"lastTransmit"];
      test.until([&] { return test.pings == 1; });
      [test.host endSession:@"Wi-Fi rotation test complete"];
      test.until([&] { return !test.host.session; });
      assert(test.releases == 1 && test.ended == 1 && !test.wifi.ready);
    }
  }
}

static void wifiProvisioningAndFailures() {
  {
    Fixture test;
    test.request(true);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; });
    const uint64_t oldNonce = test.nonce;
    // Firmware timed out waiting for this network/Mac and chose physical USB.
    // Fresh consent nonce replaces all pending setup, including stale callbacks.
    ++test.nonce; test.receiver.begin(test.nonce,test.now); test.request(false);
    assert(test.requests == 2 && test.ended == 1 && !test.host.wifiRequested);
    assert([[test.host valueForKey:@"wifiDeadline"] doubleValue] == 0);
    [test.host acceptRequest:oldNonce];
    [test.host line:[NSString stringWithFormat:@"DISPLAY WIFI_ERROR %016llx 1",(unsigned long long)oldNonce]];
    test.until([&] { return test.ready == 1; });
    assert(test.host.session == test.nonce && test.receiver.connected() && !test.releases);
    [test.host submitFrame:[NSMutableData dataWithLength:480*480*2]];
    test.until([&] { return test.usbRectangles > 0; });
    assert(!test.wifiRectangles);
  }
  {
    Fixture test;
    unsigned setups = 0; unsigned *setupCounter = &setups;
    test.host.wifiSetupHandler = ^{ ++*setupCounter; };
    test.request(true);
    test.until([&] { return setups == 1; });
    assert(test.host.wifiRequested && !test.host.wifiConnected);
    for (unsigned i = 0; i < 5; ++i) test.pump();
    assert(!test.receiver.connected() && test.ready == 0);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; });
    assert(![test.host valueForKey:@"tx"]); // Credential packet isn't retained for retries.
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    [test.host acceptRequest:test.nonce];
    for (unsigned i = 0; i < 5; ++i) test.pump();
    assert(test.wifiConfigurations == 1 && !test.receiver.connected() && !test.ready);
    // A stale nonce must not create a network connection or consume pairing data.
    NSString *hex = [@"a" stringByPaddingToLength:64 withString:@"a" startingAtIndex:0];
    [test.host line:[NSString stringWithFormat:@"DISPLAY WIFI_READY %016llx 127.0.0.1 8443 %@ %@",
                    (unsigned long long)test.nonce + 1, hex, hex]];
    assert(![test.host valueForKey:@"wifi"] && !test.ready && !test.ended);
    ++test.nonce; test.line("WIFI_ERROR", 1); --test.nonce; [test.host readAvailable];
    assert(!test.ended);
    test.line("WIFI_ERROR", 1); [test.host readAvailable];
    test.until([&] { return !test.host.session; });
    assert(test.ended == 1 && test.releases == 1 && !test.host.wifiRequested);
    test.request(true); assert(test.requests == 1); // Failed nonce cannot auto-resume.
  }
  {
    Fixture test;
    test.request(true);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; });
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 1) forKey:@"wifiDeadline"];
    test.until([&] { return !test.host.session; });
    assert(test.ended == 1 && test.releases == 1 && !test.ready);
  }
  {
    Fixture test;
    test.request(true);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; });
    test.attachWiFi();
    test.until([&] { return test.ready == 1; });
    test.wifi.failNextSend = YES;
    [test.host submitFrame:[NSMutableData dataWithLength:480 * 480 * 2]];
    test.until([&] { return !test.host.session; });
    assert(test.ended == 1 && test.releases == 1 && !test.rectangles && !test.wifi.ready);
  }
  {
    Fixture test;
    test.request(true); test.capabilities(3);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; });
    test.attachWiFi();
    test.until([&] { return test.ready == 1; });
    test.wifi.holdCompletion = YES;
    [test.host submitFrame:[NSMutableData dataWithLength:480 * 480 * 2]];
    test.until([&] { return test.rectangles == 30; });
    // The real USB ACK has arrived, but Network.framework's send completion has not.
    assert(![[test.host valueForKey:@"awaiting"] boolValue]);
    assert([[test.host valueForKey:@"networkWriting"] boolValue]);
    assert(test.wifi.pendingCompletion && !test.wifi.packets.count);
    for (unsigned i = 0; i < 10; ++i) test.pump();
    assert(test.rectangles == 30 && !test.frames() && !test.wifi.packets.count);
    void (^completion)(NSError *) = test.wifi.pendingCompletion;
    test.wifi.pendingCompletion = nil;
    test.wifi.holdCompletion = NO;
    completion(nil);
    test.until([&] { return test.frames() == 1; });
    assert(test.rectangles == 30 && !test.usbRectangles && test.wifiRectangles == 30 && test.wifi.sends == 1);
    test.matches([NSMutableData dataWithLength:480 * 480 * 2], 480);
  }
  for (NSString *badPassword in @[@"short", [@"x" stringByPaddingToLength:64 withString:@"x" startingAtIndex:0]]) {
    Fixture test;
    test.request(true);
    [test.host configureWiFiSSID:@"TestNetwork" password:badPassword];
    test.until([&] { return !test.host.session; });
    assert(test.ended == 1 && test.releases == 1 && !test.wifiConfigurations);
  }
}

static NSMutableData *repeatedPixelTexture(unsigned size) {
  NSMutableData *image = [NSMutableData dataWithLength:size * size * 2];
  auto *pixels = static_cast<uint16_t *>(image.mutableBytes);
  for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x)
    pixels[y * size + x] = static_cast<uint16_t>(x * 37 ^ ((y % 2) * 0x33));
  return image;
}
static void losslessLz4AndCroppedTransfers() {
  for (bool wireless : {false, true}) for (unsigned size : {480u, 240u}) {
    Fixture test;
    test.request(wireless); test.capabilities(15);
    if (wireless) {
      [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
      test.until([&] { return test.wifiConfigurations == 1; }); test.attachWiFi();
    }
    test.until([&] { return test.ready == 1; });
    test.host.transferSize = size;
    NSMutableData *image = repeatedPixelTexture(size);
    finishFrame(test, image);
    assert(test.lz4Rects == 30 && !test.croppedRects && test.host.capabilities == 15);
    if (wireless) assert(test.wifi.sends == 1 && test.wifi.maximumBytes <= 65536);
    test.matches(image, size);
    const unsigned initial = test.rectangles;
    finishFrame(test, image); assert(test.rectangles == initial);
    paint(image, size, 13, 101, 1, 1, 0xBEEF);
    const size_t before = test.wireBytes;
    finishFrame(test, image);
    const auto crop = test.bands.back();
    const unsigned step = size == 480 ? 2 : 1;
    assert(crop.x == (size == 480 ? 12u : 13u) && crop.width == step && crop.height == step);
    assert(crop.y == (size == 480 ? 100u : 101u));
    assert(test.wireBytes - before < 64 && test.croppedRects == 1);
    test.matches(image, size);
    paint(image, size, 7, 20, 1, 1, 0xCAFE);
    paint(image, size, size - 7, 70, 1, 1, 0xFACE);
    const unsigned sends = test.wifi.sends;
    finishFrame(test, image);
    assert(test.croppedRects == 3);
    if (wireless) assert(test.wifi.sends == sends + 1); // Disjoint cropped bands share a write.
    test.matches(image, size);
    // Newest-frame retention and retries must preserve pixels outside each tiny crop.
    test.dropFirstAck = true; test.dropped = false;
    paint(image, size, 9, 30, 1, 1, 0x1234);
    paint(image, size, size - 9, 90, 1, 1, 0x2345);
    [test.host submitFrame:image]; test.until([&] { return test.dropped; });
    const unsigned complete = test.frames();
    paint(image, size, 9, 30, 1, 1, 0x3456);
    paint(image, size, 15, 150, 1, 1, 0x4567);
    NSData *newest = [image copy]; [test.host submitFrame:image];
    paint(image, size, 9, 30, 1, 1, 0x5678); // Mutation after submit must not affect ACK state.
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
    test.until([&] { return test.frames() == complete + 2; });
    test.matches(newest, size);
    assert(test.duplicates == 1 && test.ready == 1 && !test.ended);
    if (wireless) assert(!test.usbRectangles);
  }
  {
    // Resolution changes must retire a whole compressed native batch before Fast redraw.
    Fixture test; test.request(true); test.capabilities(15);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; }); test.attachWiFi();
    test.until([&] { return test.ready == 1; }); test.dropFirstAck = true;
    [test.host submitFrame:repeatedPixelTexture(480)]; test.until([&] { return test.dropped; });
    test.host.transferSize = 240;
    NSData *fast = repeatedPixelTexture(240); [test.host submitFrame:fast];
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
    test.until([&] { return test.frames() == 1; }); test.matches(fast, 240);
    assert(test.duplicates == 1 && test.scaledRects == 30 && !test.usbRectangles);
  }
}

static NSMutableData *photographicTexture(unsigned phase) {
  NSMutableData *image = [NSMutableData dataWithLength:240 * 240 * 2];
  auto *pixels = static_cast<uint16_t *>(image.mutableBytes);
  uint32_t random = 0x9182B3C4u;
  for (unsigned y = 0; y < 240; ++y) for (unsigned x = 0; x < 240; ++x) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    const unsigned r = std::min(31u, x * 27 / 239 + (random & 3u));
    const unsigned g = std::min(63u, y * 55 / 239 + ((random >> 4) & 7u));
    const unsigned b = std::min(31u, (x + y + phase * 25) * 23 / 478 + ((random >> 8) & 3u));
    pixels[y * 240 + x] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
  }
  return image;
}
static void startFastWiFi(Fixture &test, uint32_t capabilities = 31) {
  test.request(true); test.capabilities(capabilities);
  [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
  test.until([&] { return test.wifiConfigurations == 1; }); test.attachWiFi();
  test.until([&] { return test.ready == 1; }); test.host.transferSize = 240;
}
static double jpegMeanAbsoluteError(Fixture &test, NSData *source) {
  const auto *pixels = static_cast<const uint16_t *>(source.bytes);
  double error = 0;
  for (unsigned y = 0; y < 240; ++y) for (unsigned x = 0; x < 240; ++x) {
    const uint16_t actual = test.screen[y * 2 * 480 + x * 2], expected = pixels[y * 240 + x];
    error += std::abs((int)(actual >> 11) - (int)(expected >> 11)) * 255.0 / 31;
    error += std::abs((int)((actual >> 5) & 63) - (int)((expected >> 5) & 63)) * 255.0 / 63;
    error += std::abs((int)(actual & 31) - (int)(expected & 31)) * 255.0 / 31;
  }
  return error / (240 * 240 * 3);
}
static void startWirelessOverUSB(Fixture&, uint32_t);
static void adaptiveJpegIsBoundedAndRefinesExactly() {
  NSData *photo = photographicTexture(0);
  {
    Fixture test; startWirelessOverUSB(test,127);test.host.transferSize=240;test.host.audioEnabled=YES;
    test.until([&]{return test.host.audioActive;});
    test.wifi.maximumBytes=0;finishFrame(test,photo);
    assert(test.jpegFrames==1&&test.wifi.maximumBytes<=4160);
    assert(jpegMeanAbsoluteError(test,photo)<30);
  }
  {
    Fixture test; startFastWiFi(test);
    assert(test.host.allowsLossyCompression);
    finishFrame(test, photo);
    assert(test.jpegFrames == 1 && test.rectangles == 1 && test.wifi.sends == 1);
    assert(test.wifi.maximumBytes <= 15500 && !test.usbRectangles);
    const NSUInteger jpegBytes = test.wifi.maximumBytes;
    double error = jpegMeanAbsoluteError(test, photo);
    assert(error > 0 && error < 20); // Decode uses the actual firmware JPEGDEC path.
    assert([[test.host valueForKey:@"needsLosslessRefresh"] boolValue]);
    // No more capture callbacks: an idle timer must still restore exact RGB565 pixels.
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 0.3) forKey:@"lastContentChange"];
    test.until([&] { return test.frames() == 2; });
    test.matches(photo, 240);
    assert(test.jpegFrames == 1 && ![[test.host valueForKey:@"needsLosslessRefresh"] boolValue]);
    const unsigned sends = test.wifi.sends;
    for (unsigned i = 0; i < 20; ++i) test.pump();
    assert(test.frames() == 2 && test.wifi.sends == sends); // No endless idle re-encoding.
    finishFrame(test, photo); assert(test.wifi.sends == sends && test.jpegFrames == 1);
    printf("BENCH JPEG Fast240 bytes=%lu RGB mean_absolute_error=%.2f then exact idle refinement\n",
           (unsigned long)jpegBytes, error);
  }
  {
    Fixture test; startFastWiFi(test);
    finishFrame(test, syntheticDesktop(240, 0, 0));
    assert(!test.jpegFrames); // Compressible text/UI stays lossless even in Adaptive mode.
    test.host.allowsLossyCompression = NO;
    finishFrame(test, photo); test.matches(photo, 240); assert(!test.jpegFrames);
    test.host.allowsLossyCompression = YES;
    NSData *next = photographicTexture(1);
    test.dropFirstAck = true;
    [test.host submitFrame:next]; test.until([&] { return test.dropped; });
    assert(test.jpegFrames == 1);
    const unsigned complete = test.frames();
    test.host.allowsLossyCompression = NO; // Disable while JPEG ACK is outstanding.
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
    test.until([&] { return test.frames() == complete + 2; });
    test.matches(next, 240);
    assert(test.jpegFrames == 1 && test.duplicates == 1 && !test.host.allowsLossyCompression);
  }
  {
    Fixture test; startFastWiFi(test);
    NSData *noise = syntheticDesktop(240, 3, 0);
    finishFrame(test, noise); test.matches(noise, 240);
    assert(!test.jpegFrames); // Oversized JPEG candidate falls back to bounded lossless batches.
  }
  {
    Fixture test; startFastWiFi(test); test.dropFirstAck = true;
    [test.host submitFrame:photo]; test.until([&] { return test.dropped; });
    [test.host endSession:@"Cancel pending JPEG"];
    test.until([&] { return !test.host.session; });
    assert(test.jpegFrames == 1 && test.releases == 1 && test.wifi.sends == 1 && !test.usbRectangles);
  }
  {
    Fixture test; test.request(); test.capabilities(31); test.until([&] { return test.ready == 1; });
    test.host.transferSize = 240; finishFrame(test, photo); test.matches(photo, 240);
    assert(!test.jpegFrames && test.usbRectangles); // JPEG is never routed through plain USB.
  }
  {
    Fixture test; startFastWiFi(test); test.dropFirstAck = true;
    [test.host submitFrame:photo]; test.until([&] { return test.dropped; });
    test.line("ROTATION", 1); [test.host readAvailable];
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
    test.until([&] { return test.frames() == 1; });
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 0.3) forKey:@"lastContentChange"];
    test.until([&] { return test.frames() == 2; });
    test.matches(turnFrame(photo, 240), 240);
    assert(test.duplicates == 1 && test.ready == 1 && !test.ended && !test.usbRectangles);
  }
  {
    Fixture test; startFastWiFi(test); test.dropFirstAck = true;
    [test.host submitFrame:photo]; test.until([&] { return test.dropped; });
    test.host.transferSize = 480;
    NSData *native = repeatedPixelTexture(480); [test.host submitFrame:native];
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
    test.until([&] { return test.frames() == 1; }); test.matches(native, 480);
    assert(test.jpegFrames == 1 && test.duplicates == 1 && ![[test.host valueForKey:@"needsLosslessRefresh"] boolValue]);
  }
}

static void wifiPerformanceDiagnosticsAreSessionBoundNumbers() {
  Fixture test;
  TestUSBTransport *host = (TestUSBTransport *)test.host;
  NSString *valid = [NSString stringWithFormat:@"DISPLAY PERF %016llx 1000 30 116767 7000 3000 200 1500 81 120000 13 4 8 -51 200000",
                     (unsigned long long)test.nonce];
  [host line:valid]; assert(host.performanceLogs == 0);
  test.request(true);
  [host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
  test.until([&] { return test.wifiConfigurations == 1; });
  [host line:valid]; assert(host.performanceLogs == 0); // No authenticated channel yet.
  test.attachWiFi(); test.until([&] { return test.ready == 1; });
  NSData *line = [[valid stringByAppendingString:@"\n"] dataUsingEncoding:NSASCIIStringEncoding];
  assert(write(test.master, line.bytes, line.length) == (ssize_t)line.length);
  [host readAvailable];
  assert(host.performanceLogs == 1 && host.lastPerformance.count == 14);
  assert(host.lastPerformance[0].unsignedIntValue == 1000);
  assert(host.lastPerformance[2].unsignedIntValue == 116767);
  assert(host.lastPerformance[12].intValue == -51 && host.lastPerformance[13].unsignedIntValue == 200000);
  NSArray<NSString *> *fields = [valid componentsSeparatedByString:@" "];
  for (unsigned index : {3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 16u}) {
    for (NSString *invalid in @[@"-1", @"+1", @"nan", @"1.5", @"4294967296", @"1suffix", @"", @"\t1"]) {
      NSMutableArray<NSString *> *bad = [fields mutableCopy]; bad[index] = invalid;
      [host line:[bad componentsJoinedByString:@" "]];
    }
  }
  for (NSString *invalid in @[@"1", @"-128", @"+0", @"--1", @"-"]) {
    NSMutableArray<NSString *> *bad = [fields mutableCopy]; bad[15] = invalid;
    [host line:[bad componentsJoinedByString:@" "]];
  }
  NSMutableArray<NSString *> *bad = [fields mutableCopy]; bad[2] = @"1020304050607081";
  [host line:[bad componentsJoinedByString:@" "]];
  [host line:[valid stringByAppendingString:@" 1"]];
  [host line:[[fields subarrayWithRange:NSMakeRange(0, 16)] componentsJoinedByString:@" "]];
  test.wifi.online = NO; [host line:valid]; test.wifi.online = YES;
  assert(host.performanceLogs == 1);
  // Cumulative uint32 counters may wrap: preserve each sample without invented deltas.
  NSMutableArray<NSString *> *wrapped = [fields mutableCopy]; wrapped[3] = @"4294967295";
  [host line:[wrapped componentsJoinedByString:@" "]];
  assert(host.performanceLogs == 2 && host.lastPerformance[0].unsignedIntValue == UINT32_MAX);
  wrapped[3] = @"0"; [host line:[wrapped componentsJoinedByString:@" "]];
  assert(host.performanceLogs == 3 && host.lastPerformance[0].unsignedIntValue == 0);
  [host endSession:@"Diagnostics test complete"]; [host line:valid];
  assert(host.performanceLogs == 3);
  test.until([&] { return !host.session; });
  ++test.nonce; test.receiver.begin(test.nonce, test.now); test.request();
  test.until([&] { return test.ready == 2; });
  [host line:[valid stringByReplacingOccurrencesOfString:@"1020304050607080" withString:@"1020304050607081"]];
  assert(host.performanceLogs == 3); // Ordinary USB sessions don't accept Wi-Fi diagnostics.
}

static void wifiCancellationNeverFallsBackToUSB() {
  for (bool pendingBatch : {false, true}) {
    Fixture test;
    test.request(true); test.capabilities(3);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; });
    if (pendingBatch) {
      test.attachWiFi(); test.until([&] { return test.ready == 1; });
      test.dropFirstAck = true;
      [test.host submitFrame:syntheticDesktop(480, 3, 0)];
      test.until([&] { return test.dropped; });
      assert(test.rectangles == 4 && test.wifi.sends == 1);
      assert([[test.host valueForKey:@"awaiting"] boolValue]);
      // Make the old pixel batch immediately retryable when cancellation occurs.
      [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
    } else {
      // Setup is cancellable before a channel exists or HELLO is permitted.
      assert(!test.host.wifiConnected && !test.ready && !test.receiver.connected());
    }
    [test.host endSession:@"Cancel Wi-Fi display"];
    test.until([&] { return !test.host.session; });
    for (unsigned i = 0; i < 10; ++i) test.pump();
    assert(test.ended == 1 && test.releases == 1 && !test.usbRectangles);
    assert(test.rectangles == (pendingBatch ? 4u : 0u));
    assert(!test.duplicates && !test.host.wifiRequested && !test.host.wifiConnected);
    assert(![test.host valueForKey:@"tx"] && ![test.host valueForKey:@"pendingFrame"]);
    if (pendingBatch) assert(test.wifi.sends == 1 && !test.wifi.ready);
    else assert(!test.ready && !test.wifi);
    test.request(true); assert(test.requests == 1); // Cancellation cannot auto-reconnect.
  }
}

static void wifiBatchesCommitOnlyFinalAckAndRecover() {
  for (bool truncate : {false, true}) for (unsigned size : {480u, 240u}) {
    Fixture test;
    test.request(true); test.capabilities(3);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; });
    test.attachWiFi(); test.until([&] { return test.ready == 1; });
    test.host.transferSize = size;
    NSMutableData *frame = syntheticDesktop(size, 3, 0); // Raw fallback, near maximum wire size.
    test.truncateFirstBatch = truncate;
    test.dropFirstAck = !truncate;
    [test.host submitFrame:frame];
    test.until([&] { return truncate ? test.truncated : test.dropped; });
    const unsigned firstBatch = size == 480 ? 4 : 16;
    assert(test.rectangles == (truncate ? 2u : firstBatch));
    assert([[test.host valueForKey:@"awaiting"] boolValue]);
    assert([[test.host valueForKey:@"lastSequence"] unsignedIntValue] == 0);
    assert([[test.host valueForKey:@"row"] unsignedIntValue] == 0);
    assert([[test.host valueForKey:@"frameRects"] unsignedIntValue] == 0);
    test.line("ACK", 1); test.line("ACK", 2); test.line("ACK", 3);
    [test.host readAvailable]; // Earlier ACKs must never commit a partially delivered batch.
    assert([[test.host valueForKey:@"row"] unsignedIntValue] == 0 && !test.frames());
    assert(test.wifi.sends == 1 && test.wifi.maximumBytes <= 65536);
    if (size == 480) assert(test.wifi.maximumBytes > 61000);
    else assert(test.wifi.maximumBytes > 15000);
    [test.host setValue:@([NSProcessInfo processInfo].systemUptime - 2) forKey:@"sentAt"];
    test.until([&] { return test.frames() == 1; });
    assert(test.rectangles == 30 && test.wifiRectangles == 30 && !test.usbRectangles);
    assert(test.wifi.sends == (size == 480 ? 9u : 3u) && test.duplicates == 1);
    assert(test.rejectedReplays == (truncate ? 1u : firstBatch - 1));
    test.matches(frame, size);
    const unsigned sends = test.wifi.sends;
    finishFrame(test, frame);
    assert(test.wifi.sends == sends); // All acknowledged bands now share the exact baseline.
    paint(frame, size, 3, 1, 2, 1, 0xBEEF);
    paint(frame, size, 7, size / 2 + 1, 2, 1, 0xCAFE);
    finishFrame(test, frame);
    assert(test.wifi.sends == sends + 1 && test.rectangles == 32); // Disjoint dirty bands also batch.
    test.matches(frame, size);
  }
  {
    Fixture test;
    test.request(true);
    [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    test.until([&] { return test.wifiConfigurations == 1; });
    test.attachWiFi(); test.until([&] { return test.ready == 1; });
    [test.host setValue:@(UINT32_MAX - 2) forKey:@"lastSequence"];
    [test.host submitFrame:[NSMutableData dataWithLength:480 * 480 * 2]];
    test.until([&] { return !test.host.session; });
    assert(test.wifi.sends == 0 && test.releases == 1 && test.ended == 1);
  }
}


static void startWirelessOverUSB(Fixture &test, uint32_t caps = 127) {
  test.request(true); test.capabilities(caps);
  assert(![[test.host valueForKey:@"wirelessControl"] boolValue]);
  [test.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
  test.until([&] { return test.wifiConfigurations == 1; });
  NSString *hex = [@"a" stringByPaddingToLength:64 withString:@"a" startingAtIndex:0];
  [test.host line:[NSString stringWithFormat:@"DISPLAY PAIRING %016llx 0123456789abcdef0123456789abcdef %@ %@", (unsigned long long)test.nonce, hex, hex]];
  assert(test.pairings.saves == 1);
  test.wifi = [[TestWiFiChannel alloc] init];
  ((TestUSBTransport *)test.host).nextChannel = test.wifi;
  test.wirelessControl = true;
  [test.host line:[NSString stringWithFormat:@"DISPLAY WIFI_READY %016llx 127.0.0.1 8443 %@ %@", (unsigned long long)test.nonce, hex, hex]];
  assert(test.wifi.connects == 1 && [[test.host valueForKey:@"wirelessControl"] boolValue]);
  test.until([&] { return test.ready == 1; });
  test.until([&] { return test.audioConfigurations == ((caps & 64) ? 1u : 0u); });
  assert(test.host.wifiConnected && !test.host.audioActive && !test.usbRectangles);
}

static void wirelessControlsDetachAndTeardown() {
  {
    Fixture test; startWirelessOverUSB(test);
    unsigned saves = test.pairings.saves;
    [test.host networkLine:[NSString stringWithFormat:@"DISPLAY PAIRING %016llx 0123456789abcdef0123456789abcdef %@ %@", (unsigned long long)test.nonce,
                           [@"b" stringByPaddingToLength:64 withString:@"b" startingAtIndex:0], [@"b" stringByPaddingToLength:64 withString:@"b" startingAtIndex:0]]];
    assert(test.pairings.saves == saves); // TLS cannot overwrite USB-established trust.
    [test.host line:[NSString stringWithFormat:@"DISPLAY STOP %016llx 0", (unsigned long long)test.nonce]];
    [test.host line:[NSString stringWithFormat:@"DISPLAY ROTATION %016llx 3", (unsigned long long)test.nonce]];
    assert(!test.ended && [[test.host valueForKey:@"rotation"] unsignedIntValue] == 0);
    [test.host networkLine:@"DISPLAY STOP 0000000000000001 0"];
    [test.host networkLine:[NSString stringWithFormat:@"DISPLAY STOP %016llx not-a-number", (unsigned long long)test.nonce]];
    [test.host networkLine:[NSString stringWithFormat:@"DISPLAY ROTATION %016llx 4", (unsigned long long)test.nonce]];
    assert(!test.ended && ![[test.host valueForKey:@"rotation"] unsignedIntValue]);
    test.line("ROTATION", 1);
    assert([[test.host valueForKey:@"rotation"] unsignedIntValue] == 1);
    test.line("ROTATION", 0);
    [test.host serialDisconnected:@"Synthetic unplug"];
    assert([[test.host valueForKey:@"fd"] intValue] == -1 && test.host.session == test.nonce && !test.ended);
    NSMutableData *frame = [NSMutableData dataWithLength:moss_wire::frameBytes];
    paint(frame, 480, 0, 0, 480, 480, 0x3456); finishFrame(test, frame); test.matches(frame, 480);
    assert(test.wifiRectangles == 30 && !test.usbRectangles);
    [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 2) forKey:@"lastTransmit"];
    test.until([&] { return test.pings == 1; });
    test.until([&] { return ![[test.host valueForKey:@"awaiting"] boolValue]; });
    [test.host endSession:@"Wireless menu disconnect"];
    test.until([&] { return !test.host.session; });
    assert(test.ended == 1 && test.releases == 1 && !test.host.wifiConnected);
  }
  {
    Fixture test; startWirelessOverUSB(test, 63);
    test.line("STOP", 0);
    assert(!test.host.session && test.ended == 1 && !test.wifi.ready);
  }
  {
    Fixture test; startWirelessOverUSB(test, 63);
    test.wifi.failureHandler(@"Synthetic authenticated connection failure");
    assert(!test.host.session && test.ended == 1 && !test.wifi.ready);
  }
}

static MossDiscoveredDevice *testEndpoint(uint64_t nonce) {
  MossDiscoveredDevice *device = [[MossDiscoveredDevice alloc] init];
  [device setValue:@"0123456789abcdef0123456789abcdef" forKey:@"deviceID"];
  [device setValue:@(nonce) forKey:@"nonce"];
  [device setValue:@"127.0.0.1" forKey:@"host"]; [device setValue:@"8443" forKey:@"port"];
  return device;
}
static void wirelessStartsWithoutUSBAndRequiresMetadata() {
  for (bool validMetadata : {false, true}) {
    Fixture test;
    [test.host serialDisconnected:@"No USB at app startup"];
    test.wifi = [[TestWiFiChannel alloc] init];
    ((TestUSBTransport *)test.host).nextChannel = test.wifi;
    test.wirelessControl = true;
    NSString *token = [@"a" stringByPaddingToLength:64 withString:@"a" startingAtIndex:0];
    [test.pairings saveDevice:@"0123456789abcdef0123456789abcdef" fingerprint:[NSMutableData dataWithLength:32] token:token error:nil];
    [test.host discovered:testEndpoint(test.nonce)];
    assert(test.wifi.connects == 1 && test.pairings.reads == 1 && test.requests == 0);
    assert([[test.host valueForKey:@"networkRequestPending"] boolValue] && [[test.host valueForKey:@"wifiDeadline"] doubleValue] > 0);
    [test.host networkLine:@"DISPLAY WIFI_REQUEST 0000000000000001 480 480 15360"];
    [test.host networkLine:[NSString stringWithFormat:@"DISPLAY WIFI_REQUEST %016llx 240 480 15360", (unsigned long long)test.nonce]];
    assert(!test.requests && !test.wifi.packets.count);
    if (!validMetadata) {
      [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 1) forKey:@"wifiDeadline"];
      test.pump(); assert(!test.host.session && test.ended == 1);
      continue;
    }
    [test.host networkLine:[NSString stringWithFormat:@"DISPLAY WIFI_REQUEST %016llx 480 480 15360", (unsigned long long)test.nonce]];
    test.capabilities(63);
    test.until([&] { return test.ready == 1; });
    assert(test.requests == 1 && [[test.host valueForKey:@"fd"] intValue] == -1 && ![[test.host valueForKey:@"wifiDeadline"] doubleValue]);
    finishFrame(test, [NSMutableData dataWithLength:moss_wire::frameBytes]);
    assert(test.wifiRectangles == 30 && test.usbRectangles == 0);
    [test.host endSession:@"Wireless-only test ended"];
    test.until([&] { return !test.host.session; });
    assert(test.releases == 1 && test.ended == 1);
  }
  // Unpaired announcements never cause a credential read or network connection.
  Fixture unknown; [unknown.host serialDisconnected:@"No USB"];
  [unknown.host discovered:testEndpoint(unknown.nonce)];
  assert(!unknown.host.session && !unknown.pairings.reads);
}

static void keychainDenialDoesNotRepromptSameMode() {
  Fixture test; [test.host serialDisconnected:@"No USB"];
  test.wifi = [[TestWiFiChannel alloc] init];
  ((TestUSBTransport *)test.host).nextChannel = test.wifi;
  NSString *token = [@"a" stringByPaddingToLength:64 withString:@"a" startingAtIndex:0];
  [test.pairings saveDevice:@"0123456789abcdef0123456789abcdef" fingerprint:[NSMutableData dataWithLength:32] token:token error:nil];
  test.pairings.denyReads = YES;
  [test.host discovered:testEndpoint(test.nonce)];
  assert(test.pairings.reads == 1 && !test.wifi.connects && !test.host.session);
  assert([[test.host valueForKey:@"lastStatus"] containsString:@"unlock Keychain"]);
  for (unsigned i = 0; i < 5; ++i) {
    [test.host setValue:@0 forKey:@"lastDiscoveryCheck"];
    [test.host discovered:testEndpoint(test.nonce)];
  }
  assert(test.pairings.reads == 1 && !test.wifi.connects);
  test.pairings.denyReads = NO; // Unlock alone does not silently retry a denied mode.
  [test.host setValue:@0 forKey:@"lastDiscoveryCheck"];
  [test.host discovered:testEndpoint(test.nonce)];
  assert(test.pairings.reads == 1);
  ++test.nonce;
  [test.host setValue:@0 forKey:@"lastDiscoveryCheck"];
  [test.host discovered:testEndpoint(test.nonce)];
  assert(test.pairings.reads == 2 && test.wifi.connects == 1);
}

static void audioIsBoundedAndDoesNotRetireImageACKs() {
  {
    Fixture paced;startWirelessOverUSB(paced);paced.host.audioEnabled=YES;
    paced.until([&]{return paced.host.audioActive;});
    [paced.host submitAudio:[NSMutableData dataWithLength:1600]];
    paced.until([&]{return paced.audioPackets==1;});
    [paced.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO_BUFFER %016llx 1024",(unsigned long long)paced.nonce]];
    NSMutableData *image=[NSMutableData dataWithLength:moss_wire::frameBytes];paint(image,480,0,0,480,480,0x1234);
    [paced.host submitFrame:image];for(unsigned i=0;i<10;++i)paced.pump();
    assert(!paced.rectangles);
    [paced.host networkLine:@"DISPLAY AUDIO_BUFFER 0000000000000001 4096"];
    paced.pump();assert(!paced.rectangles);
    [paced.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO_BUFFER %016llx 4096",(unsigned long long)paced.nonce]];
    paced.until([&]{return paced.frames()==1;});paced.matches(image,480);
    paint(image,480,2,2,2,2,0x4321);
    [paced.host submitAudio:[NSMutableData dataWithLength:1600]];
    [paced.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO_BUFFER %016llx 0",(unsigned long long)paced.nonce]];
    [paced.host submitFrame:image];paced.pump();assert(paced.frames()==1);
    [paced.host setValue:@(NSProcessInfo.processInfo.systemUptime-0.2) forKey:@"videoAudioWaitSince"];
    paced.until([&]{return paced.frames()==2;});paced.matches(image,480);
  }
  Fixture test; test.host.audioVolume = 99; startWirelessOverUSB(test);
  test.host.audioEnabled = YES;
  test.until([&] { return test.host.audioActive; });
  assert(test.host.audioVolume == 60 && test.audioConfigurations == 2);
  TestUSBTransport *diagnostics = (TestUSBTransport *)test.host;
  NSString *stats = [NSString stringWithFormat:@"DISPLAY AUDIO_PERF %016llx 1 2 3 4 5 6 7 16000 15900", (unsigned long long)test.nonce];
  [test.host networkLine:stats]; assert(diagnostics.audioPerformanceLogs == 1);
  [test.host line:stats];
  [test.host networkLine:[stats stringByAppendingString:@" extra"]];
  [test.host networkLine:[stats stringByReplacingOccurrencesOfString:@" 16000 " withString:@" -1 "]];
  [test.host networkLine:[stats stringByReplacingOccurrencesOfString:@" 16000 " withString:@" 4294967296 "]];
  [test.host networkLine:[stats stringByReplacingOccurrencesOfString:@" 16000 15900" withString:@""]];
  assert(diagnostics.audioPerformanceLogs == 1);
  test.dropFirstAck = true;
  NSMutableData *frame = [NSMutableData dataWithLength:moss_wire::frameBytes];
  paint(frame, 480, 0, 0, 480, 480, 0x1234);
  [test.host submitFrame:frame]; test.until([&] { return test.dropped; });
  const uint32_t waiting = [[test.host valueForKey:@"waitingSequence"] unsignedIntValue];
  const uint32_t sequence = [[test.host valueForKey:@"lastSequence"] unsignedIntValue];
  const uint32_t receiverSequence = test.receiver.sequence();
  for (uint8_t value : {0x11, 0x22, 0x33}) {
    NSMutableData *pcm = [NSMutableData dataWithLength:3200]; memset(pcm.mutableBytes, value, pcm.length);
    [test.host submitAudio:pcm]; memset(pcm.mutableBytes, 0xff, pcm.length);
  }
  assert([[test.host valueForKey:@"audioPending"] length] == 6400);
  [test.host submitAudio:[NSMutableData dataWithLength:3201]];
  [test.host submitAudio:[NSMutableData dataWithLength:3202]];
  assert([[test.host valueForKey:@"audioPending"] length] == 6400);
  test.dropAudioAck = true;
  test.until([&] { return test.audioPackets == 1; });
  assert([[test.host valueForKey:@"audioAwaiting"] boolValue]);
  for (unsigned i = 0; i < 5; ++i) test.pump();
  assert(test.audioPackets == 1); // Completed network write is not device consumption credit.
  [test.host networkLine:@"DISPLAY AUDIO_ACK 0000000000000001"];
  [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO_ACK %016llx extra", (unsigned long long)test.nonce]];
  [test.host line:[NSString stringWithFormat:@"DISPLAY AUDIO_ACK %016llx", (unsigned long long)test.nonce]];
  assert([[test.host valueForKey:@"audioAwaiting"] boolValue]);
  test.dropAudioAck = false;
  [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO_ACK %016llx", (unsigned long long)test.nonce]];
  test.until([&] { return test.audioPackets == 2; });
  assert(test.audioBytes.size() == 6400);
  for (unsigned i = 0; i < 6400; ++i) assert(test.audioBytes[i] == (i < 3200 ? 0x22 : 0x33));
  assert(test.receiver.sequence() == receiverSequence);
  assert([[test.host valueForKey:@"waitingSequence"] unsignedIntValue] == waiting && [[test.host valueForKey:@"lastSequence"] unsignedIntValue] == sequence);
  assert([[test.host valueForKey:@"awaiting"] boolValue] && [test.host valueForKey:@"pendingFrame"]);
  [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 2) forKey:@"sentAt"];
  test.until([&] { return test.frames() == 1; }); test.matches(frame, 480);
  // The image ACK may arrive while an independent audio write still completes.
  test.dropped = false;
  paint(frame, 480, 8, 8, 2, 2, 0x5678);
  [test.host submitFrame:frame]; test.until([&] { return test.dropped; });
  test.wifi.holdCompletion = YES; test.dropAudioAck = true;
  [test.host submitAudio:[NSMutableData dataWithLength:320]];
  for (unsigned i = 0; i < 3; ++i) test.pump();
  assert(test.audioPackets == 2); // Coalesce at least 50 ms to reduce packet overhead.
  [test.host submitAudio:[NSMutableData dataWithLength:1280]];
  test.until([&] { return test.audioPackets == 3; });
  assert(test.wifi.pendingCompletion && [[test.host valueForKey:@"networkWriting"] boolValue]);
  test.line("ACK", [[test.host valueForKey:@"waitingSequence"] unsignedIntValue]);
  assert(![[test.host valueForKey:@"awaiting"] boolValue]);
  for (unsigned i = 0; i < 3; ++i) test.pump();
  assert(test.frames() == 1); // Pending audio completion still owns the network writer.
  void (^completion)(NSError *) = test.wifi.pendingCompletion;
  test.wifi.pendingCompletion = nil; test.wifi.holdCompletion = NO; completion(nil);
  test.until([&] { return test.frames() == 2; }); test.matches(frame, 480);
  assert([[test.host valueForKey:@"audioAwaiting"] boolValue]);
  test.host.audioEnabled = NO;
  assert(![[test.host valueForKey:@"audioAwaiting"] boolValue]);
  test.until([&] { return test.audioConfigurations == 3; });
  assert(!test.host.audioActive);
  [test.host submitAudio:[NSMutableData dataWithLength:3200]];
  assert([[test.host valueForKey:@"audioPending"] length] == 0);
  [test.host endSession:@"Audio test ended"]; test.until([&] { return !test.host.session; });

  Fixture usb; usb.request(); usb.capabilities(127); usb.until([&] { return usb.ready == 1; });
  usb.host.audioEnabled = YES;
  [usb.host submitAudio:[NSMutableData dataWithLength:3200]];
  for (unsigned i = 0; i < 5; ++i) usb.pump();
  assert(!usb.audioConfigurations && !usb.audioPackets && !usb.host.audioActive);
}

static NSString *volumeLine(uint64_t nonce, NSString *volume) {
  return [NSString stringWithFormat:@"DISPLAY VOLUME %016llx %@", (unsigned long long)nonce, volume];
}
static NSData *frameWithVolumeOverlay(NSData *source, unsigned size, uint8_t volume) {
  std::vector<uint16_t> overlay(240 * 240);
  sloth::drawVolumeOverlay(overlay.data(), volume);
  NSMutableData *result = [source mutableCopy];
  auto *pixels = static_cast<uint16_t *>(result.mutableBytes);
  const unsigned scale = size / 240;
  for (unsigned y = sloth::kVolumeOverlayY * scale; y < (sloth::kVolumeOverlayY + sloth::kVolumeOverlayHeight) * scale; ++y)
    for (unsigned x = sloth::kVolumeOverlayX * scale; x < (sloth::kVolumeOverlayX + sloth::kVolumeOverlayWidth) * scale; ++x)
      pixels[y * size + x] = overlay[(y / scale) * 240 + x / scale];
  return result;
}
static void drainComposition(Fixture &test) {
  test.until([&] {
    return ![test.host valueForKey:@"latestFrame"] && ![test.host valueForKey:@"sendingFrame"] &&
           ![test.host valueForKey:@"pendingFrame"];
  });
}
static void volumeOverlayRestoresStaticAndChangingDesktops() {
  for (bool wireless : {false, true}) for (unsigned size : {240u, 480u}) for (unsigned turns = 0; turns < 4; ++turns) {
    Fixture test;
    if (wireless) startWirelessOverUSB(test);
    else { test.request(); test.capabilities(15); test.until([&] { return test.ready == 1; }); }
    test.host.transferSize = size; test.host.allowsLossyCompression = NO;
    test.line("ROTATION", turns); [test.host readAvailable];
    NSMutableData *desktop = [NSMutableData dataWithLength:size * size * 2];
    paint(desktop, size, 0, 0, size, size, 0x1357);
    finishFrame(test, desktop);
    auto orient = [&](NSData *pixels) {
      for (unsigned i = 0; i < turns; ++i) pixels = turnFrame(pixels, size);
      return pixels;
    };
    for (NSString *volume in @[@"0", @"30", @"60"]) {
      if (wireless) [test.host networkLine:volumeLine(test.nonce, volume)];
      else [test.host line:volumeLine(test.nonce, volume)];
      const double remaining = [[test.host valueForKey:@"volumeOverlayDeadline"] doubleValue] - NSProcessInfo.processInfo.systemUptime;
      assert(remaining > 0.9 && remaining <= 1.0);
      drainComposition(test);
      test.matches(orient(frameWithVolumeOverlay(desktop, size, volume.intValue)), size);
      assert([[test.host valueForKey:@"sourceFrame"] isEqualToData:desktop]);
      assert(!test.host.audioEnabled && !test.host.audioActive);
    }
    // A repeated press at max must extend visibility even though gain is unchanged.
    [test.host setValue:@(NSProcessInfo.processInfo.systemUptime + 0.05) forKey:@"volumeOverlayDeadline"];
    if (wireless) [test.host networkLine:volumeLine(test.nonce, @"60")];
    else [test.host line:volumeLine(test.nonce, @"60")];
    assert([[test.host valueForKey:@"volumeOverlayDeadline"] doubleValue] > NSProcessInfo.processInfo.systemUptime + 0.9);
    // New capture pixels both under and outside the card survive its lifetime.
    paint(desktop, size, 0, 0, size, size, 0x2468);
    finishFrame(test, desktop);
    test.matches(orient(frameWithVolumeOverlay(desktop, size, 60)), size);
    [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 0.01) forKey:@"volumeOverlayDeadline"];
    test.pump(); drainComposition(test); // No capture submitted on expiry.
    test.matches(orient(desktop), size);
    assert(![test.host valueForKey:@"volumeOverlayPixels"]);
    // The Mac volume menu uses the same card without opting into audio.
    test.host.audioVolume = 20; drainComposition(test);
    test.matches(orient(frameWithVolumeOverlay(desktop, size, 20)), size);
    [test.host endSession:@"Overlay cleanup"];
    assert(![test.host valueForKey:@"volumeOverlayPixels"] && ![[test.host valueForKey:@"volumeOverlayDeadline"] doubleValue]);
    assert(!test.host.audioEnabled && test.ended == 1);
  }
}
static void volumeOverlayPreservesRetryAndLatestPixels() {
  for (bool wireless : {false, true}) {
    Fixture test;
    if (wireless) startWirelessOverUSB(test);
    else { test.request(); test.capabilities(15); test.until([&] { return test.ready == 1; }); }
    test.host.transferSize = 240; test.host.allowsLossyCompression = NO;
    NSMutableData *desktop = [NSMutableData dataWithLength:240 * 240 * 2];
    finishFrame(test, desktop);
    test.dropFirstAck = true;
    test.host.audioVolume = 30;
    test.until([&] { return test.dropped; });
    NSData *pendingPacket = [[test.host valueForKey:@"tx"] copy];
    assert(pendingPacket.length && [[test.host valueForKey:@"awaiting"] boolValue]);
    test.host.audioVolume = 60;
    assert([pendingPacket isEqualToData:[test.host valueForKey:@"tx"]]);
    paint(desktop, 240, 0, 0, 240, 240, 0x7654);
    [test.host submitFrame:desktop];
    [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 0.01) forKey:@"volumeOverlayDeadline"];
    test.pump(); // Expire while an earlier overlay packet still awaits its ACK.
    assert([pendingPacket isEqualToData:[test.host valueForKey:@"tx"]]);
    [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 2) forKey:@"sentAt"];
    drainComposition(test);
    test.matches(desktop, 240);
    assert(test.duplicates && !test.ended && ![test.host valueForKey:@"volumeOverlayPixels"]);
  }
}
static void deviceVolumeIsSessionBoundAndDoesNotEnableAudio() {
  {
    Fixture test(false); // A hardware choice is retained while awaiting approval/HELLO.
    __block unsigned updates = 0;
    test.host.audioVolumeHandler = ^(NSUInteger volume) { assert(volume <= 60); ++updates; };
    [test.host line:volumeLine(test.nonce, @"40")];
    assert(test.host.audioVolume == 35 && !updates); // No session yet.
    test.request();
    [test.host line:volumeLine(test.nonce, @"40")];
    assert(test.host.audioVolume == 40 && updates == 1 && !test.host.audioEnabled && !test.ready);
    [test.host line:volumeLine(test.nonce, @"40")]; assert(updates == 1);
    for (NSString *invalid in @[@"", @"61", @"-1", @"+1", @"0x20", @"1.0", @" 25", @"25 ", @"25 extra", @"\t25", @"999999999999999999999999"])
      [test.host line:volumeLine(test.nonce, invalid)];
    [test.host line:volumeLine(1, @"25")];
    [test.host line:@"DISPLAY VOLUME 0x20304050607080 25"];
    assert(test.host.audioVolume == 40 && updates == 1);
    [test.host acceptRequest:test.nonce]; test.until([&] { return test.ready == 1; });
    [test.host line:volumeLine(test.nonce, @"0")];
    assert(test.host.audioVolume == 0 && updates == 2 && !test.host.audioEnabled);
    [test.host endSession:@"USB volume test ended"];
    [test.host line:volumeLine(test.nonce, @"60")];
    assert(test.host.audioVolume == 0 && updates == 2); // Stopping rejects late presses.
  }
  {
    Fixture test; startWirelessOverUSB(test);
    __block unsigned updates = 0;
    test.host.audioVolumeHandler = ^(NSUInteger volume) { assert(volume <= 60); ++updates; };
    const unsigned configs = test.audioConfigurations;
    [test.host line:volumeLine(test.nonce, @"45")]; // USB is no longer the owning channel.
    [test.host networkLine:volumeLine(1, @"45")];
    assert(test.host.audioVolume == 35 && !updates);
    [test.host networkLine:volumeLine(test.nonce, @"45")];
    for (unsigned i = 0; i < 5; ++i) test.pump();
    assert(test.host.audioVolume == 45 && updates == 1 && test.audioConfigurations == configs);
    assert(!test.host.audioEnabled && !test.host.audioActive);
    [test.host serialDisconnected:@"Wireless volume after unplug"];
    [test.host networkLine:volumeLine(test.nonce, @"60")];
    assert(test.host.audioVolume == 60 && updates == 2);
    test.host.audioEnabled = YES; test.until([&] { return test.host.audioActive; });
    assert(test.audioConfiguredVolumes.back() == 60);
    [test.host networkLine:volumeLine(test.nonce, @"55")];
    for (unsigned i = 0; i < 5; ++i) test.pump();
    assert(test.host.audioVolume == 55 && updates == 3 && test.host.audioEnabled && test.host.audioActive);
    assert(test.audioConfigurations == configs + 1); // Local gain was already applied; no echo.
    // An unsequenced status echo never replaces an explicit button preference.
    [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO %016llx 1 50 0", (unsigned long long)test.nonce]];
    assert(test.host.audioVolume == 55 && updates == 3 && test.host.audioEnabled);
    [test.host endSession:@"Wireless volume test ended"];
    [test.host networkLine:volumeLine(test.nonce, @"0")];
    assert(test.host.audioVolume == 55 && updates == 3);
  }
  {
    Fixture legacy; legacy.request(true); legacy.capabilities(3);
    [legacy.host configureWiFiSSID:@"TestNetwork" password:@"dummy-password"];
    legacy.until([&] { return legacy.wifiConfigurations == 1; });
    legacy.attachWiFi(); legacy.until([&] { return legacy.ready == 1; });
    [legacy.host networkLine:volumeLine(legacy.nonce, @"45")];
    assert(legacy.host.audioVolume == 35); // Pixels over TLS alone do not own controls.
    [legacy.host line:volumeLine(legacy.nonce, @"45")];
    assert(legacy.host.audioVolume == 45);
  }
}

static void oldAudioConfigurationCannotOverwriteNewerVolume() {
  Fixture test; startWirelessOverUSB(test);
  test.dropAudioConfigAck = true;
  test.host.audioEnabled = YES;
  test.until([&] { return test.audioConfigurations == 2; });
  assert([[test.host valueForKey:@"audioConfigurationAwaiting"] boolValue]);
  [test.host networkLine:volumeLine(test.nonce, @"40")];
  [test.host networkLine:volumeLine(test.nonce, @"45")];
  for (unsigned i = 0; i < 5; ++i) test.pump();
  assert(test.audioConfigurations == 2 && test.host.audioVolume == 45); // Coalesced until old reply.
  [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO %016llx 1 35 0", (unsigned long long)test.nonce]];
  assert(test.host.audioVolume == 45 && test.host.audioActive);
  test.until([&] { return test.audioConfigurations == 3; });
  assert(test.audioConfiguredVolumes.back() == 45);
  test.host.audioVolume = 20; // Mac-menu changes have the same protection.
  [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO %016llx 1 45 0", (unsigned long long)test.nonce]];
  assert(test.host.audioVolume == 20);
  test.dropAudioConfigAck = false;
  test.until([&] { return test.audioConfigurations == 4; });
  assert(test.audioConfiguredVolumes.back() == 20 && test.host.audioVolume == 20);
  for (unsigned i = 0; i < 5; ++i) test.pump();
  assert(test.audioConfigurations == 4 && ![[test.host valueForKey:@"audioConfigurationAwaiting"] boolValue]);
  // A hardware gain failure can arrive before the old successful config ACK.
  test.dropAudioConfigAck = true;
  test.host.audioVolume = 30;
  test.until([&] { return test.audioConfigurations == 5; });
  __weak MossUSBTransport *weakHost = test.host;
  test.host.audioStatusHandler = ^(BOOL active, NSString *message) {
    if (!active && [message containsString:@"could not"]) weakHost.audioEnabled = NO;
  };
  [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO %016llx 0 35 1", (unsigned long long)test.nonce]];
  [test.host networkLine:volumeLine(test.nonce, @"35")];
  assert(test.host.audioVolume == 35 && !test.host.audioEnabled && !test.host.audioActive);
  assert([[test.host valueForKey:@"audioConfigurationAwaiting"] boolValue]);
  [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO %016llx 1 30 0", (unsigned long long)test.nonce]];
  assert(test.host.audioVolume == 35 && !test.host.audioActive); // Success cannot reenable a failed/off preference.
  test.until([&] { return test.audioConfigurations == 6; });
  assert(test.audioConfiguredVolumes.back() == 35);
  // Missing audio ACKs disable only audio and allow the desktop to keep working.
  test.host.audioEnabled = YES;
  [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO %016llx 0 35 0", (unsigned long long)test.nonce]];
  test.until([&] { return test.audioConfigurations == 7; });
  [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 1) forKey:@"audioConfigurationDeadline"];
  test.pump();
  assert(!test.host.audioEnabled && !test.host.audioActive && test.host.session == test.nonce);
  test.until([&] { return test.audioConfigurations == 8; });
  [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 1) forKey:@"audioConfigurationDeadline"];
  test.pump();
  for (unsigned i = 0; i < 5; ++i) test.pump();
  assert(test.audioConfigurations == 8 && ![[test.host valueForKey:@"audioConfigurationAwaiting"] boolValue]);
  finishFrame(test, [NSMutableData dataWithLength:moss_wire::frameBytes]);
  assert(test.frames() == 1 && !test.ended);
  // Turning audio off while an enable ACK is missing still queues a mute.
  test.host.audioEnabled = YES;
  test.until([&] { return test.audioConfigurations == 9; });
  test.host.audioEnabled = NO;
  [test.host setValue:@(NSProcessInfo.processInfo.systemUptime - 1) forKey:@"audioConfigurationDeadline"];
  test.pump(); test.until([&] { return test.audioConfigurations == 10; });
  [test.host networkLine:[NSString stringWithFormat:@"DISPLAY AUDIO %016llx 0 35 0", (unsigned long long)test.nonce]];
  assert(!test.host.audioEnabled && !test.host.audioActive && ![[test.host valueForKey:@"audioConfigurationAwaiting"] boolValue]);
  test.host.audioStatusHandler = nil;
}

int main() {
  @autoreleasepool {
    wirelessControlsDetachAndTeardown();
    wirelessStartsWithoutUSBAndRequiresMetadata();
    audioIsBoundedAndDoesNotRetireImageACKs();
    deviceVolumeIsSessionBoundAndDoesNotEnableAudio();
    volumeOverlayRestoresStaticAndChangingDesktops();
    volumeOverlayPreservesRetryAndLatestPixels();
    oldAudioConfigurationCannotOverwriteNewerVolume();
    keychainDenialDoesNotRepromptSameMode();
    adaptiveJpegIsBoundedAndRefinesExactly();
    losslessLz4AndCroppedTransfers();
    wifiPerformanceDiagnosticsAreSessionBoundNumbers();
    wifiCancellationNeverFallsBackToUSB();
    wifiBatchesCommitOnlyFinalAckAndRecover();
    wifiProvisioningAndFailures();
    rotationWhileIdleAndAwaitingAck();
    changedBandsAndAcknowledgedBaseline();
    negotiatedCompressionAndResolutionChanges();
    benchmarkDesktopChanges();
    {
      Fixture test;
      test.dropFirstAck = true;
      test.request();
      test.until([&] { return test.ready == 1; });
      assert(test.requests == 1);
      test.request(); // Repeated firmware beacon cannot create a second desktop.
      assert(test.requests == 1);
      NSMutableData *frame = [NSMutableData dataWithLength:480 * 480 * 2];
      [test.host submitFrame:frame];
      test.until([&] { return test.rectangles == 30; });
      assert(test.duplicates == 1);
      [test.host endSession:@"Menu disconnect"];
      test.until([&] { return test.host.session == 0; });
      assert(test.ended == 1 && test.releases == 1);
      test.request();
      assert(test.requests == 1); // An old session never auto-restarts.
    }
    {
      Fixture test;
      test.request();
      test.until([&] { return test.ready == 1; });
      [test.host submitFrame:[NSMutableData dataWithLength:480 * 480 * 2]];
      test.until([&] { return test.rectangles == 4; });
      test.receiver.cancel();
      test.line("STOP", test.receiver.sequence());
      [test.host readAvailable];
      assert(test.ended == 1);
      test.until([&] { return test.host.session == 0; });
      assert(test.releases == 1 && test.rectangles == 4);
    }
    {
      Fixture test(false);
      test.request();
      [test.host reportHostStatus:1];
      test.pump();
      assert(test.ready == 0 && !test.receiver.connected());
      [test.host endSession:@"Permission declined"];
      test.until([&] { return test.host.session == 0; });
      assert(test.ended == 1 && test.releases == 1 && test.ready == 0);
    }
    {
      Fixture test;
      test.receiver.cancel(); // A previous host left firmware quarantined.
      test.line("IDLE", 0);
      [test.host readAvailable];
      test.until([&] { return test.releases == 1; });
      assert(test.host.session == 0 && test.requests == 0 && test.ready == 0);
      test.line("IDLE", 0);
      test.pump();
      assert(test.releases == 1); // No repeated cleanup on normal status replies.
      test.request();
      assert(test.requests == 0); // Old mode request cannot restart capture.
      ++test.nonce;
      test.receiver.begin(test.nonce, test.now);
      test.request();
      test.until([&] { return test.ready == 1; });
    }
    {
      Fixture test;
      test.nonce = 0;
      test.receiver.cancel(); test.receiver.begin(0, 0); test.receiver.setDecodeWorkspace(nullptr, 0); // No mode entry ever occurred.
      assert(test.receiver.setDecodeWorkspace(test.decodeWorkspace, sizeof(test.decodeWorkspace)));
      test.line("IDLE", 0);
      [test.host readAvailable];
      test.until([&] { return test.releases == 1; });
      assert(test.host.session == 0 && test.requests == 0 && test.ready == 0);
    }
    puts("Pseudo-terminal transport passed: changed areas, ACK-only baseline, immutable/latest frames, RLE/scale negotiation, resolution changes, desktop benchmarks, retry/exit/cleanup, Wi-Fi provisioning/routing/rotation/failures/batching, trusted wireless startup/USB detach/control teardown, bounded audio and independent ACK/send completion, device volume ownership/validation/configuration races/audio-only timeout, one-second volume overlay/static restoration/moving desktop/all orientations/retries/min-max refresh/cleanup");
  }
}
