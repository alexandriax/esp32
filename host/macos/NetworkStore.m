#import "NetworkStore.h"
#import <Security/Security.h>

static NSString *const Service = @"org.moss.usb-display.networks";
static NSString *const NetworkPrefix = @"network:";
static NSString *const PreferredAccount = @"preferred-network";

BOOL MossNetworkCredentialsValid(NSString *ssid, NSString *password) {
  NSData *name = [ssid dataUsingEncoding:NSUTF8StringEncoding];
  NSData *secret = [password dataUsingEncoding:NSUTF8StringEncoding];
  return name && secret && name.length > 0 && name.length <= 32 &&
         (!secret.length || (secret.length >= 8 && secret.length <= 63)) &&
         !memchr(name.bytes, 0, name.length) && (!secret.length || !memchr(secret.bytes, 0, secret.length));
}
static BOOL StoreError(OSStatus status, NSError **error) {
  if (status == errSecSuccess) return YES;
  if (error) {
    NSString *message = @"The Mac Keychain could not save or retrieve this network. Try unlocking your login Keychain.";
    if (status == errSecItemNotFound) message = @"This saved network is no longer available. Enter its details again.";
    else if (status == errSecAuthFailed || status == errSecUserCanceled) message = @"Keychain access was not allowed. You can connect without saving this network.";
    else if (status == errSecParam) message = @"Use a network name of 1–32 UTF-8 bytes and a password of 8–63 bytes, or leave the password blank for an open network.";
    *error = [NSError errorWithDomain:@"MossNetworkStore" code:status userInfo:@{NSLocalizedDescriptionKey:message}];
  }
  return NO;
}
@implementation MossNetworkStore {
  NSString *_service;
}
- (instancetype)init { return [self initWithService:Service]; }
- (instancetype)initWithService:(NSString *)service {
  NSParameterAssert([service isEqualToString:Service] || [service hasPrefix:[Service stringByAppendingString:@".test."]]);
  if ((self = [super init])) _service = [service copy];
  return self;
}
- (NSMutableDictionary *)query:(NSString *)account {
  NSMutableDictionary *query = [@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
                                  (__bridge id)kSecAttrService:_service} mutableCopy];
  if (account) query[(__bridge id)kSecAttrAccount] = account;
  return query;
}
- (NSData *)readAccount:(NSString *)account missingOK:(BOOL)missingOK error:(NSError **)error {
  NSMutableDictionary *query = [self query:account];
  query[(__bridge id)kSecReturnData] = @YES;
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
  CFTypeRef value = NULL;
  OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &value);
  if (status == errSecItemNotFound && missingOK) return nil;
  if (!StoreError(status, error)) { if (value) CFRelease(value); return nil; }
  return CFBridgingRelease(value);
}
- (BOOL)writeAccount:(NSString *)account data:(NSData *)data error:(NSError **)error {
  NSMutableDictionary *query = [self query:account];
  NSDictionary *attributes = @{(__bridge id)kSecValueData:data};
  OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)attributes);
  if (status == errSecItemNotFound) {
    query[(__bridge id)kSecValueData] = data;
    query[(__bridge id)kSecAttrLabel] = @"Moss Display saved Wi-Fi network";
    query[(__bridge id)kSecAttrDescription] = @"Local Wi-Fi settings for Moss Display";
    // The ordinary login Keychain protects access with this app's signing identity.
    // No synchronizable attribute: these credentials are not uploaded to iCloud.
    status = SecItemAdd((__bridge CFDictionaryRef)query, NULL);
  }
  return StoreError(status, error);
}
- (BOOL)deleteAccount:(NSString *)account error:(NSError **)error {
  OSStatus status = SecItemDelete((__bridge CFDictionaryRef)[self query:account]);
  return StoreError(status == errSecItemNotFound ? errSecSuccess : status, error);
}
- (NSArray<NSString *> *)networkNames:(NSError **)error {
  NSMutableDictionary *query = [self query:nil];
  query[(__bridge id)kSecReturnAttributes] = @YES; // Listing never retrieves password data.
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitAll;
  CFTypeRef result = NULL;
  OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (status == errSecItemNotFound) return @[];
  if (!StoreError(status, error)) { if (result) CFRelease(result); return nil; }
  NSArray *records = CFBridgingRelease(result);
  NSMutableArray<NSString *> *names = [NSMutableArray array];
  for (NSDictionary *record in records) {
    NSString *account = record[(__bridge id)kSecAttrAccount];
    if ([account hasPrefix:NetworkPrefix]) [names addObject:[account substringFromIndex:NetworkPrefix.length]];
  }
  return [names sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
}
- (NSString *)passwordForNetwork:(NSString *)ssid error:(NSError **)error {
  if (!MossNetworkCredentialsValid(ssid, @"")) { StoreError(errSecParam, error); return nil; }
  NSData *data = [self readAccount:[NetworkPrefix stringByAppendingString:ssid] missingOK:NO error:error];
  if (!data) return nil;
  NSString *password = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  if (!password || !MossNetworkCredentialsValid(ssid, password)) { StoreError(errSecDecode, error); return nil; }
  return password;
}
- (BOOL)saveNetwork:(NSString *)ssid password:(NSString *)password error:(NSError **)error {
  if (!MossNetworkCredentialsValid(ssid, password)) return StoreError(errSecParam, error);
  return [self writeAccount:[NetworkPrefix stringByAppendingString:ssid]
                       data:[password dataUsingEncoding:NSUTF8StringEncoding] error:error];
}
- (NSString *)preferredNetwork:(NSError **)error {
  NSData *data = [self readAccount:PreferredAccount missingOK:YES error:error];
  return data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : nil;
}
- (BOOL)setPreferredNetwork:(NSString *)ssid error:(NSError **)error {
  if (!ssid) return [self deleteAccount:PreferredAccount error:error];
  if (!MossNetworkCredentialsValid(ssid, @"")) return StoreError(errSecParam, error);
  NSArray *names = [self networkNames:error];
  if (!names) return NO;
  if (![names containsObject:ssid]) return StoreError(errSecItemNotFound, error);
  return [self writeAccount:PreferredAccount data:[ssid dataUsingEncoding:NSUTF8StringEncoding] error:error];
}
- (BOOL)forgetNetwork:(NSString *)ssid error:(NSError **)error {
  if (!MossNetworkCredentialsValid(ssid, @"")) return StoreError(errSecParam, error);
  NSError *readError = nil;
  NSString *preferred = [self preferredNetwork:&readError];
  if (readError) { if (error) *error = readError; return NO; }
  if ([preferred isEqualToString:ssid] && ![self deleteAccount:PreferredAccount error:error]) return NO;
  return [self deleteAccount:[NetworkPrefix stringByAppendingString:ssid] error:error];
}
@end
