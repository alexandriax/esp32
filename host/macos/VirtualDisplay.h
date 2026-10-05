#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

// All creation/destruction runs on the main queue. Asynchronous attachment
// leaves USB heartbeats responsive and is cancelled by remove.
@interface MossVirtualDisplay : NSObject
@property(nonatomic, readonly) CGDirectDisplayID displayID;
@property(nonatomic, readonly) unsigned desktopSize;
@property(nonatomic, readonly) unsigned backingSize;
+ (BOOL)isSupported;
+ (void)logState:(NSString *)context display:(CGDirectDisplayID)displayID;
- (void)createWithCompletion:(void (^)(NSError *))completion;
// 800 (larger UI) or 960 (more workspace), selected only if macOS reports a
// usable desktop mode. 800 falls back to 960 if required by the running OS.
// Smaller 480/720 modes were rejected by WindowServer on macOS 26.5.1.
- (void)createWithDesktopSize:(unsigned)size completion:(void (^)(NSError *))completion;
- (void)useCompatibilityModeWithCompletion:(void (^)(NSError *))completion;
- (void)remove;
@end
