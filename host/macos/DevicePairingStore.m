#import "DevicePairingStore.h"
#import <Security/Security.h>

static NSString *const Service = @"org.moss.usb-display.devices";
static BOOL LowerHex(NSString *value, NSUInteger length) {
  return [value isKindOfClass:NSString.class] && value.length == length &&
    [value rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet]].location == NSNotFound;
}
BOOL MossDeviceIDValid(NSString *deviceID) { return LowerHex(deviceID, 32); }
static BOOL PairingValid(NSString *deviceID, NSData *fingerprint, NSString *token) {
  return MossDeviceIDValid(deviceID) && [fingerprint isKindOfClass:NSData.class] && fingerprint.length == 32 && LowerHex(token, 64);
}
static BOOL StoreError(OSStatus status, NSError **error) {
  if (status == errSecSuccess) return YES;
  if (error) {
    NSString *message = @"The Mac Keychain could not access this paired Moss. Try unlocking your login Keychain.";
    if (status == errSecItemNotFound) message = @"This Moss is not paired. Connect it by USB and choose Wi-Fi Display to pair it.";
    else if (status == errSecParam || status == errSecDecode) message = @"The Moss pairing information is invalid. Pair it again over USB.";
    else if (status == errSecAuthFailed || status == errSecUserCanceled) message = @"Keychain access to this paired Moss was not allowed.";
    *error = [NSError errorWithDomain:@"MossDevicePairingStore" code:status userInfo:@{NSLocalizedDescriptionKey:message}];
  }
  return NO;
}
@interface MossDevicePairing ()
- (instancetype)initWithDevice:(NSString *)deviceID fingerprint:(NSData *)fingerprint token:(NSString *)token;
@end
@implementation MossDevicePairing
- (instancetype)initWithDevice:(NSString *)deviceID fingerprint:(NSData *)fingerprint token:(NSString *)token {
  if ((self = [super init])) { _deviceID = [deviceID copy]; _fingerprint = [fingerprint copy]; _token = [token copy]; }
  return self;
}
@end
@implementation MossDevicePairingStore {
  NSString *_service;
}
- (instancetype)init { return [self initWithService:Service]; }
- (instancetype)initWithService:(NSString *)service {
  BOOL valid = [service isEqualToString:Service] || [service hasPrefix:[Service stringByAppendingString:@".test."]];
  if (!valid) return nil;
  if ((self = [super init])) _service = [service copy];
  return self;
}
- (NSMutableDictionary *)query:(NSString *)deviceID {
  NSMutableDictionary *query = [@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
                                  (__bridge id)kSecAttrService:_service} mutableCopy];
  if (deviceID) query[(__bridge id)kSecAttrAccount] = deviceID;
  return query;
}
- (NSArray<NSString *> *)deviceIDs:(NSError **)error {
  NSMutableDictionary *query = [self query:nil];
  query[(__bridge id)kSecReturnAttributes] = @YES;
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitAll;
  CFTypeRef result = NULL;
  OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (status == errSecItemNotFound) return @[];
  if (!StoreError(status, error)) { if (result) CFRelease(result); return nil; }
  NSArray *records = CFBridgingRelease(result);
  NSMutableArray<NSString *> *ids = [NSMutableArray array];
  for (NSDictionary *record in records) {
    NSString *deviceID = record[(__bridge id)kSecAttrAccount];
    if (MossDeviceIDValid(deviceID)) [ids addObject:deviceID];
  }
  return [ids sortedArrayUsingSelector:@selector(compare:)];
}
- (MossDevicePairing *)pairingForDevice:(NSString *)deviceID error:(NSError **)error {
  if (!MossDeviceIDValid(deviceID)) { StoreError(errSecParam, error); return nil; }
  NSMutableDictionary *query = [self query:deviceID];
  query[(__bridge id)kSecReturnData] = @YES;
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
  CFTypeRef result = NULL;
  OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (!StoreError(status, error)) { if (result) CFRelease(result); return nil; }
  NSData *data = CFBridgingRelease(result);
  if (![data isKindOfClass:NSData.class] || data.length > 1024) { StoreError(errSecDecode, error); return nil; }
  id object = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL error:NULL];
  if (![object isKindOfClass:NSDictionary.class] || ![object[@"version"] isEqual:@1] ||
      !PairingValid(deviceID, object[@"fingerprint"], object[@"token"])) {
    StoreError(errSecDecode, error); return nil;
  }
  return [[MossDevicePairing alloc] initWithDevice:deviceID fingerprint:object[@"fingerprint"] token:object[@"token"]];
}
- (BOOL)saveDevice:(NSString *)deviceID fingerprint:(NSData *)fingerprint token:(NSString *)token error:(NSError **)error {
  if (!PairingValid(deviceID, fingerprint, token)) return StoreError(errSecParam, error);
  NSData *data = [NSPropertyListSerialization dataWithPropertyList:@{@"version":@1, @"fingerprint":fingerprint, @"token":token}
                           format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
  if (!data) return StoreError(errSecDecode, error);
  NSMutableDictionary *query = [self query:deviceID];
  OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)@{(__bridge id)kSecValueData:data});
  if (status == errSecItemNotFound) {
    query[(__bridge id)kSecValueData] = data;
    query[(__bridge id)kSecAttrLabel] = @"Moss Display paired device";
    query[(__bridge id)kSecAttrDescription] = @"USB-trusted certificate pin and authentication token for wireless Moss Display";
    status = SecItemAdd((__bridge CFDictionaryRef)query, NULL);
  }
  return StoreError(status, error);
}
- (BOOL)forgetDevice:(NSString *)deviceID error:(NSError **)error {
  if (!MossDeviceIDValid(deviceID)) return StoreError(errSecParam, error);
  OSStatus status = SecItemDelete((__bridge CFDictionaryRef)[self query:deviceID]);
  return StoreError(status == errSecItemNotFound ? errSecSuccess : status, error);
}
@end
