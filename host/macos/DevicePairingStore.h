#import <Foundation/Foundation.h>

#ifdef __cplusplus
extern "C" {
#endif
BOOL MossDeviceIDValid(NSString *deviceID);
#ifdef __cplusplus
}
#endif

@interface MossDevicePairing : NSObject
@property(nonatomic, readonly, copy) NSString *deviceID;
@property(nonatomic, readonly, copy) NSData *fingerprint;
@property(nonatomic, readonly, copy) NSString *token;
@end

// Only USB-authenticated pairing material may be saved. Bonjour never establishes trust.
// Exact-service login-Keychain access; no secrets in defaults, logs, or iCloud sync.
@interface MossDevicePairingStore : NSObject
- (instancetype)init;
- (instancetype)initWithService:(NSString *)service; // Fresh isolated test namespaces only.
- (NSArray<NSString *> *)deviceIDs:(NSError **)error; // Attributes only, no secret reads.
- (MossDevicePairing *)pairingForDevice:(NSString *)deviceID error:(NSError **)error;
- (BOOL)saveDevice:(NSString *)deviceID fingerprint:(NSData *)fingerprint token:(NSString *)token error:(NSError **)error;
- (BOOL)forgetDevice:(NSString *)deviceID error:(NSError **)error;
@end
