#import "DevicePairingStore.h"
#import "DeviceDiscovery.h"
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <assert.h>

static NSString *service, *otherService;
static void cleanup(void) {
  for (NSString *name in @[service, otherService]) {
    assert([name hasPrefix:@"org.moss.usb-display.devices.test."]);
    SecItemDelete((__bridge CFDictionaryRef)@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
                                            (__bridge id)kSecAttrService:name});
  }
}
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Device pairing assertion failed at line %d\n", __LINE__); cleanup(); return 1; } } while (0)
static NSData *TXT(NSDictionary *fields) { return [NSNetService dataFromTXTRecordDictionary:fields]; }
int main(void) {
  @autoreleasepool {
    service = [@"org.moss.usb-display.devices.test." stringByAppendingString:NSUUID.UUID.UUIDString];
    otherService = [service stringByAppendingString:@".other"];
    MossDevicePairingStore *store = [[MossDevicePairingStore alloc] initWithService:service];
    MossDevicePairingStore *other = [[MossDevicePairingStore alloc] initWithService:otherService];
    NSString *deviceID = @"0123456789abcdef0123456789abcdef";
    NSString *otherID = @"fedcba9876543210fedcba9876543210";
    NSMutableData *pin = [NSMutableData dataWithLength:32];
    memset(pin.mutableBytes, 0xab, pin.length);
    NSData *originalPin = [pin copy];
    NSString *token = [@"a" stringByPaddingToLength:64 withString:@"a" startingAtIndex:0];
    NSError *error = nil;
    CHECK(![[MossDevicePairingStore alloc] initWithService:@"unrelated.service"]);
    CHECK([store deviceIDs:&error].count == 0 && !error);
    CHECK(!MossDeviceIDValid(nil) && !MossDeviceIDValid(@"short") && !MossDeviceIDValid(deviceID.uppercaseString));
    CHECK([store saveDevice:deviceID fingerprint:pin token:token error:&error]);
    CHECK([other saveDevice:otherID fingerprint:pin token:token error:&error]);
    memset(pin.mutableBytes, 0xcd, pin.length);
    CHECK([store deviceIDs:&error].count == 1 && ![[store deviceIDs:&error] containsObject:otherID]);
    MossDevicePairing *pairing = [store pairingForDevice:deviceID error:&error];
    CHECK([pairing.deviceID isEqual:deviceID] && [pairing.fingerprint isEqual:originalPin] && [pairing.token isEqual:token]);
    CHECK([store saveDevice:deviceID fingerprint:pin token:token error:&error]);
    MossDevicePairingStore *reopened = [[MossDevicePairingStore alloc] initWithService:service];
    CHECK([[reopened pairingForDevice:deviceID error:&error].fingerprint isEqual:pin]);
    CHECK([pairing.fingerprint isEqual:originalPin]); // Returned snapshot survives update.
    CHECK(![store saveDevice:deviceID fingerprint:[NSData data] token:token error:&error] && error);
    error = nil;
    CHECK(![store saveDevice:deviceID fingerprint:pin token:@"bad" error:&error] && error);
    error = nil;
    CHECK(![store pairingForDevice:otherID error:&error] && error);
    NSDictionary *query = @{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
                            (__bridge id)kSecAttrService:service, (__bridge id)kSecAttrAccount:deviceID};
    CHECK(SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)@{(__bridge id)kSecValueData:[@"malformed" dataUsingEncoding:NSUTF8StringEncoding]}) == errSecSuccess);
    error = nil;
    CHECK(![store pairingForDevice:deviceID error:&error] && error);
    CHECK([store forgetDevice:deviceID error:&error] && [store forgetDevice:deviceID error:&error]);
    CHECK([store deviceIDs:&error].count == 0 && [other deviceIDs:&error].count == 1);

    NSDictionary *valid = @{@"v":@"1", @"id":deviceID, @"nonce":@"0123456789abcdef"};
    CHECK([MossParseDeviceTXT(TXT(valid)) isEqual:valid]);
    for (NSDictionary *invalid in @[@{@"v":@"2", @"id":deviceID, @"nonce":@"0123456789abcdef"},
                                    @{@"v":@"1", @"id":deviceID, @"nonce":@"0000000000000000"},
                                    @{@"v":@"1", @"id":deviceID, @"nonce":@"0123456789abcdeg"},
                                    @{@"v":@"1", @"id":@"short", @"nonce":@"0123456789abcdef"}]) {
      CHECK(!MossParseDeviceTXT(TXT(invalid)));
    }
    CHECK(!MossParseDeviceTXT([NSData data]) && !MossParseDeviceTXT([NSMutableData dataWithLength:513]));
    NSMutableData *duplicate = [TXT(valid) mutableCopy];
    [duplicate appendData:TXT(@{@"V":@"1"})];
    CHECK(!MossParseDeviceTXT(duplicate));
    NSMutableData *truncated = [TXT(valid) mutableCopy]; [truncated setLength:truncated.length - 1];
    CHECK(!MossParseDeviceTXT(truncated));
    cleanup();
    puts("Device pairing passed: scoped Keychain CRUD, immutable snapshots, restart, invalid/corrupt data, namespace isolation, strict Bonjour TXT bounds and identities");
  }
}
