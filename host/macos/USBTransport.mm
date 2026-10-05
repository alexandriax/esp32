#import "USBTransport.h"
#import "WiFiChannel.h"
#import "ImageCodec.h"
#import "DevicePairingStore.h"
#import "DeviceDiscovery.h"
#include <arpa/inet.h>
#include "WireProtocol.hpp"
#include "../../firmware/sloth_pet/screen_rotation.h"
#include "../../firmware/sloth_pet/volume_overlay.h"
#import <IOKit/IOKitLib.h>
#import <IOKit/serial/IOSerialKeys.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>

static NSArray<NSString *> *MossPorts(void) {
  NSMutableArray<NSString *> *paths = [NSMutableArray array];
  io_iterator_t iterator = IO_OBJECT_NULL;
  CFMutableDictionaryRef matching = IOServiceMatching(kIOSerialBSDServiceValue);
  if (!matching || IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != KERN_SUCCESS) return paths;
  io_object_t service;
  while ((service = IOIteratorNext(iterator))) {
    const IOOptionBits options = kIORegistryIterateRecursively | kIORegistryIterateParents;
    NSNumber *vendor = CFBridgingRelease(IORegistryEntrySearchCFProperty(service, kIOServicePlane, CFSTR("idVendor"), kCFAllocatorDefault, options));
    NSNumber *product = CFBridgingRelease(IORegistryEntrySearchCFProperty(service, kIOServicePlane, CFSTR("idProduct"), kCFAllocatorDefault, options));
    NSString *path = CFBridgingRelease(IORegistryEntryCreateCFProperty(service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0));
    if ([vendor isKindOfClass:[NSNumber class]] && [product isKindOfClass:[NSNumber class]] &&
        vendor.unsignedIntValue == 0x303A && product.unsignedIntValue == 0x1001 &&
        [path isKindOfClass:[NSString class]] && [path hasPrefix:@"/dev/cu."]) [paths addObject:path];
    IOObjectRelease(service);
  }
  IOObjectRelease(iterator);
  return paths;
}
static BOOL Number(NSString *text, unsigned base, uint64_t maximum, uint64_t *result) {
  const char *start = text.UTF8String;
  if (!start || !*start || *start == '+' || *start == '-') return NO;
  char *end = nullptr;
  errno = 0;
  const unsigned long long value = strtoull(start, &end, base);
  if (errno || !end || *end || value > maximum) return NO;
  *result = value;
  return YES;
}

