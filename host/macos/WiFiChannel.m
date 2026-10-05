#import "WiFiChannel.h"
#import <Network/Network.h>
#import <Security/Security.h>
#import <CommonCrypto/CommonDigest.h>

static NSError *WiFiError(NSString *message) {
  return [NSError errorWithDomain:@"MossWiFi" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
@implementation MossWiFiChannel {
  nw_connection_t _connection;
  NSUInteger _generation;
  BOOL _ready, _sending;
  NSMutableData *_authReply;
  NSMutableData *_line;
  void (^_completion)(NSError *);
}
- (BOOL)ready { return _ready; }
- (void)stop {
  ++_generation;
  _ready = _sending = NO;
  _completion = nil;
  _authReply = nil;
  _line = nil;
  if (_connection) nw_connection_cancel(_connection);
  _connection = nil;
}
- (void)failed:(NSString *)message {
  void (^completion)(NSError *) = _completion;
  void (^failure)(NSString *) = self.failureHandler;
  const BOOL established = _ready;
  [self stop];
  if (completion) completion(WiFiError(message));
  else if (established && failure) failure(message);
}
- (void)connectHost:(NSString *)host port:(NSString *)port fingerprint:(NSData *)fingerprint
             token:(NSString *)token completion:(void (^)(NSError *))completion {
  [self stop];
  const NSUInteger generation = _generation;
  fingerprint = [fingerprint copy];
  token = [token copy];
  NSCharacterSet *nonHex = [[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"] invertedSet];
  if (fingerprint.length != CC_SHA256_DIGEST_LENGTH || token.length != 64 ||
      [token rangeOfCharacterFromSet:nonHex].location != NSNotFound) {
    completion(WiFiError(@"Invalid USB pairing information")); return;
  }
  _completion = [completion copy];
  _authReply = [NSMutableData data];
  _line = [NSMutableData data];
  // Pin the exact leaf certificate whose fingerprint was trusted through USB.
  // Standard TLS encryption/authentication remains active; no system trust is altered.
  nw_parameters_t parameters = nw_parameters_create_secure_tcp(^(nw_protocol_options_t options) {
    sec_protocol_options_t security = nw_tls_copy_sec_protocol_options(options);
    sec_protocol_options_set_min_tls_protocol_version(security, tls_protocol_version_TLSv12);
    sec_protocol_options_set_verify_block(security,
        ^(sec_protocol_metadata_t metadata, sec_trust_t trust, sec_protocol_verify_complete_t complete) {
      (void)metadata;
      SecTrustRef reference = sec_trust_copy_ref(trust);
      CFArrayRef chain = reference ? SecTrustCopyCertificateChain(reference) : NULL;
      SecCertificateRef leaf = chain && CFArrayGetCount(chain) ? (SecCertificateRef)CFArrayGetValueAtIndex(chain, 0) : NULL;
      CFDataRef der = leaf ? SecCertificateCopyData(leaf) : NULL;
      uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {0};
      BOOL matches = NO;
      if (der) {
        CC_SHA256(CFDataGetBytePtr(der), (CC_LONG)CFDataGetLength(der), digest);
        const uint8_t *expected = fingerprint.bytes;
        unsigned difference = 0;
        for (unsigned i = 0; i < sizeof(digest); ++i) difference |= digest[i] ^ expected[i];
        matches = difference == 0;
      }
      if (der) CFRelease(der);
      if (chain) CFRelease(chain);
      if (reference) CFRelease(reference);
      complete(matches);
    }, dispatch_get_main_queue());
  }, ^(nw_protocol_options_t options) { nw_tcp_options_set_no_delay(options, true); });
  _connection = nw_connection_create(nw_endpoint_create_host(host.UTF8String, port.UTF8String), parameters);
  nw_connection_set_queue(_connection, dispatch_get_main_queue());
  __weak MossWiFiChannel *weakSelf = self;
  nw_connection_set_state_changed_handler(_connection, ^(nw_connection_state_t state, nw_error_t error) {
    (void)error;
    MossWiFiChannel *self = weakSelf;
    if (!self || generation != self->_generation) return;
    if (state == nw_connection_state_ready) {
      NSData *auth = [[NSString stringWithFormat:@"MOSS AUTH %@\n", token] dataUsingEncoding:NSASCIIStringEncoding];
      dispatch_data_t content = dispatch_data_create(auth.bytes, auth.length, dispatch_get_main_queue(), ^{ (void)auth; });
      nw_connection_send(self->_connection, content, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, false, ^(nw_error_t sendError) {
        if (generation != self->_generation) return;
        if (sendError) [self failed:@"Wi-Fi authentication could not be sent"];
        else [self receive:generation];
      });
    } else if (state == nw_connection_state_failed) [self failed:@"Wi-Fi connection failed · check the network and Local Network permission"];
  });
  nw_connection_start(_connection);
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    MossWiFiChannel *self = weakSelf;
    if (self && generation == self->_generation && !self->_ready) [self failed:@"Wi-Fi pairing timed out"];
  });
}
- (void)receive:(NSUInteger)generation {
  nw_connection_receive(_connection, 1, 2048, ^(dispatch_data_t data, nw_content_context_t context, bool complete, nw_error_t error) {
    (void)context;
    if (generation != self->_generation) return;
    if (data && dispatch_data_get_size(data)) {
      static const char expected[] = "MOSS AUTH OK\n";
      const NSUInteger expectedLength = sizeof(expected) - 1;
      dispatch_data_apply(data, ^bool(dispatch_data_t region, size_t offset, const void *bytes, size_t size) {
        (void)region; (void)offset;
        const uint8_t *input = bytes;
        for (size_t i = 0; i < size; ++i) {
          if (generation != self->_generation) return false;
          const uint8_t byte = input[i];
          if (!self->_ready) {
            if (byte != (uint8_t)expected[self->_authReply.length]) {
              [self failed:@"Wi-Fi authentication failed"]; return false;
            }
            [self->_authReply appendBytes:&byte length:1];
            if (self->_authReply.length == expectedLength) {
              self->_ready = YES; self->_authReply = nil;
              void (^completion)(NSError *) = self->_completion; self->_completion = nil;
              if (completion) completion(nil);
            }
          } else if (byte == '\n') {
            NSString *line = [[NSString alloc] initWithData:self->_line encoding:NSASCIIStringEncoding];
            [self->_line setLength:0];
            if (![line hasPrefix:@"DISPLAY "]) {
              [self failed:@"Unexpected Wi-Fi control response"]; return false;
            }
            if (self.lineHandler) self.lineHandler(line);
          } else {
            if (byte < 0x20 || byte > 0x7e || self->_line.length >= 512) {
              [self failed:@"Invalid or oversized Wi-Fi control response"]; return false;
            }
            [self->_line appendBytes:&byte length:1];
          }
        }
        return true;
      });
    }
    if (generation != self->_generation) return;
    if (error || complete) { [self failed:@"Wi-Fi disconnected · choose Wi-Fi Display again"]; return; }
    if (generation == self->_generation) [self receive:generation];
  });
}
- (void)sendPacket:(NSData *)packet completion:(void (^)(NSError *))completion {
  if (!_ready || _sending || !packet.length || packet.length > 65536) {
    completion(WiFiError(@"Wi-Fi transport is not ready")); return;
  }
  _sending = YES;
  const NSUInteger generation = _generation;
  packet = [packet copy];
  dispatch_data_t content = dispatch_data_create(packet.bytes, packet.length, dispatch_get_main_queue(), ^{ (void)packet; });
  nw_connection_send(_connection, content, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, false, ^(nw_error_t error) {
    if (generation != self->_generation) return;
    self->_sending = NO;
    completion(error ? WiFiError(@"Wi-Fi transfer failed") : nil);
  });
}
@end
