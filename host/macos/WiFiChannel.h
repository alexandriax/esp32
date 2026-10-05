#import <Foundation/Foundation.h>

// TLS peer certificate and authentication token must be trusted through USB pairing.
// Discovery supplies an endpoint only; it never establishes trust.
@interface MossWiFiChannel : NSObject
@property(nonatomic, copy) void (^failureHandler)(NSString *reason);
// Authenticated printable-ASCII DISPLAY lines, excluding the trailing newline.
// Callbacks run on the main queue. Malformed/oversized lines close the channel.
@property(nonatomic, copy) void (^lineHandler)(NSString *line);
@property(nonatomic, readonly) BOOL ready;
- (void)connectHost:(NSString *)host port:(NSString *)port fingerprint:(NSData *)fingerprint
             token:(NSString *)token completion:(void (^)(NSError *error))completion;
- (void)sendPacket:(NSData *)packet completion:(void (^)(NSError *error))completion;
- (void)stop;
@end