@implementation MossUSBTransport {
  int _fd;
  MossWiFiChannel *_wifi;
  MossDevicePairingStore *_pairings;
  MossDeviceDiscovery *_discovery;
  NSMutableDictionary<NSString *, MossDiscoveredDevice *> *_discoveredDevices;
  NSMutableDictionary<NSString *, NSNumber *> *_failedPairingNonces;
  NSString *_pairedDeviceID, *_stopReason;
  BOOL _wirelessControl, _networkRequestPending, _audioEnabled, _audioActive, _audioConfigurationDirty, _audioAwaiting;
  NSUInteger _audioVolume, _audioConfigurationVolume;
  BOOL _audioConfigurationAwaiting, _audioConfigurationEnabled;
  NSTimeInterval _audioConfigurationDeadline;
  NSMutableData *_audioPending;
  NSUInteger _audioQueuedSamples;
  NSTimeInterval _audioQueueMeasuredAt, _lastAudioInput, _videoAudioWaitSince;
  NSTimeInterval _stopDeadline, _lastDiscoveryCheck;
  NSData *_networkWritePacket;
  BOOL _wifiRequested, _wifiConfiguring, _networkWriting;
  BOOL _allowsLossyCompression, _needsLosslessRefresh, _refiningLossless, _frameJpeg;
  NSTimeInterval _lastContentChange;
  NSTimeInterval _wifiDeadline;
  NSString *_port, *_lastStatus;
  dispatch_source_t _reader, _timer;
  NSMutableData *_input;
  NSData *_tx, *_latestFrame, *_sendingFrame, *_pendingFrame, *_sourceFrame;
  NSData *_volumeOverlayPixels;
  NSTimeInterval _volumeOverlayDeadline;
  BOOL _replaceComposition;
  size_t _txOffset;
  uint64_t _session, _ignoredSession, _cleanedSession;
  uint32_t _lastSequence, _waitingSequence, _capabilities;
  NSUInteger _transferSize;
  unsigned _rotation;
  moss_wire::DeltaFrame _delta;
  std::vector<moss_wire::Band> _pendingBands;
  moss_wire::Type _waitingType;
  BOOL _running, _bound, _active, _stopping, _awaiting, _ended, _cleaned, _discardFrame;
  unsigned _row, _retries, _framesSent, _changedFramesSent, _frameRects, _frameLosslessPixels;
  size_t _frameWireBytes;
  NSTimeInterval _lastDiscovery, _lastQuery, _lastTransmit, _sentAt, _writeStarted, _frameStarted;
}
- (instancetype)init {
  if ((self = [super init])) { _fd = -1; _input = [NSMutableData data]; _transferSize = 480; _allowsLossyCompression = YES; _audioVolume = 35; _pairings = [[MossDevicePairingStore alloc] init]; _discoveredDevices = [NSMutableDictionary dictionary]; _failedPairingNonces = [NSMutableDictionary dictionary]; }
  return self;
}
- (NSArray<NSString *> *)pairedDeviceIDs { return [_pairings deviceIDs:nil] ?: @[]; }
- (BOOL)forgetPairedDevice:(NSString *)deviceID error:(NSError **)error {
  if ([_pairedDeviceID isEqualToString:deviceID] && _session) [self endSession:@"Pairing forgotten · connect USB to pair again"];
  return [_pairings forgetDevice:deviceID error:error];
}
- (BOOL)audioEnabled { return _audioEnabled; }
- (BOOL)audioActive { return _audioActive; }
- (void)setAudioEnabled:(BOOL)enabled {
  _audioEnabled = enabled; _audioConfigurationDirty = YES;
  _audioActive = _audioAwaiting = NO; [_audioPending setLength:0];
  _audioQueueMeasuredAt = _lastAudioInput = 0;
}
- (NSUInteger)audioVolume { return _audioVolume; }
- (void)setAudioVolume:(NSUInteger)volume {
  _audioVolume = MIN(volume, 60); _audioConfigurationDirty = YES;
  [self showVolumeOverlay];
}
- (void)receivedAudioVolume:(NSUInteger)volume {
  const BOOL changed = _audioVolume != volume;
  _audioVolume = volume;
  [self showVolumeOverlay]; // Another press at min/max also restarts the timer.
  if (changed && self.audioVolumeHandler) self.audioVolumeHandler(volume);
}
- (void)submitAudio:(NSData *)pcm {
  if (!_audioEnabled || !_audioActive || !_active || _stopping || !_wirelessControl || !_wifi.ready ||
      !pcm.length || (pcm.length & 1) || pcm.length > 3200) return;
  // Keep at most 200ms of fresh audio, rather than accumulating latency.
  if (!_audioPending) _audioPending = [NSMutableData data];
  if (_audioPending.length + pcm.length > 6400) {
    const NSUInteger discard = _audioPending.length + pcm.length - 6400;
    [_audioPending replaceBytesInRange:NSMakeRange(0, discard) withBytes:NULL length:0];
  }
  [_audioPending appendData:pcm];
  _lastAudioInput = NSProcessInfo.processInfo.systemUptime;
}
- (BOOL)sendAudioIfNeeded {
  if (!_active || _stopping || !_wirelessControl || !_wifi.ready || _networkWriting || !(_capabilities & moss_wire::Audio)) return NO;
  NSData *payload;
  moss_wire::Type type;
  if (_audioConfigurationDirty && !_audioConfigurationAwaiting) {
    const uint8_t config[] = {1, (uint8_t)_audioEnabled, (uint8_t)_audioVolume, 0};
    payload = [NSData dataWithBytes:config length:sizeof(config)];
    type = moss_wire::AudioConfig; _audioConfigurationDirty = NO;
    // One configuration acknowledgment at a time makes an old reply
    // distinguishable from a newer hardware-button or Mac-menu choice.
    _audioConfigurationAwaiting = YES;
    _audioConfigurationVolume = _audioVolume;
    _audioConfigurationEnabled = _audioEnabled;
    _audioConfigurationDeadline = NSProcessInfo.processInfo.systemUptime + 2;
  } else {
    if (!_audioActive || !_audioEnabled || _audioAwaiting || _audioPending.length < 1600) return NO;
    const NSUInteger length = MIN((NSUInteger)3200, _audioPending.length);
    payload = [_audioPending subdataWithRange:NSMakeRange(0, length)];
    [_audioPending replaceBytesInRange:NSMakeRange(0, length) withBytes:NULL length:0];
    type = moss_wire::AudioPCM;
    _audioAwaiting = YES; // Network send completion only queues bytes; device credit paces consumption.
  }
  const auto wire = moss_wire::packet(type, _session, 0, 0, 0, (const uint8_t *)payload.bytes, payload.length);
  NSData *packet = [NSData dataWithBytes:wire.data() length:wire.size()];
  _networkWriting = YES; _networkWritePacket = packet;
  _writeStarted = NSProcessInfo.processInfo.systemUptime;
  const uint64_t session = _session;
  [_wifi sendPacket:packet completion:^(NSError *error) {
    if (session != self->_session || self->_networkWritePacket != packet) return;
    self->_networkWriting = NO; self->_networkWritePacket = nil;
    if (error) [self failWiFi:error.localizedDescription];
  }];
  return YES;
}
- (void)detachSerial {
  if (_reader) {
    const int old = _fd;
    dispatch_source_set_cancel_handler(_reader, ^{ if (old >= 0) close(old); });
    dispatch_source_cancel(_reader); _reader = nil;
  } else if (_fd >= 0) close(_fd);
  _fd = -1; _port = nil; [_input setLength:0];
}
- (void)serialDisconnected:(NSString *)reason {
  if (_wirelessControl && _wifi.ready) {
    [self detachSerial];
    [self status:@"Connected wirelessly · USB unplugged"];
  } else [self closePort:reason];
}
- (void)discovered:(MossDiscoveredDevice *)device {
  if (!device) return;
  _discoveredDevices[device.deviceID] = device;
  [self tryDiscoveredDevices];
}
- (void)tryDiscoveredDevices {
  if (!_running || _session || _wifi || _stopping || !_discoveredDevices.count) return;
  const NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
  if (now - _lastDiscoveryCheck < 1) return;
  _lastDiscoveryCheck = now;
  NSSet *trusted = [NSSet setWithArray:[self pairedDeviceIDs]]; // Attributes only.
  for (MossDiscoveredDevice *device in _discoveredDevices.allValues) {
    if (![trusted containsObject:device.deviceID] || device.nonce == _ignoredSession) continue;
    if (_failedPairingNonces[device.deviceID].unsignedLongLongValue == device.nonce) continue;
    NSError *error = nil;
    MossDevicePairing *pairing = [_pairings pairingForDevice:device.deviceID error:&error];
    if (!pairing) {
      _failedPairingNonces[device.deviceID] = @(device.nonce);
      [self status:@"Pairing unavailable · unlock Keychain, then choose Remote Display on Moss again"];
      continue;
    }
    [_failedPairingNonces removeObjectForKey:device.deviceID];
    _session = device.nonce; _pairedDeviceID = device.deviceID;
    _wifiRequested = _wifiConfiguring = _wirelessControl = _networkRequestPending = YES;
    _ended = NO; _lastSequence = 0; _row = 0; _framesSent = _changedFramesSent = 0;
    _capabilities = 0; _transferSize = 480;
    _wifiDeadline = NSProcessInfo.processInfo.systemUptime + 20;
    [self connectWiFiHost:device.host port:device.port fingerprint:pairing.fingerprint token:pairing.token];
    break; // One virtual desktop/session at a time.
  }
}
- (void)networkLine:(NSString *)line {
  if (!_wifi.ready || ![line hasPrefix:@"DISPLAY "]) return;
  NSArray<NSString *> *fields = [line componentsSeparatedByString:@" "];
  // Pairing credentials and endpoint selection are trusted only from USB.
  if (fields.count < 3 || ![@[@"WIFI_REQUEST", @"CAPS", @"ROTATION", @"VOLUME", @"READY", @"ACK", @"STOP", @"RELEASED", @"NACK", @"PERF", @"AUDIO", @"AUDIO_ACK", @"AUDIO_BUFFER", @"AUDIO_PERF"] containsObject:fields[1]]) return;
  uint64_t nonce = 0;
  if (fields[2].length != 16 || !Number(fields[2], 16, UINT64_MAX, &nonce) || nonce != _session) return;
  [self processLine:line network:YES];
}
- (MossWiFiChannel *)newWiFiChannel { return [[MossWiFiChannel alloc] init]; }
- (void)connectWiFiHost:(NSString *)host port:(NSString *)port fingerprint:(NSData *)fingerprint token:(NSString *)token {
  const uint64_t nonce = _session;
  _wifi = [self newWiFiChannel];
  __weak MossUSBTransport *weakSelf = self;
  _wifi.lineHandler = ^(NSString *line) { [weakSelf networkLine:line]; };
  _wifi.failureHandler = ^(NSString *reason) { [weakSelf failWiFi:reason]; };
  [self status:@"Connecting paired Moss over Wi-Fi…"];
  [_wifi connectHost:host port:port fingerprint:fingerprint token:token completion:^(NSError *error) {
    MossUSBTransport *self = weakSelf;
    if (!self || nonce != self->_session || self->_stopping) return;
    if (error) { [self failWiFi:error.localizedDescription]; return; }
    self->_wifiDeadline = self->_networkRequestPending ? NSProcessInfo.processInfo.systemUptime + 5 : 0;
    if (self->_capabilities & moss_wire::WirelessControl) self->_wirelessControl = YES;
    if (!self->_networkRequestPending) [self acceptRequest:nonce];
  }];
}

