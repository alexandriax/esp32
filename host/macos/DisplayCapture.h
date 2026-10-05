#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

// Captures only the supplied virtual display ID; no physical-display fallback.
// Returned pixels are 240 or480 square RGB565 LE, row-major. Optional Mac/system
// audio follows ScreenCaptureKit's app-level audio policy, not display-only routing.
@interface MossDisplayCapture : NSObject
@property(nonatomic, copy) void (^frameHandler)(NSData *pixels);
@property(nonatomic, copy) void (^failureHandler)(NSString *message);
@property(nonatomic) BOOL audioEnabled; // Default NO; restart capture after changing.
@property(nonatomic, copy) void (^audioHandler)(NSData *pcm); // Main queue; PCM16 LE mono16kHz.
@property(nonatomic, copy) void (^audioFailureHandler)(NSString *message);
- (void)startDisplay:(CGDirectDisplayID)displayID pixelSize:(unsigned)size completion:(void (^)(NSError *error))completion;
- (void)stop;
@end
