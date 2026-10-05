#import "VirtualDisplay.h"
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

// Private CoreGraphics ABI, also used by Chromium's primary implementation:
// https://chromium.googlesource.com/chromium/src/+/HEAD/ui/display/mac/test/virtual_display_util_mac.mm
// Classes are resolved at runtime so an OS removal fails gracefully.
@interface CGVirtualDisplayDescriptor : NSObject
@property(nonatomic) unsigned int vendorID, productID, serialNum, serialNumber;
@property(nonatomic, strong) NSString *name;
@property(nonatomic) CGSize sizeInMillimeters;
@property(nonatomic) unsigned int maxPixelsWide, maxPixelsHigh;
@property(nonatomic) CGPoint redPrimary, greenPrimary, bluePrimary, whitePoint;
@property(nonatomic, strong) id queue;
@end
@interface CGVirtualDisplayMode : NSObject
- (instancetype)initWithWidth:(unsigned int)width height:(unsigned int)height refreshRate:(double)rate;
@end
@interface CGVirtualDisplaySettings : NSObject
@property(nonatomic, strong) NSArray *modes;
@property(nonatomic) unsigned int hiDPI, rotation;
@end
@interface CGVirtualDisplay : NSObject
@property(nonatomic, readonly) unsigned int displayID;
- (instancetype)initWithDescriptor:(CGVirtualDisplayDescriptor *)descriptor;
- (BOOL)applySettings:(CGVirtualDisplaySettings *)settings;
@end