- (uint64_t)session { return _session; }
- (BOOL)allowsLossyCompression { return _allowsLossyCompression; }
- (void)setAllowsLossyCompression:(BOOL)enabled {
  NSAssert([NSThread isMainThread], @"Compression configuration must run on the main thread");
  _allowsLossyCompression = enabled;
  // An in-flight JPEG is retired normally. Its ACK marks a full lossless
  // refinement as needed; disabling compression makes that refresh immediate.
}
- (BOOL)wifiRequested { return _wifiRequested; }
- (BOOL)wifiConnected { return _wifi.ready; }
- (uint32_t)capabilities { return _capabilities; }
- (NSUInteger)transferSize { return _transferSize; }
- (void)invalidateFrameBaseline { _delta.invalidate(); }
- (void)setTransferSize:(NSUInteger)size {
  NSAssert([NSThread isMainThread], @"USB configuration must run on the main thread");
  if ((size != 240 && size != 480) || (size == 240 && !(_capabilities & moss_wire::Scale2)) || size == _transferSize) return;
  _transferSize = size;
  _latestFrame = _sourceFrame = nil;
  [self invalidateFrameBaseline];
  if (_pendingFrame && _awaiting) _discardFrame = YES;
  else { _sendingFrame = nil; _row = 0; _discardFrame = NO; }
}
- (void)status:(NSString *)status {
  if ([_lastStatus isEqualToString:status]) return;
  _lastStatus = [status copy];
  if (self.statusHandler) self.statusHandler(status);
}
- (void)start {
  NSAssert([NSThread isMainThread], @"USB lifecycle must run on the main thread");
  if (_running) return;
  _running = YES;
  _discovery = [[MossDeviceDiscovery alloc] init];
  __weak MossUSBTransport *discoverySelf = self;
  _discovery.foundHandler = ^(MossDiscoveredDevice *device) { [discoverySelf discovered:device]; };
  _discovery.removedHandler = ^(MossDiscoveredDevice *device) {
    MossUSBTransport *self = discoverySelf;
    if (self && self->_discoveredDevices[device.deviceID].nonce == device.nonce)
      [self->_discoveredDevices removeObjectForKey:device.deviceID];
  };
  [_discovery start];
  _timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_timer(_timer, DISPATCH_TIME_NOW, 5 * NSEC_PER_MSEC, NSEC_PER_MSEC);
  __weak MossUSBTransport *weakSelf = self;
  dispatch_source_set_event_handler(_timer, ^{ [weakSelf tick]; });
  dispatch_resume(_timer);
}
- (void)stop {
  _running = NO;
  [_discovery stop]; _discovery = nil;
  if (_timer) { dispatch_source_cancel(_timer); _timer = nil; }
  [self closePort:@"Moss Display stopped"];
}
- (void)finishVisualSession:(NSString *)reason {
  [_wifi stop]; _wifi = nil;
  _wifiRequested = _wifiConfiguring = _networkWriting = NO;
  _networkWritePacket = nil;
  _wifiDeadline = _stopDeadline = 0;
  _wirelessControl = _networkRequestPending = NO;
  _audioActive = _audioAwaiting = _audioConfigurationAwaiting = NO;
  _audioConfigurationDeadline = 0;
  _audioConfigurationDirty = YES; [_audioPending setLength:0];
  _pairedDeviceID = nil;
  _latestFrame = _sendingFrame = _pendingFrame = _sourceFrame = nil;
  _volumeOverlayPixels = nil; _volumeOverlayDeadline = 0; _replaceComposition = NO;
  _pendingBands.clear();
  _needsLosslessRefresh = _refiningLossless = _frameJpeg = NO;
  _lastContentChange = 0;
  _rotation = 0;
  _delta.resize(0);
  _discardFrame = NO;
  const BOOL hadCapabilities = _capabilities != 0;
  _capabilities = 0; _transferSize = 480;
  if (hadCapabilities && self.capabilitiesHandler) self.capabilitiesHandler(0);
  if (!_ended && _session) { _ended = YES; if (self.endedHandler) self.endedHandler(reason); }
}
- (void)closePort:(NSString *)reason {
  [self finishVisualSession:reason];
  if (_session) _ignoredSession = _session;
  if (_reader) {
    const int old = _fd;
    dispatch_source_set_cancel_handler(_reader, ^{ if (old >= 0) close(old); });
    dispatch_source_cancel(_reader);
    _reader = nil;
  } else if (_fd >= 0) close(_fd);
  _fd = -1;
  _port = nil;
  _tx = nil;
  _txOffset = 0;
  [_input setLength:0];
  _session = 0;
  _bound = _active = _stopping = _awaiting = NO;
  [self status:reason];
}
- (void)openPort:(NSString *)path {
  const int fd = open(path.fileSystemRepresentation, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) { [self status:@"USB is busy · close the serial monitor"]; return; }
  if (ioctl(fd, TIOCEXCL) < 0) { close(fd); [self status:@"USB is already in use"]; return; }
  struct termios options;
  if (tcgetattr(fd, &options) < 0) { close(fd); return; }
  cfmakeraw(&options);
  cfsetispeed(&options, B115200);
  cfsetospeed(&options, B115200);
  options.c_cflag |= CLOCAL | CREAD | CS8;
  options.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS | HUPCL);
  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 0;
  if (tcsetattr(fd, TCSANOW, &options) < 0) { close(fd); return; }
  _fd = fd;
  _port = [path copy];
  _cleaned = NO;
  _lastQuery = 0;
  _reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t)fd, 0, dispatch_get_main_queue());
  __weak MossUSBTransport *weakSelf = self;
  dispatch_source_set_event_handler(_reader, ^{ [weakSelf readAvailable]; });
  dispatch_resume(_reader);
  [self status:@"USB connected · choose Remote Display in the Moss menu"];
}
- (void)queuePacket:(moss_wire::Type)type sequence:(uint32_t)sequence y:(unsigned)y
             height:(unsigned)height payload:(const uint8_t *)payload length:(size_t)length awaitReply:(BOOL)reply {
  const auto bytes = moss_wire::packet(type, _session, sequence, y, height, payload, length);
  _tx = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  _txOffset = 0;
  _awaiting = reply;
  _waitingType = type;
  _waitingSequence = sequence;
  _retries = 0;
  _sentAt = 0;
  _writeStarted = [NSProcessInfo processInfo].systemUptime;
  _bound = YES;
  [self writeAvailable];
}
- (void)control:(moss_wire::Type)type sequence:(uint32_t)sequence {
  [self queuePacket:type sequence:sequence y:0 height:0 payload:nullptr length:0 awaitReply:YES];
}
- (void)writeAvailable {
  if ((!_wirelessControl && _fd < 0) || !_tx || _txOffset == _tx.length) return;
  if (_wirelessControl || (_wifiRequested && moss_wire::rectangleType(_waitingType))) {
    if (_networkWriting) return;
    if (!_wifi.ready) { [self failWiFi:@"Wi-Fi is unavailable · choose Remote Display again"]; return; }
    _networkWriting = YES;
    NSData *packet = _tx;
    _networkWritePacket = packet;
    const uint64_t session = _session;
    const BOOL rectangle = moss_wire::rectangleType(_waitingType);
    const BOOL expectsReply = _awaiting;
    _txOffset = packet.length;
    [_wifi sendPacket:packet completion:^(NSError *error) {
      if (session != self->_session || self->_networkWritePacket != packet) return;
      self->_networkWriting = NO;
      self->_networkWritePacket = nil;
      if (error) { [self failWiFi:error.localizedDescription]; return; }
      self->_sentAt = self->_lastTransmit = [NSProcessInfo processInfo].systemUptime;
      if (rectangle) self->_frameWireBytes += packet.length;
      if (!expectsReply && self->_tx == packet) { self->_tx = nil; self->_txOffset = 0; }
    }];
    return;
  }
  const uint8_t *bytes = static_cast<const uint8_t *>(_tx.bytes);
  while (_txOffset < _tx.length) {
    const ssize_t count = write(_fd, bytes + _txOffset, _tx.length - _txOffset);
    if (count > 0) {
      _txOffset += (size_t)count;
      if (moss_wire::rectangleType(_waitingType)) _frameWireBytes += (size_t)count;
    }
    else if (count < 0 && errno == EINTR) continue;
    else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
    else { [self serialDisconnected:@"USB disconnected · reconnect and choose Remote Display again"]; return; }
  }
  if (_txOffset == _tx.length) {
    _sentAt = _lastTransmit = [NSProcessInfo processInfo].systemUptime;
    if (!_awaiting) { _tx = nil; _txOffset = 0; }
  }
}
- (void)acceptRequest:(uint64_t)session {
  if ((_fd < 0 && !_wirelessControl) || !_session || _session != session || _active || _stopping || _awaiting || _networkRequestPending) return;
  if (_wifiRequested && !_wifi.ready) {
    if (!_wifiConfiguring && self.wifiSetupHandler) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (session == self->_session && !self->_wifiConfiguring && !self->_wifi.ready && !self->_stopping && self.wifiSetupHandler)
          self.wifiSetupHandler();
      });
    }
    return;
  }
  const uint8_t hello[] = {0xE0, 0x01, 0xE0, 0x01, 0x00, 0x3C, 0x01, 0x00};
  [self queuePacket:moss_wire::Hello sequence:0 y:0 height:0 payload:hello length:sizeof(hello) awaitReply:YES];
}
- (void)failWiFi:(NSString *)reason {
  if (_wirelessControl) {
    const NSString *finalReason = _stopReason ?: reason;
    _ignoredSession = _session;
    [self finishVisualSession:(NSString *)finalReason];
    _session = 0; _active = _awaiting = _stopping = _bound = NO;
    _tx = nil; _txOffset = 0; _stopReason = nil;
    [self status:(NSString *)finalReason];
    return;
  }
  [self finishVisualSession:reason];
  _ignoredSession = _session;
  _active = _awaiting = NO; _stopping = YES;
  _tx = nil; _txOffset = 0;
  if (_fd >= 0 && _session) [self control:moss_wire::Release sequence:0];
  [self status:reason];
}
- (void)configureWiFiSSID:(NSString *)ssid password:(NSString *)password {
  if (!_wifiRequested || !_session || _stopping || _active || _wifiConfiguring) return;
  NSData *name = [ssid dataUsingEncoding:NSUTF8StringEncoding];
  NSData *secret = [password dataUsingEncoding:NSUTF8StringEncoding];
  if (!name.length || name.length > 32 || secret.length > 63 || (secret.length && secret.length < 8) ||
      memchr(name.bytes, 0, name.length) || (secret.length && memchr(secret.bytes, 0, secret.length))) {
    [self failWiFi:@"Wi-Fi name/password length is invalid"]; return;
  }
  uint8_t credentials[97] = {(uint8_t)name.length, (uint8_t)secret.length};
  memcpy(credentials + 2, name.bytes, name.length);
  if (secret.length) memcpy(credentials + 2 + name.length, secret.bytes, secret.length);
  _wifiConfiguring = YES;
  _wifiDeadline = [NSProcessInfo processInfo].systemUptime + 45;
  [self status:@"Connecting Moss to Wi-Fi…"];
  [self queuePacket:moss_wire::ConfigureWiFi sequence:0 y:0 height:0 payload:credentials
             length:2 + name.length + secret.length awaitReply:NO];
  volatile uint8_t *wipe = credentials;
  for (unsigned i = 0; i < sizeof(credentials); ++i) wipe[i] = 0;
}
- (void)reportHostStatus:(uint8_t)status {
  if ((_fd < 0 && !_wirelessControl) || !_session || _awaiting || _tx) return;
  [self queuePacket:moss_wire::HostStatus sequence:0 y:0 height:0 payload:&status length:1 awaitReply:NO];
}
- (void)submitFrame:(NSData *)pixels {
  if (!_active || _stopping || pixels.length != moss_wire::bytesForSize((unsigned)_transferSize)) return;
  if (!_sourceFrame || ![pixels isEqualToData:_sourceFrame])
    _lastContentChange = [NSProcessInfo processInfo].systemUptime;
  _sourceFrame = [pixels copy]; // Retain one unrotated snapshot even if capture goes idle.
  [self composeLatestFrame];
}
- (void)composeLatestFrame {
  if (!_sourceFrame) return;
  NSData *composed = _sourceFrame;
  const unsigned size = (unsigned)_transferSize;
  if (_volumeOverlayPixels) {
    NSMutableData *withOverlay = [_sourceFrame mutableCopy];
    const auto *overlay = static_cast<const uint16_t *>(_volumeOverlayPixels.bytes);
    auto *destination = static_cast<uint16_t *>(withOverlay.mutableBytes);
    const unsigned scale = size / sloth::graphics::kSize;
    for (unsigned y = sloth::kVolumeOverlayY * scale; y < (sloth::kVolumeOverlayY + sloth::kVolumeOverlayHeight) * scale; ++y)
      for (unsigned x = sloth::kVolumeOverlayX * scale; x < (sloth::kVolumeOverlayX + sloth::kVolumeOverlayWidth) * scale; ++x)
        destination[y * size + x] = overlay[(y / scale) * sloth::graphics::kSize + x / scale];
    composed = withOverlay;
  }
  // Compose before rotation: text stays aligned with the desktop's orientation.
  if (!_rotation) { _latestFrame = composed; return; }
  NSMutableData *rotated = [NSMutableData dataWithLength:composed.length];
  const auto *source = static_cast<const uint16_t *>(composed.bytes);
  auto *destination = static_cast<uint16_t *>(rotated.mutableBytes);
  for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x)
    destination[y * size + x] = source[sloth::rotatedSource(x, y, size, _rotation)];
  _latestFrame = rotated;
}
- (void)refreshVolumeComposition {
  [self composeLatestFrame];
  // Preserve the immutable packet already on the wire. After its ACK, start
  // from the newest composition and retain the valid acknowledged baseline.
  // This avoids making a button wait for every strip of an old USB frame.
  if (_pendingFrame && _awaiting) _replaceComposition = YES;
  else { _sendingFrame = nil; _row = 0; _refiningLossless = NO; }
}
- (void)showVolumeOverlay {
  if (!_session || _stopping || _ended) return;
  NSMutableData *overlay = [NSMutableData dataWithLength:sloth::graphics::kSize * sloth::graphics::kSize * sizeof(uint16_t)];
  sloth::drawVolumeOverlay(static_cast<uint16_t *>(overlay.mutableBytes), (uint8_t)_audioVolume);
  _volumeOverlayPixels = overlay;
  _volumeOverlayDeadline = NSProcessInfo.processInfo.systemUptime + 1;
  [self refreshVolumeComposition];
}
- (void)endSession:(NSString *)reason {
  if (!_session || (_fd < 0 && !_wirelessControl)) { [self status:reason]; return; }
  if (_wirelessControl && _wifi.ready) {
    _stopReason = [reason copy]; _ignoredSession = _session; _stopping = YES;
    _stopDeadline = NSProcessInfo.processInfo.systemUptime + 2;
    _tx = nil; _awaiting = NO; _txOffset = 0;
    _latestFrame = _sendingFrame = _pendingFrame = _sourceFrame = nil;
    _volumeOverlayPixels = nil; _volumeOverlayDeadline = 0; _replaceComposition = NO;
    _pendingBands.clear(); [_audioPending setLength:0]; _audioActive = _audioAwaiting = NO;
    if (!_ended) { _ended = YES; if (self.endedHandler) self.endedHandler(reason); }
    [self status:reason];
    return; // Tick sends sequence-independent RELEASE after any current write.
  }
  // Wi-Fi teardown must discard pending pixel batches before closing TLS. If
  // left queued after clearing wifiRequested, a retry could send them over USB.
  // RELEASE is sequence-independent and works during provisioning as well.
  if (_wifiRequested) { [self failWiFi:reason]; return; }
  [self finishVisualSession:reason];
  _ignoredSession = _session;
  _stopping = YES;
  if (!_awaiting && !_tx) [self control:moss_wire::Stop sequence:_active ? _lastSequence + 1 : 0];
  [self status:reason];
}
- (void)readAvailable {
  uint8_t bytes[4096];
  for (;;) {
    const ssize_t count = read(_fd, bytes, sizeof(bytes));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (count < 0) { [self serialDisconnected:@"USB disconnected · reconnect and choose Remote Display again"]; return; }
    if (!count) return;
    for (ssize_t i = 0; i < count; ++i) {
      if (bytes[i] == '\n') {
        NSString *line = [[NSString alloc] initWithData:_input encoding:NSASCIIStringEncoding];
        [_input setLength:0];
        if (line) [self line:[line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]];
        if (_fd < 0) return;
      } else if (bytes[i] >= 32 && bytes[i] < 127) {
        if (_input.length < 1024) [_input appendBytes:bytes + i length:1];
        else [_input setLength:0];
      }
    }
  }
}
- (void)logPerformance:(NSArray<NSNumber *> *)values {
  // Fixed labels and normalized numbers only; never echo untrusted serial text.
  NSLog(@"DISPLAY PERF session=%016llx elapsed_ms=%u rectangles=%u wire_bytes=%u feed_us=%u panel_us=%u ack_us=%u tls_us=%u tls_calls=%u socket_bytes=%u socket_again=%u queue_full=%u queue_high=%u rssi=%d heap=%u",
        (unsigned long long)_session, values[0].unsignedIntValue, values[1].unsignedIntValue,
        values[2].unsignedIntValue, values[3].unsignedIntValue, values[4].unsignedIntValue,
        values[5].unsignedIntValue, values[6].unsignedIntValue, values[7].unsignedIntValue,
        values[8].unsignedIntValue, values[9].unsignedIntValue, values[10].unsignedIntValue,
        values[11].unsignedIntValue, values[12].intValue, values[13].unsignedIntValue);
}
- (void)logAudioPerformance:(NSArray<NSNumber *> *)values {
  NSLog(@"DISPLAY AUDIO PERF queued=%u underruns=%u overflows=%u dropped=%u errors=%u heap=%u minimum_heap=%u received_samples=%u rendered_samples=%u",
    values[0].unsignedIntValue, values[1].unsignedIntValue, values[2].unsignedIntValue,
    values[3].unsignedIntValue, values[4].unsignedIntValue, values[5].unsignedIntValue,
    values[6].unsignedIntValue, values[7].unsignedIntValue, values[8].unsignedIntValue);
}
- (void)line:(NSString *)line {
  if (_wirelessControl && _wifi.ready) return; // USB cannot interleave an authenticated network session.
  [self processLine:line network:NO];
}
- (void)processLine:(NSString *)line network:(BOOL)network {
  if (![line hasPrefix:@"DISPLAY "]) return;
  NSArray<NSString *> *fields = [line componentsSeparatedByString:@" "];
  if (fields.count < 3 || fields[2].length != 16) return;
  uint64_t nonce = 0;
  if (!Number(fields[2], 16, UINT64_MAX, &nonce)) return;
  NSString *kind = fields[1];
  if ([kind isEqualToString:@"VOLUME"]) {
    uint64_t volume = 0;
    if (fields.count != 4 || !nonce || nonce != _session || _stopping || _ended ||
        (network ? (!_wirelessControl || !_wifi.ready || _networkRequestPending) : (_wirelessControl || _fd < 0)) ||
        [fields[2] rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"] invertedSet]].location != NSNotFound ||
        [fields[3] rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789"] invertedSet]].location != NSNotFound ||
        !Number(fields[3], 10, 60, &volume)) return;
    [self receivedAudioVolume:(NSUInteger)volume];
    // The device applied this button press already. Only resend if a prior
    // host configuration still in flight might overwrite the new choice.
    if (_audioConfigurationAwaiting && _audioConfigurationVolume != _audioVolume)
      _audioConfigurationDirty = YES;
    return;
  }
  if ([kind isEqualToString:@"AUDIO_BUFFER"]) {
    uint64_t samples=0;
    if (network && fields.count==4 && nonce==_session && _active && !_stopping &&
        _audioEnabled && _audioActive && Number(fields[3],10,6144,&samples)) {
      _audioQueuedSamples=(NSUInteger)samples;
      _audioQueueMeasuredAt=NSProcessInfo.processInfo.systemUptime;
    }
    return;
  }
  if ([kind isEqualToString:@"AUDIO_ACK"]) {
    if (network && fields.count == 3 && nonce == _session && _active && !_stopping && _audioEnabled && _audioActive)
      _audioAwaiting = NO;
    return;
  }
  if ([kind isEqualToString:@"AUDIO_PERF"]) {
    if (!network || nonce != _session || !_active || fields.count != 12) return;
    NSMutableArray<NSNumber *> *values = [NSMutableArray arrayWithCapacity:9];
    for (unsigned i = 0; i < 9; ++i) {
      uint64_t value = 0;
      if (!Number(fields[i+3], 10, UINT32_MAX, &value)) return;
      [values addObject:@((uint32_t)value)];
    }
    [self logAudioPerformance:values];
    return;
  }
  if ([kind isEqualToString:@"AUDIO"]) {
    uint64_t active = 0, volume = 0, error = 0;
    if (!network || !_wirelessControl || nonce != _session || !_active || _stopping || fields.count != 6 ||
        !Number(fields[3],10,1,&active) || !Number(fields[4],10,60,&volume) || !Number(fields[5],10,1,&error)) return;
    // AUDIO is an acknowledgment, not a volume preference. Only VOLUME can
    // change the desired gain, so an older config reply cannot roll it back.
    // Button gain failures also send AUDIO; a differing gain/enable reply
    // cannot acknowledge a newer configuration still in flight.
    if (_audioConfigurationAwaiting && volume == _audioConfigurationVolume &&
        (error || (BOOL)active == _audioConfigurationEnabled)) {
      _audioConfigurationAwaiting = NO; _audioConfigurationDeadline = 0;
    }
    _audioActive = active && _audioEnabled && !error;
    if (!_audioActive) { _audioAwaiting = NO; [_audioPending setLength:0]; }
    if (self.audioStatusHandler) self.audioStatusHandler(_audioActive, error ? @"Moss audio could not start" : (_audioActive ? @"Audio on" : @"Audio off"));
    return;
  }
  if ([kind isEqualToString:@"WIFI_SAVED"]) {
    if (!network && fields.count == 4 && [fields[3] isEqualToString:@"1"] && nonce == _session && _wifiRequested && !_active) {
      _wifiConfiguring = YES; _wifiDeadline = NSProcessInfo.processInfo.systemUptime + 45;
      [self status:@"Moss is joining its saved Wi-Fi network…"];
    }
    return;
  }
  if ([kind isEqualToString:@"PAIRING"]) {
    if (network || fields.count != 6 || nonce != _session || !_wifiRequested || _active || _stopping || !MossDeviceIDValid(fields[3]) || fields[4].length != 64 || fields[5].length != 64) return;
    NSMutableData *fingerprint = [NSMutableData dataWithLength:32];
    for (NSUInteger i = 0; i < 32; ++i) {
      uint64_t byte = 0, tokenByte = 0;
      if (!Number([fields[4] substringWithRange:NSMakeRange(i*2,2)],16,255,&byte) || !Number([fields[5] substringWithRange:NSMakeRange(i*2,2)],16,255,&tokenByte)) return;
      ((uint8_t *)fingerprint.mutableBytes)[i] = (uint8_t)byte;
    }
    NSError *error = nil;
    if (![_pairings saveDevice:fields[3] fingerprint:fingerprint token:[fields[5] lowercaseString] error:&error]) {
      [self status:@"Connected setup received, but Keychain pairing could not be saved"];
    } else { _pairedDeviceID = fields[3]; [_failedPairingNonces removeObjectForKey:fields[3]]; }
    return; // Never log or accept pairing material from network advertisements.
  }
  if ([kind isEqualToString:@"PERF"]) {
    if (fields.count != 17 || !nonce || nonce != _session || !_active || _stopping ||
        !_wifiRequested || !_wifi.ready) return;
    NSMutableArray<NSNumber *> *values = [NSMutableArray arrayWithCapacity:14];
    NSCharacterSet *nonDecimal = [[NSCharacterSet characterSetWithCharactersInString:@"0123456789"] invertedSet];
    for (unsigned i = 0; i < 14; ++i) {
      NSString *number = fields[i + 3];
      const BOOL rssi = i == 12;
      const BOOL negative = rssi && [number hasPrefix:@"-"];
      if (negative) number = [number substringFromIndex:1];
      uint64_t value = 0;
      if (!number.length || [number rangeOfCharacterFromSet:nonDecimal].location != NSNotFound ||
          !Number(number, 10, rssi ? 127 : UINT32_MAX, &value) || (rssi && !negative && value)) return;
      [values addObject:rssi ? @(-(int32_t)value) : @((uint32_t)value)];
    }
    [self logPerformance:values];
    return;
  }
  if ([kind isEqualToString:@"WIFI_READY"]) {
    if (!_wifiRequested || !_wifiConfiguring || _wifi || _stopping || nonce != _session || fields.count != 7) return;
    uint64_t port = 0;
    struct in_addr address;
    if (!Number(fields[4], 10, 65535, &port) || !port || inet_pton(AF_INET, fields[3].UTF8String, &address) != 1 ||
        fields[5].length != 64 || fields[6].length != 64) { [self failWiFi:@"Invalid USB Wi-Fi pairing response"]; return; }
    NSMutableData *fingerprint = [NSMutableData dataWithLength:32];
    uint8_t *bytes = static_cast<uint8_t *>(fingerprint.mutableBytes);
    for (unsigned i = 0; i < 32; ++i) {
      uint64_t value = 0, tokenByte = 0;
      if (!Number([fields[5] substringWithRange:NSMakeRange(i * 2, 2)], 16, 255, &value) ||
          !Number([fields[6] substringWithRange:NSMakeRange(i * 2, 2)], 16, 255, &tokenByte)) {
        [self failWiFi:@"Invalid USB Wi-Fi pairing response"]; return;
      }
      bytes[i] = (uint8_t)value;
    }
    [self connectWiFiHost:fields[3] port:fields[4] fingerprint:fingerprint token:fields[6]];
    return;
  }
  if ([kind isEqualToString:@"WIFI_ERROR"] && nonce == _session && _wifiRequested) {
    [self failWiFi:@"Moss could not connect to Wi-Fi · check name/password and a 2.4 GHz network"]; return;
  }
  if ([kind isEqualToString:@"ROTATION"]) {
    uint64_t turns = 0;
    if (fields.count != 4 || !nonce || nonce != _session || _stopping ||
        !Number(fields[3], 10, 3, &turns) || turns == _rotation) return;
    _rotation = (unsigned)turns;
    _lastContentChange = [NSProcessInfo processInfo].systemUptime;
    [self invalidateFrameBaseline];
    if (_pendingFrame && _awaiting) _discardFrame = YES;
    else { _sendingFrame = nil; _row = 0; }
    if (_sourceFrame) [self submitFrame:_sourceFrame];
    NSLog(@"USB ROTATION session=%016llx clockwise=%u", (unsigned long long)_session, _rotation * 90);
    return;
  }
  if ([kind isEqualToString:@"CAPS"]) {
    uint64_t flags = 0;
    if (fields.count != 4 || !nonce || nonce != _session || _stopping ||
        !Number(fields[3], 10, UINT32_MAX, &flags)) return;
    const uint32_t capabilities = (uint32_t)flags & (moss_wire::Rle | moss_wire::Scale2 | moss_wire::Lz4 | moss_wire::CropRects | moss_wire::Jpeg240 | moss_wire::WirelessControl | moss_wire::Audio);
    if (_capabilities != capabilities) {
      _capabilities = capabilities;
      if (_wifiRequested && _wifi.ready && (capabilities & moss_wire::WirelessControl)) _wirelessControl = YES;
      if (!(_capabilities & moss_wire::Scale2)) self.transferSize = 480;
      NSLog(@"USB CAPS session=%016llx flags=%u", (unsigned long long)_session, _capabilities);
      if (self.capabilitiesHandler) self.capabilitiesHandler(_capabilities);
    }
    return;
  }
  if ([kind isEqualToString:@"NACK"]) {
    uint64_t sequence = 0, code = 0;
    if (fields.count == 5 && nonce == _session && Number(fields[3],10,UINT32_MAX,&sequence) && Number(fields[4],10,15,&code))
      NSLog(@"Display NACK session=%016llx sequence=%u error=%u", (unsigned long long)nonce, (unsigned)sequence, (unsigned)code);
    return;
  }
  if ([kind isEqualToString:@"IDLE"] && fields.count == 4) {
    uint64_t sequence = 0;
    if (!Number(fields[3], 10, UINT32_MAX, &sequence) || (_cleaned && nonce == _cleanedSession) ||
        (_awaiting && _waitingType == moss_wire::Release)) return;
    if (_session && nonce != _session) return;
    // A capability beacon from new firmware makes binary cleanup safe, even
    // after an earlier host crashed with the receiver still quarantined. IDLE
    // never starts a display or capture, and cleanup runs only once per nonce.
    [self finishVisualSession:@"Moss is back in pet mode"];
    _session = nonce; _ignoredSession = nonce; _ended = YES;
    _active = NO; _stopping = YES;
    _tx = nil; _awaiting = NO;
    tcflush(_fd, TCOFLUSH);
    [self control:moss_wire::Release sequence:(uint32_t)sequence + 1];
    return;
  }
  if ([kind isEqualToString:@"REQUEST"] || [kind isEqualToString:@"WIFI_REQUEST"]) {
    if (network && _networkRequestPending && nonce == _session && fields.count == 6 &&
        [fields[3] isEqualToString:@"480"] && [fields[4] isEqualToString:@"480"] && [fields[5] isEqualToString:@"15360"]) {
      _networkRequestPending = NO; _wifiDeadline = 0;
      [self status:@"Paired Moss requested a wireless desktop"];
      if (self.requestHandler) self.requestHandler(nonce);
      return;
    }
    if (fields.count != 6 || ![fields[3] isEqualToString:@"480"] || ![fields[4] isEqualToString:@"480"] ||
        ![fields[5] isEqualToString:@"15360"] || !nonce || nonce == _ignoredSession || nonce == _session) return;
    [self finishVisualSession:@"Moss requested a new display session"];
    _tx = nil; _txOffset = 0; _awaiting = _active = _stopping = NO;
    _session = nonce; _lastSequence = 0; _ended = NO; _row = 0; _framesSent = _changedFramesSent = 0;
    _capabilities = 0; _transferSize = 480;
    _wifiRequested = [kind isEqualToString:@"WIFI_REQUEST"];
    [self status:@"Moss requested an extended desktop"];
    if (self.requestHandler) self.requestHandler(nonce);
    return;
  }
  if (fields.count != 4 || nonce != _session) return;
  uint64_t sequence = 0;
  if (!Number(fields[3], 10, UINT32_MAX, &sequence)) return;
  if (network && ([kind isEqualToString:@"STOP"] || [kind isEqualToString:@"RELEASED"])) {
    [self failWiFi:_stopReason ?: @"Display ended · choose Remote Display on Moss to reconnect"];
    return;
  }
  if ([kind isEqualToString:@"STOP"]) {
    NSLog(@"USB STOP session=%016llx sequence=%u", (unsigned long long)nonce, (unsigned)sequence);
    [self finishVisualSession:@"Display ended · choose Remote Display on Moss to reconnect"];
    _ignoredSession = _session;
    _active = NO; _stopping = YES;
    _lastSequence = (uint32_t)sequence;
    // Discard queued writes before the fresh leading delimiter in RELEASE.
    // Already-transmitted bytes remain quarantined by the receiver.
    tcflush(_fd, TCOFLUSH);
    _tx = nil; _awaiting = NO;
    [self control:moss_wire::Release sequence:_lastSequence + 1];
    return;
  }
  if ([kind isEqualToString:@"RELEASED"]) {
    NSLog(@"USB RELEASED session=%016llx sequence=%u; virtual-display session cleaned up", (unsigned long long)nonce, (unsigned)sequence);
    _cleaned = YES; _cleanedSession = nonce;
    _session = 0; _active = _stopping = _awaiting = _bound = NO;
    _tx = nil; _txOffset = 0;
    return;
  }
  if (!_awaiting || sequence != _waitingSequence) return;
  const BOOL hello = _waitingType == moss_wire::Hello && [kind isEqualToString:@"READY"] && !sequence;
  const BOOL ack = (moss_wire::rectangleType(_waitingType) || _waitingType == moss_wire::Ping) && [kind isEqualToString:@"ACK"];
  if (!hello && !ack) return;
  const moss_wire::Type accepted = _waitingType;
  _awaiting = NO; _tx = nil; _txOffset = 0;
  _lastSequence = (uint32_t)sequence;
  if (hello) {
    NSLog(@"USB READY session=%016llx", (unsigned long long)_session);
    _active = YES; _audioConfigurationDirty = YES;
    if (!_stopping && self.readyHandler) self.readyHandler(_session);
  } else if (moss_wire::rectangleType(accepted)) {
    if (_pendingFrame) {
      // The receiver accepts only contiguous sequences. A final batch ACK
      // therefore proves every preceding strip was accepted, including after retry.
      if (accepted == moss_wire::ScaledJpegFrame) {
        // The displayed JPEG is approximate. Track its original snapshot for
        // motion deltas, then force an exact full-frame refresh when motion stops.
        for (unsigned y = 0; y < 240; y += 8) {
          moss_wire::Band band; band.y = y; band.height = 8; band.width = 240;
          _delta.acknowledge(static_cast<const uint8_t *>(_pendingFrame.bytes), _pendingFrame.length, band);
        }
        _row = 240; ++_frameRects; _needsLosslessRefresh = YES;
      } else {
        for (const auto &band : _pendingBands) {
          _delta.acknowledge(static_cast<const uint8_t *>(_pendingFrame.bytes), _pendingFrame.length, band);
          _row = band.y + band.height;
          ++_frameRects; _frameLosslessPixels += band.width * band.height;
        }
      }
    }
    _pendingFrame = nil;
    _pendingBands.clear();
    if (_discardFrame || _replaceComposition) {
      if (_discardFrame) [self invalidateFrameBaseline]; // Old-orientation rows cannot mark new rows clean.
      _sendingFrame = nil; _row = 0; _discardFrame = _replaceComposition = NO; _refiningLossless = NO;
    }
  }
  if (_stopping) [self control:moss_wire::Stop sequence:_active ? _lastSequence + 1 : 0];
}
- (void)tick {
  if (!_running) return;
  const NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
  if (_volumeOverlayDeadline && now >= _volumeOverlayDeadline) {
    _volumeOverlayPixels = nil; _volumeOverlayDeadline = 0;
    [self refreshVolumeComposition]; // Erase it even when capture emits no new frames.
  }
  if (now - _lastDiscovery >= 2) {
    _lastDiscovery = now;
    NSArray<NSString *> *ports = MossPorts();
    if (_fd >= 0 && ![ports containsObject:_port]) [self serialDisconnected:@"USB unplugged · waiting for Moss"];
    if (_fd < 0 && !_session) {
      if (ports.count == 1) [self openPort:ports[0]];
      else [self status:ports.count ? @"Multiple ESP32 devices · unplug extras" : @"Waiting for Moss USB"];
    }
  }
  [self tryDiscoveredDevices];
  if (_fd < 0 && !_wirelessControl) return;
  if (_stopDeadline && now >= _stopDeadline) { [self failWiFi:_stopReason ?: @"Display disconnected"]; return; }
  if (_wifiDeadline && now >= _wifiDeadline) { [self failWiFi:@"Wi-Fi setup timed out · choose Remote Display again"]; return; }
  if (_audioConfigurationAwaiting && !_stopping && now >= _audioConfigurationDeadline) {
    const BOOL wasEnabled = _audioEnabled || _audioConfigurationEnabled;
    _audioConfigurationAwaiting = NO; _audioConfigurationDeadline = 0;
    _audioEnabled = _audioActive = _audioAwaiting = NO; [_audioPending setLength:0];
    _audioConfigurationDirty = wasEnabled; // One best-effort mute; no endless retry loop.
    if (wasEnabled && self.audioStatusHandler)
      self.audioStatusHandler(NO, @"Moss audio could not start · configuration timed out");
  }
  [self writeAvailable];
  if (_fd < 0 && !_wirelessControl) return;
  if (((_tx && _txOffset < _tx.length) || _networkWriting) && now - _writeStarted > 2) {
    [self closePort:@"Display transfer timed out · choose Remote Display again"]; return;
  }
  if (_networkWriting) return; // An ACK may arrive before Network.framework send completion.
  if ([self sendAudioIfNeeded]) return;
  if (_awaiting) {
    if (_sentAt && now - _sentAt >= 1) {
      if (_retries++ < 2) { _txOffset = 0; _sentAt = 0; _writeStarted = now; [self writeAvailable]; }
      else {
        NSLog(@"DISPLAY ACK TIMEOUT session=%016llx waiting_type=%u waiting_sequence=%u last_sequence=%u tx_bytes=%lu tx_offset=%zu audio_awaiting=%u network_writing=%u retries=%u",
              (unsigned long long)_session, (unsigned)_waitingType, _waitingSequence, _lastSequence,
              (unsigned long)_tx.length, _txOffset, (unsigned)_audioAwaiting, (unsigned)_networkWriting, _retries);
        [self closePort:@"Moss stopped responding · choose Remote Display again"];
      }
    }
    return;
  }
  if (_tx) return;
  if (_stopping) { [self control:_wirelessControl ? moss_wire::Release : moss_wire::Stop sequence:_active ? _lastSequence + 1 : 0]; return; }
  if (_active) {
    if (!_sendingFrame && _needsLosslessRefresh && _sourceFrame &&
        (!_allowsLossyCompression || now - _lastContentChange >= 0.25)) {
      [self invalidateFrameBaseline];
      _refiningLossless = YES; _needsLosslessRefresh = NO;
      [self submitFrame:_sourceFrame]; // Also works when ScreenCaptureKit has gone idle.
    }
    if (!_sendingFrame && _latestFrame) {
      _sendingFrame = _latestFrame; _latestFrame = nil; _row = 0;
      _delta.resize((unsigned)_transferSize);
      _frameStarted = now; _frameRects = _frameLosslessPixels = 0; _frameWireBytes = 0; _frameJpeg = NO;
    }
    if (_sendingFrame) {
      // Refill before sending an indivisible image packet. One PCM packet per
      // video frame cannot keep up when the image takes longer than 100 ms.
      // Older firmware lacks this optional telemetry and retains legacy pacing.
      if (_audioActive && _audioQueueMeasuredAt && now-_lastAudioInput<0.2) {
        const double queued=(double)_audioQueuedSamples-(now-_audioQueueMeasuredAt)*16000;
        if (queued<2880) { // 160 ms plus 20 ms transport allowance.
          if (!_videoAudioWaitSince) _videoAudioWaitSince=now;
          if (now-_videoAudioWaitSince<0.15) return;
        }
      }
      // A silent/stalled capture or delayed telemetry cannot freeze the desktop.
      _videoAudioWaitSince=0;
      const unsigned size = _delta.size();
      const uint8_t *frame = static_cast<const uint8_t *>(_sendingFrame.bytes);
      moss_wire::Band band;
      if (_row == 0 && !_refiningLossless && _allowsLossyCompression && _wifiRequested &&
          (_capabilities & moss_wire::Jpeg240) && size == 240) {
        size_t losslessBytes = 0;
        unsigned row = 0;
        while (_delta.next(frame, _sendingFrame.length, row, band, (_capabilities & moss_wire::CropRects) != 0)) {
          const auto pixels = moss_wire::pack(frame, _sendingFrame.length, size, band);
          const auto encoded = moss_wire::encodePixels(pixels.data(), pixels.size(), size, _capabilities);
          losslessBytes += encoded.payload.size() + encoded.payload.size() / 254 + 40;
          row = band.y + band.height;
        }
        // Compressible text/UI stays lossless. Full JPEG is worthwhile only for
        // large, poorly compressing updates, and must save at least forty percent.
        if (losslessBytes > 20000) {
          NSData *jpeg = MossEncodeJPEG240(_sendingFrame, _audioActive ? 0.25 : 0.5);
          // A whole JPEG cannot interleave PCM. Keep its wire time within the
          // playback reservoir; detailed scenes can use smaller lossless strips.
          if (_audioActive && jpeg.length > 4096) jpeg = MossEncodeJPEG240(_sendingFrame, 0.15);
          if (jpeg && (!_audioActive || jpeg.length <= 4096) && jpeg.length + 100 < losslessBytes * 0.6) {
            if (_lastSequence == UINT32_MAX) { [self failWiFi:@"Display session sequence exhausted · choose Remote Display again"]; return; }
            _pendingFrame = _sendingFrame; _pendingBands.clear(); _frameJpeg = YES;
            [self queuePacket:moss_wire::ScaledJpegFrame sequence:_lastSequence + 1 y:0 height:240
                       payload:static_cast<const uint8_t *>(jpeg.bytes) length:jpeg.length awaitReply:YES];
            return;
          }
        }
      }
      if (_delta.next(frame, _sendingFrame.length, _row, band, (_capabilities & moss_wire::CropRects) != 0)) {
        const unsigned maximumBands = _wifiRequested ? 30 : 1;
        NSMutableData *batch = [NSMutableData dataWithCapacity:_wifiRequested ? 65536 : 15500];
        _pendingBands.clear();
        moss_wire::Type lastType = moss_wire::Rect;
        uint32_t sequence = _lastSequence;
        do {
          if (sequence == UINT32_MAX) {
            [self failWiFi:@"Display session sequence exhausted · choose Remote Display again"];
            return;
          }
          const auto pixels = moss_wire::pack(frame, _sendingFrame.length, size, band);
          const auto encoded = moss_wire::encodePixels(pixels.data(), pixels.size(), size, _capabilities);
          const auto packet = moss_wire::packet(encoded.type, _session, sequence + 1, band.y, band.height,
                                               encoded.payload.data(), encoded.payload.size(), band.x, band.width);
          if (encoded.payload.empty() || packet.empty() || packet.size() > 65536) {
            [self failWiFi:@"Display packet exceeded its transport limit"];
            return;
          }
          // More compressed strips may fit in one bounded write. Leave the next
          // strip for a later batch instead of overrunning the 64 KiB budget.
          const NSUInteger budget = _audioActive ? 4096 : 65536;
          if (batch.length && batch.length + packet.size() > budget) break;
          [batch appendBytes:packet.data() length:packet.size()];
          ++sequence; lastType = encoded.type;
          _pendingBands.push_back(band);
        } while (_pendingBands.size() < maximumBands &&
                 _delta.next(frame, _sendingFrame.length, band.y + band.height, band,
                             (_capabilities & moss_wire::CropRects) != 0));
        // Keep one immutable batch for whole-batch retransmission. The baseline,
        // current frame and row remain unchanged until its final sequence ACK.
        _pendingFrame = _sendingFrame;
        _tx = [batch copy]; _txOffset = 0;
        _awaiting = YES; _waitingType = lastType; _waitingSequence = sequence;
        _retries = 0; _sentAt = 0;
        _writeStarted = now; _bound = YES;
        [self writeAvailable];
        return;
      }
      if (!_frameJpeg && _frameLosslessPixels == size * size) _needsLosslessRefresh = NO;
      ++_framesSent;
      if (_frameRects > 0) ++_changedFramesSent;
      if (_framesSent == 1 || _framesSent % (_wifiRequested ? 10 : 30) == 0)
        NSLog(@"DISPLAY FRAME COMPLETE transport=%@ session=%016llx frames=%u changed_frames=%u transfer=%ux%u rectangles=%u wire_bytes=%zu elapsed_ms=%.1f RLE=%@ LZ4=%@ crop=%@ JPEG=%@",
              _wifi.ready ? @"Wi-Fi" : @"USB", (unsigned long long)_session, _framesSent, _changedFramesSent, size, size, _frameRects, _frameWireBytes,
              (now - _frameStarted) * 1000, _capabilities & moss_wire::Rle ? @"yes" : @"no",
              _capabilities & moss_wire::Lz4 ? @"yes" : @"no", _capabilities & moss_wire::CropRects ? @"yes" : @"no", _frameJpeg ? @"yes" : @"no");
      _sendingFrame = nil; _row = 0; _refiningLossless = NO;
    }
    if (now - _lastTransmit >= 1) {
      if (_lastSequence == UINT32_MAX) [self failWiFi:@"Display session sequence exhausted · choose Remote Display again"];
      else [self control:moss_wire::Ping sequence:_lastSequence + 1];
    }
  } else if (!_session && now - _lastQuery >= 2) {
    _lastQuery = now;
    // '?' is read-only in the new firmware and ignored by older pets. Once a
    // REQUEST binds a binary session, only framed control is ever sent.
    if (_bound) [self queuePacket:moss_wire::Query sequence:0 y:0 height:0 payload:nullptr length:0 awaitReply:NO];
    else { const char query[] = "?\n"; (void)write(_fd, query, sizeof(query) - 1); }
  }
}
@end
