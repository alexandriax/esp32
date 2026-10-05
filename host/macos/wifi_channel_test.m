#import "WiFiChannel.h"
#import <Foundation/Foundation.h>
#include <stdlib.h>

static MossWiFiChannel *channel;
static BOOL connected, sent;
static unsigned lines;
static void finish(BOOL passed, NSString *reason) {
  fprintf(passed ? stdout : stderr, "%s: %s\n", passed ? "PASS" : "FAIL", reason.UTF8String);
  [channel stop];
  exit(passed ? 0 : 1);
}
int main(int argc, const char *argv[]) {
  @autoreleasepool {
    if (argc != 4) return 2;
    NSString *port = @(argv[1]), *hex = @(argv[2]), *mode = @(argv[3]);
    NSMutableData *pin = [NSMutableData data];
    for (NSUInteger i = 0; i + 1 < hex.length; i += 2) {
      unsigned value = 0;
      [[NSScanner scannerWithString:[hex substringWithRange:NSMakeRange(i, 2)]] scanHexInt:&value];
      uint8_t byte = (uint8_t)value;
      [pin appendBytes:&byte length:1];
    }
    channel = [[MossWiFiChannel alloc] init];
    channel.failureHandler = ^(NSString *reason) {
      BOOL transfer = [mode isEqualToString:@"transfer-disconnect"] && connected && sent;
      BOOL invalidLine = [mode hasPrefix:@"line-"] && connected &&
        ([reason containsString:@"control response"]);
      finish(transfer || invalidLine, reason);
    };
    channel.lineHandler = ^(NSString *line) {
      if (![mode isEqualToString:@"stream-lines"] && ![mode isEqualToString:@"stop-on-line"])
        finish(NO, @"Unexpected control line");
      NSArray *expected = @[@"DISPLAY CAPS 0123456789abcdef 31", @"DISPLAY ACK 0123456789abcdef 1"];
      if (lines >= expected.count || ![line isEqualToString:expected[lines++]])
        finish(NO, @"Control lines were corrupted or reordered");
      if ([mode isEqualToString:@"stop-on-line"]) {
        [channel stop];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
          finish(lines == 1, @"Stopping in line callback discards coalesced trailing lines");
        });
      } else if (lines == expected.count) finish(YES, @"Fragmented auth, coalesced metadata and fragmented control lines");
    };
    NSMutableString *token = [[@"a" stringByPaddingToLength:64 withString:@"a" startingAtIndex:0] mutableCopy];
    if ([mode isEqualToString:@"invalid-token"]) [token replaceCharactersInRange:NSMakeRange(0, 1) withString:@"G"];
    [channel connectHost:@"127.0.0.1" port:port fingerprint:pin token:token completion:^(NSError *error) {
      BOOL postAuth = [mode isEqualToString:@"stream-lines"] || [mode isEqualToString:@"stop-on-line"] || [mode hasPrefix:@"line-"];
      if (![mode isEqualToString:@"transfer-disconnect"] && !postAuth) {
        finish(error != nil && !channel.ready, error ? error.localizedDescription : @"Invalid peer response was accepted");
        return;
      }
      if (error || !channel.ready) finish(NO, error.localizedDescription ?: @"Not ready");
      connected = YES;
      if (postAuth) return;
      __block BOOL rejectedOversize = NO;
      [channel sendPacket:[NSMutableData dataWithLength:65537] completion:^(NSError *limitError) {
        rejectedOversize = limitError != nil;
      }];
      if (!rejectedOversize) finish(NO, @"Oversized Wi-Fi batch was accepted");
      NSMutableData *packet = [NSMutableData dataWithLength:65536];
      uint8_t *bytes = packet.mutableBytes;
      for (unsigned i = 0; i < packet.length; ++i) bytes[i] = (uint8_t)i;
      [channel sendPacket:packet completion:^(NSError *sendError) {
        if (sendError) finish(NO, sendError.localizedDescription);
        sent = YES;
      }];
      memset(packet.mutableBytes, 0xff, packet.length);
    }];
    // The channel must snapshot mutable inputs before asynchronous verification.
    memset(pin.mutableBytes, 0, pin.length);
    [token setString:@"changed after connect"];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 18 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
      finish(NO, @"Loopback TLS test timed out");
    });
    dispatch_main();
  }
}
