#import "DisplayCapture.h"
#import "VirtualDisplay.h"
#import "AudioConverter.h"
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

@interface MossDisplayCapture () <SCStreamOutput, SCStreamDelegate>
@end

@implementation MossDisplayCapture {
  SCStream *_stream;
  dispatch_queue_t _frames;
  NSUInteger _generation;
  BOOL _audioEnabled, _audioFailed;
  MossAudioConverter *_audioConverter;
  SCStream *_audioPendingStream;
  NSMutableData *_pendingAudio;
  NSError *_pendingAudioError;
  SCStream *_videoPendingStream;
  NSData *_pendingVideo;
}
- (instancetype)init {
  if ((self = [super init])) _frames = dispatch_queue_create("org.moss.display.capture", DISPATCH_QUEUE_SERIAL);
  return self;
}
- (BOOL)audioEnabled { @synchronized(self) { return _audioEnabled; } }
- (void)setAudioEnabled:(BOOL)enabled { @synchronized(self) { _audioEnabled = enabled; } }
- (void)startDisplay:(CGDirectDisplayID)displayID pixelSize:(unsigned)size completion:(void (^)(NSError *))completion {
  NSAssert([NSThread isMainThread], @"Capture lifecycle must run on the main thread");
  [self stop];
  const NSUInteger generation = _generation;
  if (size != 240 && size != 480) {
    completion([NSError errorWithDomain:@"MossDisplay" code:3 userInfo:@{NSLocalizedDescriptionKey:@"Unsupported pixel size"}]);
    return;
  }
  [self findDisplay:displayID pixelSize:size generation:generation attempt:0 completion:completion];
}
- (void)findDisplay:(CGDirectDisplayID)displayID pixelSize:(unsigned)size generation:(NSUInteger)generation
            attempt:(NSUInteger)attempt completion:(void (^)(NSError *))completion {
  [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:NO
      completionHandler:^(SCShareableContent *content, NSError *error) {
    dispatch_async(dispatch_get_main_queue(), ^{
      if (generation != self->_generation) return;
      SCDisplay *target = nil;
      for (SCDisplay *display in content.displays) if (display.displayID == displayID) { target = display; break; }
      if (attempt == 0 || attempt == 20 || target || error) {
        NSMutableArray *available = [NSMutableArray array];
        for (SCDisplay *display in content.displays)
          [available addObject:[NSString stringWithFormat:@"%u:%zux%zu", display.displayID, display.width, display.height]];
        NSLog(@"ScreenCaptureKit displays wanted=%u attempt=%lu available=%@ error=%@", displayID, (unsigned long)attempt, available, error);
        [MossVirtualDisplay logState:@"capture enumeration" display:displayID];
      }
      if (!error && !target && attempt < 20) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
          if (generation == self->_generation) [self findDisplay:displayID pixelSize:size generation:generation attempt:attempt + 1 completion:completion];
        });
        return;
      }
      if (error || !target) {
        completion(error ?: [NSError errorWithDomain:@"MossDisplay" code:2
            userInfo:@{NSLocalizedDescriptionKey:@"The requested virtual display is not available for capture."}]);
        return;
      }
      // Never substitute the main display if enumeration races with removal.
      SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:target excludingWindows:@[]];
      filter.includeMenuBar = YES;
      SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
      config.width = size;
      config.height = size;
      config.pixelFormat = kCVPixelFormatType_32BGRA;
      config.minimumFrameInterval = CMTimeMake(1, 30);
      config.queueDepth = 3;
      config.showsCursor = YES;
      config.scalesToFit = YES;
      config.preservesAspectRatio = YES;
      config.capturesAudio = self.audioEnabled;
      config.sampleRate = 48000;
      config.channelCount = 1;
      config.excludesCurrentProcessAudio = YES;
      config.captureMicrophone = NO;
      config.colorSpaceName = kCGColorSpaceSRGB;
      @synchronized(self) {
        self->_stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:self];
        self->_audioConverter = config.capturesAudio ? [[MossAudioConverter alloc] init] : nil;
        self->_audioFailed = NO;
      }
      NSError *outputError = nil;
      if (![self->_stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:self->_frames error:&outputError]) {
        @synchronized(self) { self->_stream = nil; self->_audioConverter = nil; }
        completion(outputError);
        return;
      }
      if (config.capturesAudio && ![self->_stream addStreamOutput:self type:SCStreamOutputTypeAudio sampleHandlerQueue:self->_frames error:&outputError]) {
        @synchronized(self) { self->_stream = nil; self->_audioConverter = nil; }
        completion(outputError);
        return;
      }
      [self->_stream startCaptureWithCompletionHandler:^(NSError *startError) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (generation != self->_generation) return;
          completion(startError);
        });
      }];
    });
  }];
}
- (void)stop {
  NSAssert([NSThread isMainThread], @"Capture lifecycle must run on the main thread");
  ++_generation;
  SCStream *old;
  @synchronized(self) {
    old = _stream; _stream = nil;
    _audioConverter = nil; _audioPendingStream = nil; _audioFailed = NO;
    _pendingAudio = nil; _pendingAudioError = nil;
    _videoPendingStream = nil; _pendingVideo = nil;
  }
  if (old) [old stopCaptureWithCompletionHandler:^(NSError *error) { (void)error; }];
}
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
  dispatch_async(dispatch_get_main_queue(), ^{
    if (stream == self->_stream && self.failureHandler) self.failureHandler(error.localizedDescription);
  });
}
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type {
  @synchronized(self) { if (stream != _stream) return; }
  if (type == SCStreamOutputTypeAudio) {
    if (!CMSampleBufferIsValid(sample) || !CMSampleBufferDataIsReady(sample) || CMSampleBufferGetNumSamples(sample) <= 0) return;
    MossAudioConverter *converter;
    @synchronized(self) {
      if (!_audioEnabled || _audioFailed || !_audioConverter || stream != _stream) return;
      converter = _audioConverter;
    }
    NSError *error = nil;
    NSData *pcm = [converter convertSample:sample error:&error];
    @synchronized(self) {
      if (stream != _stream) return;
      if (error) { _audioFailed = YES; _pendingAudioError = error; _pendingAudio = nil; }
      else if (pcm.length) {
        if (!_pendingAudio) _pendingAudio = [NSMutableData data];
        if (_pendingAudio.length + pcm.length > 6400)
          [_pendingAudio replaceBytesInRange:NSMakeRange(0, _pendingAudio.length + pcm.length - 6400) withBytes:NULL length:0];
        [_pendingAudio appendData:pcm];
      } else return;
      if (_audioPendingStream == stream) return;
      _audioPendingStream = stream;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      NSData *pending;
      NSError *pendingError;
      @synchronized(self) {
        if (self->_audioPendingStream != stream) return;
        self->_audioPendingStream = nil;
        pending = [self->_pendingAudio copy]; pendingError = self->_pendingAudioError;
        self->_pendingAudio = nil; self->_pendingAudioError = nil;
        if (stream != self->_stream || !self->_audioEnabled) return;
      }
      if (pendingError) { if (self.audioFailureHandler) self.audioFailureHandler(pendingError.localizedDescription); }
      else if (pending.length && self.audioHandler) {
        // Drain at most200ms of latest PCM in100ms transport submissions, with
        // one pending main-queue callback while resampling remains continuous.
        for (NSUInteger offset = 0; offset < pending.length; offset += 3200) {
          @synchronized(self) { if (stream != self->_stream || !self->_audioEnabled) return; }
          NSData *chunk = [pending subdataWithRange:NSMakeRange(offset, MIN((NSUInteger)3200, pending.length - offset))];
          if (self.audioHandler) self.audioHandler(chunk);
        }
      }
    });
    return;
  }
  if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample)) return;
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
  if (!attachments || !CFArrayGetCount(attachments)) return;
  NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
  if ([info[SCStreamFrameInfoStatus] integerValue] != SCFrameStatusComplete) return;
  CVPixelBufferRef image = CMSampleBufferGetImageBuffer(sample);
  if (!image) return;
  const size_t size = CVPixelBufferGetWidth(image);
  if ((size != 240 && size != 480) || CVPixelBufferGetHeight(image) != size ||
      CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_32BGRA) return;
  if (CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
  NSMutableData *data = [NSMutableData dataWithLength:size * size * 2];
  uint8_t *output = data.mutableBytes;
  const uint8_t *source = CVPixelBufferGetBaseAddress(image);
  const size_t stride = CVPixelBufferGetBytesPerRow(image);
  for (unsigned y = 0; y < size; ++y) {
    const uint8_t *row = source + y * stride;
    for (unsigned x = 0; x < size; ++x) {
      const uint8_t *pixel = row + x * 4;
      const uint16_t rgb = ((uint16_t)(pixel[2] >> 3) << 11) | ((uint16_t)(pixel[1] >> 2) << 5) | (pixel[0] >> 3);
      *output++ = (uint8_t)rgb;
      *output++ = (uint8_t)(rgb >> 8);
    }
  }
  CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
  @synchronized(self) {
    if (stream != _stream) return;
    _pendingVideo = data;
    if (_videoPendingStream == stream) return; // Replace stale capture without another queued callback.
    _videoPendingStream = stream;
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    NSData *latest;
    @synchronized(self) {
      if (self->_videoPendingStream != stream) return;
      latest = self->_pendingVideo;
      self->_pendingVideo = nil; self->_videoPendingStream = nil;
      if (stream != self->_stream) return;
    }
    if (latest && self.frameHandler) self.frameHandler(latest);
  });
}
@end