@implementation MossVirtualDisplay {
  CGVirtualDisplay *_display;
  NSUInteger _generation;
  unsigned _desktopSize;
  unsigned _backingSize;
}
+ (BOOL)isSupported {
  return objc_getClass("CGVirtualDisplay") && objc_getClass("CGVirtualDisplayDescriptor") &&
      objc_getClass("CGVirtualDisplayMode") && objc_getClass("CGVirtualDisplaySettings") &&
      class_getInstanceMethod(objc_getClass("CGVirtualDisplay"), @selector(initWithDescriptor:)) &&
      class_getInstanceMethod(objc_getClass("CGVirtualDisplay"), @selector(applySettings:));
}
- (CGDirectDisplayID)displayID { return _display ? _display.displayID : kCGNullDirectDisplay; }
- (unsigned)desktopSize { return _desktopSize; }
- (unsigned)backingSize { return _backingSize; }
+ (void)logState:(NSString *)context display:(CGDirectDisplayID)wanted {
  CGDirectDisplayID ids[32];
  uint32_t count = 0;
  NSMutableArray *online = [NSMutableArray array], *active = [NSMutableArray array], *screens = [NSMutableArray array];
  if (CGGetOnlineDisplayList(32, ids, &count) == kCGErrorSuccess)
    for (uint32_t i = 0; i < count; ++i) [online addObject:@(ids[i])];
  if (CGGetActiveDisplayList(32, ids, &count) == kCGErrorSuccess)
    for (uint32_t i = 0; i < count; ++i) [active addObject:@(ids[i])];
  for (NSScreen *screen in NSScreen.screens)
    [screens addObject:[NSString stringWithFormat:@"%@:%.0fx%.0f@%.1f", screen.deviceDescription[@"NSScreenNumber"],
                        screen.frame.size.width, screen.frame.size.height, screen.backingScaleFactor]];
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(wanted);
  CGRect bounds = CGDisplayBounds(wanted);
  NSLog(@"Display diagnostic %@ wanted=%u online=%d active=%d asleep=%d mirrored=%d onlineIDs=%@ activeIDs=%@ NSScreens=%@ bounds=(%.0f,%.0f %.0fx%.0f) current=%zux%zu pixels=%zux%zu usable=%d",
        context, wanted, (int)CGDisplayIsOnline(wanted), (int)CGDisplayIsActive(wanted), (int)CGDisplayIsAsleep(wanted),
        (int)CGDisplayIsInMirrorSet(wanted), online, active, screens, bounds.origin.x, bounds.origin.y,
        bounds.size.width, bounds.size.height, mode ? CGDisplayModeGetWidth(mode) : 0,
        mode ? CGDisplayModeGetHeight(mode) : 0, mode ? CGDisplayModeGetPixelWidth(mode) : 0,
        mode ? CGDisplayModeGetPixelHeight(mode) : 0, mode ? CGDisplayModeIsUsableForDesktopGUI(mode) : 0);
  if (mode) CGDisplayModeRelease(mode);
}
- (void)fail:(NSString *)reason completion:(void (^)(NSError *))completion {
  [MossVirtualDisplay logState:@"failure" display:self.displayID];
  [self remove];
  completion([NSError errorWithDomain:@"MossDisplay" code:1 userInfo:@{NSLocalizedDescriptionKey:reason}]);
}
- (void)createWithCompletion:(void (^)(NSError *))completion {
  [self createWithDesktopSize:800 completion:completion];
}
- (void)createWithDesktopSize:(unsigned)size completion:(void (^)(NSError *))completion {
  NSAssert([NSThread isMainThread], @"Display lifecycle must run on the main thread");
  [self remove];
  if (size != 800 && size != 960) {
    completion([NSError errorWithDomain:@"MossDisplay" code:3
        userInfo:@{NSLocalizedDescriptionKey:@"Choose an 800 × 800 or 960 × 960 desktop."}]);
    return;
  }
  if (![MossVirtualDisplay isSupported]) {
    [self fail:@"This macOS version does not expose virtual-display support." completion:completion];
    return;
  }
  @try {
    CGVirtualDisplayDescriptor *descriptor = [[objc_getClass("CGVirtualDisplayDescriptor") alloc] init];
    descriptor.name = @"Moss USB Display";
    descriptor.vendorID = 0x4D53;
    // Versioned capabilities: this profile advertises usable 800/960 modes.
    // Do not reuse cached settings from the earlier 480-only prototype.
    descriptor.productID = 0x0003;
    descriptor.serialNum = 0x4D4F5353;
    if ([descriptor respondsToSelector:@selector(setSerialNumber:)]) descriptor.serialNumber = descriptor.serialNum;
    descriptor.queue = dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0);
    descriptor.maxPixelsWide = 960;
    descriptor.maxPixelsHigh = 960;
    // This is a virtual desktop, so describe a conventional 110-PPI monitor,
    // not the tiny physical panel (which can provoke unwanted HiDPI selection).
    descriptor.sizeInMillimeters = CGSizeMake(25.4 * 960 / 110, 25.4 * 960 / 110);
    descriptor.redPrimary = CGPointMake(0.64, 0.33);
    descriptor.greenPrimary = CGPointMake(0.30, 0.60);
    descriptor.bluePrimary = CGPointMake(0.15, 0.06);
    descriptor.whitePoint = CGPointMake(0.3127, 0.3290);
    _display = [[objc_getClass("CGVirtualDisplay") alloc] initWithDescriptor:descriptor];
    if (!_display || !_display.displayID) {
      [self fail:@"macOS could not create the Moss display." completion:completion]; return;
    }
    CGVirtualDisplayMode *compact = [[objc_getClass("CGVirtualDisplayMode") alloc] initWithWidth:800 height:800 refreshRate:60];
    CGVirtualDisplayMode *compatible = [[objc_getClass("CGVirtualDisplayMode") alloc] initWithWidth:960 height:960 refreshRate:60];
    CGVirtualDisplaySettings *settings = [[objc_getClass("CGVirtualDisplaySettings") alloc] init];
    settings.hiDPI = 0;
    settings.rotation = 0;
    settings.modes = @[compact, compatible];
    if (![_display applySettings:settings]) {
      [self fail:@"macOS rejected the Moss display modes." completion:completion]; return;
    }
    [MossVirtualDisplay logState:@"created" display:self.displayID];
    [self prepareSize:size generation:_generation attempt:0 selected:NO completion:completion];
  } @catch (NSException *exception) {
    [self fail:[@"Virtual-display API failed: " stringByAppendingString:exception.reason ?: exception.name] completion:completion];
  }
}
- (void)useCompatibilityModeWithCompletion:(void (^)(NSError *))completion {
  NSAssert([NSThread isMainThread], @"Display lifecycle must run on the main thread");
  if (!_display) { [self fail:@"Moss display was disconnected." completion:completion]; return; }
  NSLog(@"Trying 960 × 960 desktop compatibility mode; capture resolution is configured separately");
  [self prepareSize:960 generation:_generation attempt:0 selected:NO completion:completion];
}
- (void)prepareSize:(unsigned)size generation:(NSUInteger)generation attempt:(NSUInteger)attempt
          selected:(BOOL)selected completion:(void (^)(NSError *))completion {
  if (generation != _generation || !_display) return;
  const CGDirectDisplayID displayID = self.displayID;
  if (!selected) {
    NSDictionary *options = @{(__bridge NSString *)kCGDisplayShowDuplicateLowResolutionModes:@YES};
    CFArrayRef modes = CGDisplayCopyAllDisplayModes(displayID, (__bridge CFDictionaryRef)options);
    if (modes) {
      for (CFIndex i = 0; i < CFArrayGetCount(modes); ++i) {
        CGDisplayModeRef mode = (CGDisplayModeRef)CFArrayGetValueAtIndex(modes, i);
        if (attempt == 0) NSLog(@"Moss mode id=%u logical=%zux%zu pixels=%zux%zu usable=%d flags=%08x",
            CGDisplayModeGetIODisplayModeID(mode), CGDisplayModeGetWidth(mode), CGDisplayModeGetHeight(mode),
            CGDisplayModeGetPixelWidth(mode), CGDisplayModeGetPixelHeight(mode),
            CGDisplayModeIsUsableForDesktopGUI(mode), CGDisplayModeGetIOFlags(mode));
        if (CGDisplayModeGetWidth(mode) != size || CGDisplayModeGetHeight(mode) != size ||
            CGDisplayModeGetPixelWidth(mode) != size || CGDisplayModeGetPixelHeight(mode) != size ||
            !CGDisplayModeIsUsableForDesktopGUI(mode)) continue;
        CGError result = CGDisplaySetDisplayMode(displayID, mode, NULL);
        NSLog(@"Moss select %ux%u result=%d", size, size, result);
        selected = result == kCGErrorSuccess;
        break;
      }
      CFRelease(modes);
    }
  }
  if (selected && CGDisplayIsOnline(displayID) == 1) {
    // Configure only our display, after WindowServer has attached it. Use
    // session-scoped configuration so the physical arrangement is preserved.
    CGDirectDisplayID displays[32];
    uint32_t count = 0;
    if (CGGetOnlineDisplayList(32, displays, &count) != kCGErrorSuccess) {
      [self fail:@"Could not inspect the desktop arrangement." completion:completion]; return;
    }
    double right = 0;
    for (uint32_t i = 0; i < count; ++i)
      if (displays[i] != displayID) right = MAX(right, CGRectGetMaxX(CGDisplayBounds(displays[i])));
    CGDisplayConfigRef configuration = NULL;
    CGError result = CGBeginDisplayConfiguration(&configuration);
    if (result == kCGErrorSuccess) result = CGConfigureDisplayMirrorOfDisplay(configuration, displayID, kCGNullDirectDisplay);
    if (result == kCGErrorSuccess) result = CGConfigureDisplayOrigin(configuration, displayID, (int32_t)right, 0);
    if (result == kCGErrorSuccess) result = CGCompleteDisplayConfiguration(configuration, kCGConfigureForSession);
    else if (configuration) CGCancelDisplayConfiguration(configuration);
    [MossVirtualDisplay logState:@"configured" display:displayID];
    if (result != kCGErrorSuccess || CGDisplayIsOnline(displayID) != 1 || CGDisplayIsActive(displayID) != 1 ||
        CGDisplayIsInMirrorSet(displayID) != 0) {
      [self fail:[NSString stringWithFormat:@"Could not configure Moss as an extended desktop (CoreGraphics %d).", result] completion:completion];
      return;
    }
    CGDisplayModeRef current = CGDisplayCopyDisplayMode(displayID);
    const BOOL exact = current && CGDisplayModeGetWidth(current) == size && CGDisplayModeGetHeight(current) == size &&
        CGDisplayModeGetPixelWidth(current) == size && CGDisplayModeGetPixelHeight(current) == size;
    if (current) CGDisplayModeRelease(current);
    if (!exact) {
      [self fail:@"macOS did not retain the requested Moss desktop mode." completion:completion];
      return;
    }
    _desktopSize = size;
    _backingSize = size;
    NSLog(@"Virtual display ready: id=%u desktop=%ux%u backing=%ux%u origin=(%.0f,0) extended=YES", displayID, size, size, size, size, right);
    completion(nil);
    return;
  }
  if (attempt >= 30) {
    [MossVirtualDisplay logState:@"attach timeout" display:displayID];
    if (size == 800) [self useCompatibilityModeWithCompletion:completion];
    else [self fail:@"macOS did not activate the Moss virtual display." completion:completion];
    return;
  }
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    [self prepareSize:size generation:generation attempt:attempt + 1 selected:selected completion:completion];
  });
}
- (void)remove {
  NSAssert([NSThread isMainThread], @"Display lifecycle must run on the main thread");
  ++_generation;
  _desktopSize = 0;
  _backingSize = 0;
  _display = nil; // CoreGraphics unregisters the display when its owner releases it.
}
@end
