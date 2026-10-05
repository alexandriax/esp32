#import <Foundation/Foundation.h>
#include <stdint.h>

// Untrusted Bonjour endpoint metadata. The pairing store + TLS certificate pin,
// never these advertisements, determine whether a device is trusted.
@interface MossDiscoveredDevice : NSObject
@property(nonatomic, readonly, copy) NSString *deviceID;
@property(nonatomic, readonly) uint64_t nonce;
@property(nonatomic, readonly, copy) NSString *host; // Resolved numeric address.
@property(nonatomic, readonly, copy) NSString *port;
@end

// Validates bounded TXT metadata independently of network discovery (also used by tests).
#ifdef __cplusplus
extern "C" {
#endif
NSDictionary<NSString *, NSString *> *MossParseDeviceTXT(NSData *data);
#ifdef __cplusplus
}
#endif
@interface MossDeviceDiscovery : NSObject
@property(nonatomic, copy) void (^foundHandler)(MossDiscoveredDevice *device);
@property(nonatomic, copy) void (^removedHandler)(MossDiscoveredDevice *device);
@property(nonatomic, copy) void (^failureHandler)(NSString *reason);
- (void)start;
- (void)stop;
@end
