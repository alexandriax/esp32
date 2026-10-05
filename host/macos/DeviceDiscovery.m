#import "DeviceDiscovery.h"
#import "DevicePairingStore.h"
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

NSDictionary<NSString *, NSString *> *MossParseDeviceTXT(NSData *data) {
  if (![data isKindOfClass:NSData.class] || !data.length || data.length > 512) return nil;
  // Check TXT framing and duplicate required keys before Foundation builds a map.
  const uint8_t *bytes = data.bytes;
  NSMutableDictionary<NSString *, NSString *> *fields = [NSMutableDictionary dictionary];
  for (NSUInteger offset = 0; offset < data.length;) {
    const NSUInteger length = bytes[offset++];
    if (!length || length > data.length - offset) return nil;
    NSString *entry = [[NSString alloc] initWithBytes:bytes + offset length:length encoding:NSASCIIStringEncoding];
    offset += length;
    if (!entry) return nil;
    NSRange equals = [entry rangeOfString:@"="];
    if (equals.location == NSNotFound) continue;
    NSString *key = [[entry substringToIndex:equals.location] lowercaseString];
    if (![@[@"v", @"id", @"nonce"] containsObject:key]) continue;
    if (fields[key]) return nil;
    fields[key] = [entry substringFromIndex:equals.location + 1];
  }
  NSString *nonce = fields[@"nonce"];
  if (![fields[@"v"] isEqualToString:@"1"] || !MossDeviceIDValid(fields[@"id"]) || nonce.length != 16 ||
      [nonce rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet]].location != NSNotFound ||
      [nonce isEqualToString:@"0000000000000000"]) return nil;
  return [fields copy];
}
@interface MossDiscoveredDevice ()
- (instancetype)initWithID:(NSString *)deviceID nonce:(uint64_t)nonce host:(NSString *)host port:(NSString *)port;
@end
@implementation MossDiscoveredDevice
- (instancetype)initWithID:(NSString *)deviceID nonce:(uint64_t)nonce host:(NSString *)host port:(NSString *)port {
  if ((self = [super init])) { _deviceID = [deviceID copy]; _nonce = nonce; _host = [host copy]; _port = [port copy]; }
  return self;
}
@end
@interface MossDeviceDiscovery () <NSNetServiceBrowserDelegate, NSNetServiceDelegate>
@end
@implementation MossDeviceDiscovery {
  NSNetServiceBrowser *_browser;
  NSMutableSet<NSNetService *> *_services;
  NSMapTable<NSNetService *, MossDiscoveredDevice *> *_published;
}
- (void)start {
  NSAssert(NSThread.isMainThread, @"Discovery runs on the main thread");
  if (_browser) return;
  _services = [NSMutableSet set];
  _published = [NSMapTable strongToStrongObjectsMapTable];
  _browser = [[NSNetServiceBrowser alloc] init];
  _browser.delegate = self;
  [_browser searchForServicesOfType:@"_moss-display._tcp." inDomain:@"local."];
}
- (void)stop {
  NSAssert(NSThread.isMainThread, @"Discovery runs on the main thread");
  _browser.delegate = nil; [_browser stop]; _browser = nil;
  for (NSNetService *service in _services) { service.delegate = nil; [service stopMonitoring]; [service stop]; }
  [_services removeAllObjects]; [_published removeAllObjects];
}
- (void)netServiceBrowser:(NSNetServiceBrowser *)browser didFindService:(NSNetService *)service moreComing:(BOOL)more {
  (void)more;
  if (browser != _browser || _services.count >= 64 || [_services containsObject:service]) return;
  [_services addObject:service]; service.delegate = self;
  [service startMonitoring]; [service resolveWithTimeout:5];
}
- (void)removePublished:(NSNetService *)service {
  MossDiscoveredDevice *previous = [_published objectForKey:service];
  [_published removeObjectForKey:service];
  if (previous && self.removedHandler) self.removedHandler(previous);
}
- (void)netServiceBrowser:(NSNetServiceBrowser *)browser didRemoveService:(NSNetService *)service moreComing:(BOOL)more {
  (void)more;
  if (browser != _browser) return;
  [self removePublished:service];
  service.delegate = nil; [service stopMonitoring]; [service stop]; [_services removeObject:service];
}
- (void)publish:(NSNetService *)service txt:(NSData *)txt {
  if (![_services containsObject:service]) return;
  NSDictionary *fields = MossParseDeviceTXT(txt);
  if (!fields) { [self removePublished:service]; return; }
  if (service.port <= 0 || service.port > 65535) return;
  NSString *host = nil;
  // Prefer IPv4; scoped IPv6 is retained when it is the only usable address.
  for (unsigned pass = 0; pass < 2 && !host; ++pass) {
    for (NSData *address in service.addresses) {
      if (address.length < sizeof(struct sockaddr)) continue;
      const struct sockaddr *socketAddress = address.bytes;
      if (socketAddress->sa_family != (pass ? AF_INET6 : AF_INET)) continue;
      const size_t minimum = pass ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
      if (address.length < minimum || socketAddress->sa_len < minimum || socketAddress->sa_len > address.length) continue;
      char name[NI_MAXHOST];
      if (!getnameinfo(socketAddress, socketAddress->sa_len, name, sizeof(name), NULL, 0, NI_NUMERICHOST)) {
        host = @(name); break;
      }
    }
  }
  if (!host) return;
  unsigned long long nonce = 0;
  [[NSScanner scannerWithString:fields[@"nonce"]] scanHexLongLong:&nonce];
  NSString *port = [NSString stringWithFormat:@"%ld", (long)service.port];
  MossDiscoveredDevice *previous = [_published objectForKey:service];
  if (previous && [previous.deviceID isEqualToString:fields[@"id"]] && previous.nonce == nonce &&
      [previous.host isEqualToString:host] && [previous.port isEqualToString:port]) return;
  if (previous) [self removePublished:service];
  if (![_services containsObject:service]) return; // Removal callback may stop discovery.
  MossDiscoveredDevice *device = [[MossDiscoveredDevice alloc] initWithID:fields[@"id"] nonce:nonce host:host port:port];
  [_published setObject:device forKey:service];
  if (self.foundHandler) self.foundHandler(device);
}
- (void)netServiceDidResolveAddress:(NSNetService *)service { [self publish:service txt:service.TXTRecordData]; }
- (void)netService:(NSNetService *)service didUpdateTXTRecordData:(NSData *)data { [self publish:service txt:data]; }
- (void)netService:(NSNetService *)service didNotResolve:(NSDictionary<NSString *, NSNumber *> *)error {
  (void)error;
  if ([_services containsObject:service]) [self removePublished:service];
}
- (void)netServiceBrowser:(NSNetServiceBrowser *)browser didNotSearch:(NSDictionary<NSString *, NSNumber *> *)error {
  (void)error;
  if (browser == _browser && self.failureHandler) self.failureHandler(@"Moss discovery is unavailable. Check Local Network permission and your network connection.");
}
@end
