#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import "DisplayCapture.h"
#import "AudioConverter.h"
#import "VirtualDisplay.h"
#import "USBTransport.h"
#include <assert.h>

@interface MossDisplayCapture (LifecycleTest)
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type;
@end
@interface SyntheticStream : NSObject
@property(nonatomic) unsigned stops;
- (void)stopCaptureWithCompletionHandler:(void (^)(NSError *))completion;
@end
@implementation SyntheticStream
- (void)stopCaptureWithCompletionHandler:(void (^)(NSError *))completion { ++_stops; completion(nil); }
@end
static void deliver(MossDisplayCapture *capture, SyntheticStream *stream, uint8_t blue) {
  CVPixelBufferRef image = NULL;
  assert(CVPixelBufferCreate(NULL, 240, 240, kCVPixelFormatType_32BGRA, NULL, &image) == kCVReturnSuccess);
  assert(CVPixelBufferLockBaseAddress(image, 0) == kCVReturnSuccess);
  uint8_t *bytes = CVPixelBufferGetBaseAddress(image);
  for (unsigned y = 0; y < 240; ++y) for (unsigned x = 0; x < 240; ++x) {
    uint8_t *pixel = bytes + y * CVPixelBufferGetBytesPerRow(image) + x * 4;
    pixel[0] = blue; pixel[1] = pixel[2] = 0; pixel[3] = 255;
  }
  CVPixelBufferUnlockBaseAddress(image, 0);
  CMVideoFormatDescriptionRef format = NULL;
  assert(CMVideoFormatDescriptionCreateForImageBuffer(NULL, image, &format) == noErr);
  CMSampleTimingInfo timing = {CMTimeMake(1, 30), kCMTimeZero, kCMTimeInvalid};
  CMSampleBufferRef sample = NULL;
  assert(CMSampleBufferCreateReadyWithImageBuffer(NULL, image, format, &timing, &sample) == noErr);
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, true);
  NSMutableDictionary *info = (__bridge NSMutableDictionary *)CFArrayGetValueAtIndex(attachments, 0);
  info[SCStreamFrameInfoStatus] = @(SCFrameStatusComplete);
  [capture stream:(SCStream *)stream didOutputSampleBuffer:sample ofType:SCStreamOutputTypeScreen];
  CFRelease(sample); CFRelease(format); CFRelease(image);
}
static void deliverAudio(MossDisplayCapture *capture, SyntheticStream *stream) {
  AudioStreamBasicDescription format = {0};
  format.mSampleRate = 48000; format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
  format.mBytesPerPacket = format.mBytesPerFrame = 4;
  format.mFramesPerPacket = format.mChannelsPerFrame = 1; format.mBitsPerChannel = 32;
  CMAudioFormatDescriptionRef description = NULL;
  assert(CMAudioFormatDescriptionCreate(NULL, &format, 0, NULL, 0, NULL, NULL, &description) == noErr);
  CMBlockBufferRef block = NULL;
  float values[480]; for (unsigned i = 0; i < 480; ++i) values[i] = 0.25f;
  assert(CMBlockBufferCreateWithMemoryBlock(NULL, NULL, sizeof(values), NULL, NULL, 0, sizeof(values), 0, &block) == noErr);
  assert(CMBlockBufferReplaceDataBytes(values, block, 0, sizeof(values)) == noErr);
  CMSampleBufferRef sample = NULL;
  assert(CMAudioSampleBufferCreateReadyWithPacketDescriptions(NULL, block, description, 480, kCMTimeZero, NULL, &sample) == noErr);
  [capture stream:(SCStream *)stream didOutputSampleBuffer:sample ofType:SCStreamOutputTypeAudio];
  CFRelease(sample); CFRelease(description); CFRelease(block);
}
static void drain(void) { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false); }

