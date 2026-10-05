#import "NetworkStore.h"
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <assert.h>

static NSString *service;
static NSString *otherService;
static void cleanup(void) {
  for (NSString *name in @[service, otherService]) {
    // Each name is a fresh test UUID namespace, never the production service.
    assert([name hasPrefix:@"org.moss.usb-display.networks.test."]);
    NSDictionary *query = @{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
                            (__bridge id)kSecAttrService:name};
    SecItemDelete((__bridge CFDictionaryRef)query);
  }
}
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Network store assertion failed at line %d\n", __LINE__); cleanup(); return 1; } } while (0)
int main(void) {
  @autoreleasepool {
    service = [@"org.moss.usb-display.networks.test." stringByAppendingString:NSUUID.UUID.UUIDString];
    otherService = [service stringByAppendingString:@".other"];
    MossNetworkStore *store = [[MossNetworkStore alloc] initWithService:service];
    MossNetworkStore *other = [[MossNetworkStore alloc] initWithService:otherService];
    NSError *error = nil;
    CHECK([store networkNames:&error].count == 0 && !error);
    CHECK([store preferredNetwork:&error] == nil && !error);
    CHECK(!MossNetworkCredentialsValid(@"", @"password"));
    CHECK(!MossNetworkCredentialsValid(@"Test", @"short"));
    CHECK(!MossNetworkCredentialsValid([@"🌿" stringByPaddingToLength:18 withString:@"🌿" startingAtIndex:0], @"password"));
    CHECK(MossNetworkCredentialsValid(@"Test", @""));
    CHECK(MossNetworkCredentialsValid(@"Test", @"password"));
    CHECK([store saveNetwork:@"Test 🌿" password:@"fixture-password" error:&error]);
    CHECK([store saveNetwork:@"Open fixture" password:@"" error:&error]);
    CHECK([other saveNetwork:@"Other fixture" password:@"unrelated-test" error:&error]);
    CHECK([store networkNames:&error].count == 2);
    CHECK(![[store networkNames:&error] containsObject:@"Other fixture"]);
    CHECK([[store passwordForNetwork:@"Test 🌿" error:&error] isEqualToString:@"fixture-password"]);
    CHECK([[store passwordForNetwork:@"Open fixture" error:&error] isEqualToString:@""]);
    CHECK([store saveNetwork:@"Test 🌿" password:@"replacement" error:&error]);
    CHECK([store networkNames:&error].count == 2);
    CHECK([[store passwordForNetwork:@"Test 🌿" error:&error] isEqualToString:@"replacement"]);
    CHECK([store setPreferredNetwork:@"Test 🌿" error:&error]);
    CHECK([[store preferredNetwork:&error] isEqualToString:@"Test 🌿"]);
    MossNetworkStore *reopened = [[MossNetworkStore alloc] initWithService:service];
    CHECK([[reopened preferredNetwork:&error] isEqualToString:@"Test 🌿"]);
    CHECK([[reopened passwordForNetwork:@"Test 🌿" error:&error] isEqualToString:@"replacement"]);
    CHECK([store setPreferredNetwork:nil error:&error]);
    CHECK(![store preferredNetwork:&error]);
    CHECK([store setPreferredNetwork:@"Open fixture" error:&error]);
    CHECK([store forgetNetwork:@"Open fixture" error:&error]);
    CHECK(![store preferredNetwork:&error]);
    CHECK([store networkNames:&error].count == 1);
    CHECK([store forgetNetwork:@"Test 🌿" error:&error]);
    CHECK([store networkNames:&error].count == 0);
    CHECK([[other passwordForNetwork:@"Other fixture" error:&error] isEqualToString:@"unrelated-test"]);
    CHECK([store forgetNetwork:@"Missing fixture" error:&error]);
    CHECK(![store saveNetwork:@"Invalid" password:@"short" error:&error] && error);
    error = nil;
    CHECK(![store setPreferredNetwork:@"Missing fixture" error:&error] && error);
    cleanup();
    puts("Network store passed: scoped Keychain CRUD, Unicode/open networks, preferred selection, reopening, forgetting and namespace isolation");
  }
}
