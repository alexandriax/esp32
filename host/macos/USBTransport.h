#import <Foundation/Foundation.h>
#include <stdint.h>

// Main-queue state machine: discover Espressif USB serial devices, then wait for
// a firmware REQUEST before any binary transmission. One bounded batch is in
// flight (one USB strip or up to 30 Wi-Fi strips / 64 KiB), with one latest frame retained.
@interface MossUSBTransport : NSObject
@property(nonatomic, copy) void (^requestHandler)(uint64_t session);
@property(nonatomic, copy) void (^readyHandler)(uint64_t session);
@property(nonatomic, copy) void (^endedHandler)(NSString *reason);
@property(nonatomic, copy) void (^statusHandler)(NSString *status);
@property(nonatomic, copy) void (^capabilitiesHandler)(uint32_t flags);
@property(nonatomic, readonly) uint64_t session;
@property(nonatomic, readonly) BOOL wifiRequested;
@property(nonatomic, readonly) BOOL wifiConnected;
// Called only after a physical Wi-Fi Display selection, after privacy approval.
@property(nonatomic, copy) void (^wifiSetupHandler)(void);
- (void)configureWiFiSSID:(NSString *)ssid password:(NSString *)password;
// Adaptive JPEG is limited to beneficial Fast240 Wi-Fi frames and followed by
// an exact lossless refresh after motion settles. Disable to always stay lossless.
@property(nonatomic) BOOL allowsLossyCompression;
@property(nonatomic) BOOL audioEnabled;
@property(nonatomic, readonly) BOOL audioActive;
@property(nonatomic) NSUInteger audioVolume; // 0..60, default35.
// Device button changes update the desired volume without enabling audio or
// echoing configuration. Called on the main queue, including while waiting.
@property(nonatomic, copy) void (^audioVolumeHandler)(NSUInteger volume);
@property(nonatomic, copy) void (^audioStatusHandler)(BOOL active, NSString *message);
- (void)submitAudio:(NSData *)pcm; // PCM16LE mono16k, at most100ms per callback.
- (NSArray<NSString *> *)pairedDeviceIDs;
- (BOOL)forgetPairedDevice:(NSString *)deviceID error:(NSError **)error;
// CAPS bits: 0 RLE, 1 Fast240, 2 LZ4, 3 cropped rectangles, 4 JPEG240.
// Zero until negotiated; legacy firmware keeps native raw compatibility.
@property(nonatomic, readonly) uint32_t capabilities;
// 480 by default. Setting240 requires capability bit1; unsupported/invalid
// selections are ignored. Changing size invalidates the acknowledged image.
@property(nonatomic) NSUInteger transferSize;
- (void)start;
- (void)stop;
- (void)acceptRequest:(uint64_t)session;
- (void)reportHostStatus:(uint8_t)status;
- (void)endSession:(NSString *)reason;
// Immutable snapshot of RGB565LE pixels matching transferSize squared.
- (void)submitFrame:(NSData *)pixels;
@end