@interface MossApp : NSObject
- (void)startCapture:(uint64_t)session allowCompatibility:(BOOL)allowCompatibility;
- (void)failed:(NSString *)reason;
- (void)setStatus:(NSString *)message;
- (void)updateChoices;
@end
@interface LifecycleApp : MossApp
@property(nonatomic) unsigned failures;
@property(nonatomic, copy) NSString *status;
@end
@implementation LifecycleApp
- (void)failed:(NSString *)reason { assert(reason.length); ++_failures; }
- (void)setStatus:(NSString *)message { _status = [message copy]; }
- (void)updateChoices {}
@end
@interface SyntheticCapture : MossDisplayCapture
@property(nonatomic) unsigned starts, failuresRemaining;
@property(nonatomic, strong) NSMutableArray<NSNumber *> *audioChoices;
@end
@implementation SyntheticCapture
- (instancetype)init { if ((self = [super init])) _audioChoices = [NSMutableArray array]; return self; }
- (void)startDisplay:(CGDirectDisplayID)displayID pixelSize:(unsigned)size completion:(void (^)(NSError *))completion {
  assert(displayID == 99 && size == 480); ++_starts; [_audioChoices addObject:@(self.audioEnabled)];
  if (_failuresRemaining) { --_failuresRemaining; completion([NSError errorWithDomain:@"SyntheticCapture" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Synthetic capture failure"}]); }
  else completion(nil);
}
@end
@interface SyntheticDisplay : MossVirtualDisplay @end
@implementation SyntheticDisplay
- (CGDirectDisplayID)displayID { return 99; }
- (unsigned)desktopSize { return 960; }
@end
@interface SyntheticTransport : MossUSBTransport @end
@implementation SyntheticTransport
- (BOOL)wifiConnected { return YES; }
- (uint32_t)capabilities { return 127; }
@end
static void fallback(unsigned failures, BOOL audio, unsigned expectedStarts, unsigned expectedFailures) {
  LifecycleApp *app = [[LifecycleApp alloc] init];
  SyntheticCapture *capture = [[SyntheticCapture alloc] init]; capture.failuresRemaining = failures;
  [app setValue:capture forKey:@"capture"];
  [app setValue:[[SyntheticDisplay alloc] init] forKey:@"display"];
  [app setValue:[[SyntheticTransport alloc] init] forKey:@"usb"];
  [app setValue:@123 forKey:@"session"]; [app setValue:@(audio) forKey:@"audioPreference"];
  [app startCapture:123 allowCompatibility:NO];
  assert(capture.starts == expectedStarts && app.failures == expectedFailures);
  assert([[app valueForKey:@"session"] unsignedLongLongValue] == 123);
  if (expectedStarts == 2) assert([capture.audioChoices isEqual:(@[@YES, @NO])]);
}
int main(void) {
  @autoreleasepool {
    MossDisplayCapture *capture = [[MossDisplayCapture alloc] init];
    SyntheticStream *first = [[SyntheticStream alloc] init];
    [capture setValue:first forKey:@"stream"];
    __block unsigned callbacks = 0;
    __block NSData *latest;
    capture.frameHandler = ^(NSData *pixels) { ++callbacks; latest = pixels; };
    for (unsigned i = 0; i < 60; ++i) deliver(capture, first, (uint8_t)i);
    assert(!callbacks && [[capture valueForKey:@"pendingVideo"] length] == 240 * 240 * 2);
    drain(); assert(callbacks == 1 && ((const uint8_t *)latest.bytes)[0] == (59 >> 3));
    assert(![capture valueForKey:@"pendingVideo"] && ![capture valueForKey:@"videoPendingStream"]);
    deliver(capture, first, 64); [capture stop]; assert(first.stops == 1);
    SyntheticStream *second = [[SyntheticStream alloc] init];
    [capture setValue:second forKey:@"stream"];
    deliver(capture, first, 255); // Stale stream cannot replace the new frame.
    deliver(capture, second, 128);
    drain(); assert(callbacks == 2 && ((const uint8_t *)latest.bytes)[0] == (128 >> 3));
    deliver(capture, second, 240); [capture stop]; drain(); assert(callbacks == 2);
    SyntheticStream *audioStream = [[SyntheticStream alloc] init];
    [capture setValue:audioStream forKey:@"stream"];
    [capture setValue:[[MossAudioConverter alloc] init] forKey:@"audioConverter"];
    capture.audioEnabled = YES;
    __block NSUInteger deliveredBytes = 0, audioCallbacks = 0;
    capture.audioHandler = ^(NSData *pcm) { assert(pcm.length <= 3200); deliveredBytes += pcm.length; ++audioCallbacks; };
    for (unsigned i = 0; i < 10; ++i) deliverAudio(capture, audioStream);
    assert(!audioCallbacks && [[capture valueForKey:@"pendingAudio"] length] == 3200);
    drain(); assert(deliveredBytes == 3200 && audioCallbacks == 1);
    for (unsigned i = 0; i < 50; ++i) deliverAudio(capture, audioStream);
    assert([[capture valueForKey:@"pendingAudio"] length] == 6400);
    drain(); assert(deliveredBytes == 9600 && audioCallbacks == 3);
    deliverAudio(capture, audioStream); [capture stop]; drain();
    assert(deliveredBytes == 9600 && audioCallbacks == 3);
    // Restore this test executable's preferences after exercising the real fallback.
    id previous = [NSUserDefaults.standardUserDefaults objectForKey:@"audioEnabled"];
    fallback(1, YES, 2, 0); fallback(2, YES, 2, 1); fallback(1, NO, 1, 1);
    if (previous) [NSUserDefaults.standardUserDefaults setObject:previous forKey:@"audioEnabled"];
    else [NSUserDefaults.standardUserDefaults removeObjectForKey:@"audioEnabled"];
    puts("Capture lifecycle passed:60 queued frames coalesce to latest, stop/restart drops stale callbacks, continuous resampling with200ms bounded audio delivery, optional audio retries once video-only, persistent/video-only failure still reported");
  }
}
