#import <Foundation/Foundation.h>

BOOL MossNetworkCredentialsValid(NSString *ssid, NSString *password);

// Exact-service generic-password queries only. Passwords never enter defaults or logs.
@interface MossNetworkStore : NSObject
- (instancetype)init;
- (instancetype)initWithService:(NSString *)service; // Isolated test namespace support.
- (NSArray<NSString *> *)networkNames:(NSError **)error;
- (NSString *)passwordForNetwork:(NSString *)ssid error:(NSError **)error;
- (BOOL)saveNetwork:(NSString *)ssid password:(NSString *)password error:(NSError **)error;
- (BOOL)forgetNetwork:(NSString *)ssid error:(NSError **)error;
- (NSString *)preferredNetwork:(NSError **)error;
- (BOOL)setPreferredNetwork:(NSString *)ssid error:(NSError **)error; // nil disables auto-connect.
@end
