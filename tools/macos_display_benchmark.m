#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <AVFoundation/AVFoundation.h>

static NSString *testMode = @"scroll";
static double testSeconds = 15;
static BOOL audioTone = NO;
static BOOL audioSilence = NO;
static int exitStatus = 0;

// A quarter-second 440 Hz pulse each second, with 20 ms cosine fades. Generate
// in memory; no files, input node, microphone, or changes to system volume.
static float toneSample(uint64_t frame) {
  const uint64_t position = frame % 48000;
  if (position >= 12000) return 0;
  const double ramp = fmin(1, fmin(position / 960.0, (12000 - position) / 960.0));
  const double envelope = 0.5 - 0.5 * cos(M_PI * ramp);
  return (float)(0.08 * envelope * sin(2 * M_PI * 440 * position / 48000.0));
}

@interface BenchmarkView : NSView
@property(nonatomic) CFTimeInterval began;
@property(nonatomic, strong) NSImage *texture;
@end
@implementation BenchmarkView
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  [[NSColor colorWithCalibratedWhite:0.14 alpha:1] setFill]; NSRectFill(self.bounds);
  const CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
  const double elapsed = CACurrentMediaTime() - self.began;
  NSDictionary *title = @{NSFontAttributeName:[NSFont systemFontOfSize:28 weight:NSFontWeightSemibold],
                          NSForegroundColorAttributeName:[NSColor colorWithCalibratedWhite:0.88 alpha:1]};
  NSDictionary *small = @{NSFontAttributeName:[NSFont systemFontOfSize:16],
                          NSForegroundColorAttributeName:[NSColor colorWithCalibratedWhite:0.70 alpha:1]};
  [@"Moss · Wi-Fi motion test" drawAtPoint:NSMakePoint(38, height - 72) withAttributes:title];
  [[NSString stringWithFormat:@"Synthetic %@ · closes after %.0f seconds", testMode, testSeconds]
      drawAtPoint:NSMakePoint(38, height - 105) withAttributes:small];
  if ([testMode isEqualToString:@"photo"]) {
    if (!self.texture) {
      NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:512 pixelsHigh:512
          bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
      uint32_t random = 0x17493285;
      for (int y = 0; y < 512; ++y) for (int x = 0; x < 512; ++x) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        double grain = ((random & 255) / 255.0 - .5) * .14;
        double wave = sin(x * .043 + sin(y * .019) * 3) * .05;
        double channels[3] = {.25 + x / 1800.0 + grain + wave,
                              .32 + y / 1700.0 + grain - wave,
                              .36 + (x + y) / 3200.0 + grain};
        unsigned char *pixel = bitmap.bitmapData + y * bitmap.bytesPerRow + x * 4;
        for (unsigned c = 0; c < 3; ++c) pixel[c] = (unsigned char)(fmax(0, fmin(1, channels[c])) * 255);
        pixel[3] = 255;
      }
      self.texture = [[NSImage alloc] initWithSize:NSMakeSize(512, 512)];
      [self.texture addRepresentation:bitmap];
    }
    // A slowly panning textured image tests detailed motion without flashing.
    [self.texture drawInRect:NSMakeRect(36, 80, width - 72, height - 225)
                   fromRect:NSMakeRect(30 + sin(elapsed * .3) * 25, 30 + cos(elapsed * .25) * 25, 440, 440)
                  operation:NSCompositingOperationCopy fraction:1];
    return;
  }
  if ([testMode isEqualToString:@"cursor"]) {
    [@"Only this small marker moves" drawAtPoint:NSMakePoint(38, height - 150) withAttributes:small];
    [[NSColor colorWithCalibratedRed:.58 green:.75 blue:.54 alpha:1] setFill];
    NSRect marker = NSMakeRect(width / 2 + sin(elapsed * .7) * width * .25,
                              height / 2 + cos(elapsed * .5) * height * .2, 20, 20);
    [[NSBezierPath bezierPathWithOvalInRect:marker] fill];
    return;
  }
  [NSGraphicsContext saveGraphicsState];
  NSRectClip(NSMakeRect(36, 80, width - 72, height - 225));
  // A gentle continuous scroll. Stable muted colors avoid flashes and strobing.
  CGFloat offset = fmod((CACurrentMediaTime() - self.began) * 35.0, 78.0);
  for (NSInteger row = -1; row < 13; ++row) {
    CGFloat y = height - 215 - row * 78 + offset;
    [[NSColor colorWithCalibratedWhite:0.23 alpha:1] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(40, y, width - 80, 62) xRadius:10 yRadius:10] fill];
    [[NSColor colorWithCalibratedWhite:0.48 alpha:1] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(58, y + 19, 24, 24) xRadius:6 yRadius:6] fill];
    [[NSColor colorWithCalibratedWhite:0.65 alpha:1] setFill];
    NSRectFill(NSMakeRect(100, y + 35, width * 0.44, 6));
    [[NSColor colorWithCalibratedWhite:0.36 alpha:1] setFill];
    NSRectFill(NSMakeRect(100, y + 18, width * 0.30, 5));
  }
  [NSGraphicsContext restoreGraphicsState];
  [@"No physical monitor is captured by this helper." drawAtPoint:NSMakePoint(38, 35) withAttributes:small];
}
@end

@interface BenchmarkApp : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, strong) BenchmarkView *view;
@property(nonatomic, strong) AVAudioEngine *audioEngine;
@end
@implementation BenchmarkApp
- (BOOL)startTone {
  self.audioEngine = [[AVAudioEngine alloc] init];
  AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:1];
  __block uint64_t frame = 0;
  AVAudioSourceNode *source = [[AVAudioSourceNode alloc] initWithFormat:format renderBlock:
      ^OSStatus(BOOL *isSilence, const AudioTimeStamp *timestamp,
                AVAudioFrameCount frameCount, AudioBufferList *output) {
    (void)timestamp;
    BOOL silent = YES;
    for (AVAudioFrameCount i = 0; i < frameCount; ++i) {
      const float sample = audioSilence ? 0 : toneSample(frame++);
      if (sample != 0) silent = NO;
      for (UInt32 channel = 0; channel < output->mNumberBuffers; ++channel)
        ((float *)output->mBuffers[channel].mData)[i] = sample;
    }
    *isSilence = silent;
    return noErr;
  }];
  [self.audioEngine attachNode:source];
  [self.audioEngine connect:source to:self.audioEngine.mainMixerNode format:format];
  NSError *error = nil;
  if (![self.audioEngine startAndReturnError:&error]) {
    fprintf(stderr, "Cannot start synthetic benchmark audio: %s\n", error.localizedDescription.UTF8String);
    return NO;
  }
  return YES;
}
- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  [self.audioEngine stop];
  // Cocoa's terminate: exits directly, without returning through main().
  if (exitStatus) exit(exitStatus);
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  NSMutableArray<NSScreen *> *matches = [NSMutableArray array];
  for (NSScreen *screen in NSScreen.screens)
    if ([screen.localizedName isEqualToString:@"Moss USB Display"]) [matches addObject:screen];
  if (matches.count != 1) {
    fprintf(stderr, "Refusing benchmark: expected exactly one Moss USB Display, found %lu\n", (unsigned long)matches.count);
    exitStatus = 1;
    [NSApp terminate:nil]; return;
  }
  NSScreen *screen = matches.firstObject;
  NSRect frame = screen.frame;
  fprintf(stdout, "Drawing synthetic %s test on %s at %.0f,%.0f %.0fx%.0f for %.0f seconds\n",
          testMode.UTF8String, screen.localizedName.UTF8String, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height, testSeconds);
  self.window = [[NSWindow alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
  self.window.releasedWhenClosed = NO;
  self.window.ignoresMouseEvents = YES;
  self.window.title = @"Temporary Moss Display Benchmark";
  self.view = [[BenchmarkView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
  self.view.began = CACurrentMediaTime();
  self.window.contentView = self.view;
  [self.window setFrame:frame display:YES];
  [self.window orderFrontRegardless];
  if (audioTone && ![self startTone]) {
    exitStatus = 1;
    [self.window close]; [NSApp terminate:nil]; return;
  }
  self.view.began = CACurrentMediaTime(); // Exclude audio-engine startup from either timed scene.
  // Audio mode does not change the visual scene, making paired runs comparable.
  fprintf(stdout, "BENCH begin_epoch=%.3f mode=%s duration=%.0f audio_tone=%u audio_silence=%u\n",
          NSDate.date.timeIntervalSince1970, testMode.UTF8String, testSeconds,
          audioTone && !audioSilence ? 1u : 0u, audioSilence ? 1u : 0u);
  fflush(stdout);
  __weak BenchmarkApp *weakSelf = self;
  self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 repeats:YES block:^(NSTimer *timer) {
    BenchmarkApp *self = weakSelf;
    if (!self || CACurrentMediaTime() - self.view.began >= testSeconds) {
      fprintf(stdout, "BENCH end_epoch=%.3f\n", NSDate.date.timeIntervalSince1970);
      fflush(stdout);
      [self.audioEngine stop];
      [timer invalidate]; [self.window close]; [NSApp terminate:nil]; return;
    }
    self.view.needsDisplay = YES;
  }];
}
@end
int main(int argc, const char *argv[]) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    if (argc == 2 && !strcmp(argv[1], "--list")) {
      for (NSScreen *screen in NSScreen.screens) {
        NSRect f = screen.frame;
        printf("%s | %.0f,%.0f %.0fx%.0f\n", screen.localizedName.UTF8String,
               f.origin.x, f.origin.y, f.size.width, f.size.height);
      }
      return 0;
    }
    for (int i = 1; i < argc; ++i) {
      if (!strcmp(argv[i], "--mode") && i + 1 < argc) testMode = [NSString stringWithUTF8String:argv[++i]];
      else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
        char *end = NULL;
        testSeconds = strtod(argv[++i], &end);
        if (!end || *end || !isfinite(testSeconds)) return 2;
      } else if (!strcmp(argv[i], "--audio-tone")) audioTone = YES;
      else if (!strcmp(argv[i], "--audio-silence")) { audioTone = YES; audioSilence = YES; }
      else { fprintf(stderr, "Usage: benchmark [--list | --mode scroll|cursor|photo --seconds 5..60 [--audio-tone | --audio-silence]]\n"); return 2; }
    }
    if (![@[@"scroll", @"cursor", @"photo"] containsObject:testMode] || testSeconds < 5 || testSeconds > 60) return 2;
    BenchmarkApp *delegate = [[BenchmarkApp alloc] init];
    NSApp.delegate = delegate;
    [NSApp run];
  }
  return exitStatus;
}
