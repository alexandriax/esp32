#import "ImageCodec.h"
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include <math.h>

static NSData *StripMetadata(NSData *data) {
  const uint8_t *bytes = data.bytes;
  if (data.length < 4 || bytes[0] != 0xff || bytes[1] != 0xd8) return nil;
  NSMutableData *clean = [NSMutableData dataWithBytes:bytes length:2];
  for (NSUInteger at = 2; at + 4 <= data.length;) {
    const NSUInteger start = at;
    if (bytes[at++] != 0xff) return nil;
    while (at < data.length && bytes[at] == 0xff) ++at;
    if (at >= data.length) return nil;
    const uint8_t marker = bytes[at++];
    if (marker == 0xd8 || marker == 0xd9 || at + 2 > data.length) return nil;
    const NSUInteger length = ((NSUInteger)bytes[at] << 8) | bytes[at + 1];
    if (length < 2 || length > data.length - at) return nil;
    if (marker == 0xda) {
      // Preserve SOS and entropy bytes exactly, including byte stuffing/EOI.
      [clean appendBytes:bytes + start length:data.length - start];
      return clean;
    }
    const BOOL metadata = (marker >= 0xe1 && marker <= 0xef) || marker == 0xfe;
    if (!metadata) [clean appendBytes:bytes + start length:at + length - start];
    at += length;
  }
  return nil;
}

static BOOL IsBaseline240(NSData *data) {
  const uint8_t *bytes = data.bytes;
  if (data.length < 4 || bytes[0] != 0xff || bytes[1] != 0xd8) return NO;
  for (NSUInteger at = 2; at + 4 <= data.length;) {
    if (bytes[at++] != 0xff) return NO;
    while (at < data.length && bytes[at] == 0xff) ++at;
    if (at >= data.length) return NO;
    const uint8_t marker = bytes[at++];
    if (marker == 0xc2 || marker == 0xda || at + 2 > data.length) return NO;
    const NSUInteger length = ((NSUInteger)bytes[at] << 8) | bytes[at + 1];
    if (length < 2 || length > data.length - at) return NO;
    if (marker == 0xc0) {
      return length == 17 && bytes[at + 2] == 8 &&
          (((NSUInteger)bytes[at + 3] << 8) | bytes[at + 4]) == 240 &&
          (((NSUInteger)bytes[at + 5] << 8) | bytes[at + 6]) == 240 && bytes[at + 7] == 3 &&
          bytes[at + 8] == 1 && bytes[at + 9] == 0x22 && bytes[at + 10] <= 1 &&
          bytes[at + 11] == 2 && bytes[at + 12] == 0x11 && bytes[at + 13] <= 1 &&
          bytes[at + 14] == 3 && bytes[at + 15] == 0x11 && bytes[at + 16] <= 1;
    }
    at += length;
  }
  return NO;
}

NSData *MossEncodeJPEG240(NSData *rgb565, double quality) {
  if (rgb565.length != 240 * 240 * 2 || !isfinite(quality) || quality < 0 || quality > 1) return nil;
  NSMutableData *rgb = [NSMutableData dataWithLength:240 * 240 * 3];
  const uint8_t *source = rgb565.bytes;
  uint8_t *destination = rgb.mutableBytes;
  for (NSUInteger i = 0; i < 240 * 240; ++i) {
    const uint16_t pixel = source[2 * i] | ((uint16_t)source[2 * i + 1] << 8);
    const uint8_t red = (pixel >> 11) & 31, green = (pixel >> 5) & 63, blue = pixel & 31;
    destination[3 * i] = (red << 3) | (red >> 2);
    destination[3 * i + 1] = (green << 2) | (green >> 4);
    destination[3 * i + 2] = (blue << 3) | (blue >> 2);
  }
  CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  if (!colors) return nil;
  CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)rgb);
  CGImageRef image = provider ? CGImageCreate(240, 240, 8, 24, 240 * 3, colors,
      (CGBitmapInfo)kCGImageAlphaNone, provider, NULL, false, kCGRenderingIntentDefault) : NULL;
  if (provider) CGDataProviderRelease(provider);
  CGColorSpaceRelease(colors);
  if (!image) return nil;
  NSMutableData *encoded = [NSMutableData data];
  CGImageDestinationRef output = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)encoded,
      CFSTR("public.jpeg"), 1, NULL);
  BOOL success = NO;
  if (output) {
    NSDictionary *options = @{
      (__bridge NSString *)kCGImageDestinationLossyCompressionQuality:@(quality),
      (__bridge NSString *)kCGImagePropertyJFIFDictionary:@{
        (__bridge NSString *)kCGImagePropertyJFIFIsProgressive:@NO}
    };
    CGImageDestinationAddImage(output, image, (__bridge CFDictionaryRef)options);
    success = CGImageDestinationFinalize(output);
    CFRelease(output);
  }
  CGImageRelease(image);
  if (!success) return nil;
  // JPEGDEC consumes a restricted baseline subset. Drop ImageIO's EXIF and
  // other metadata so its optional metadata/orientation parsers are not entered.
  NSData *clean = StripMetadata(encoded);
  if (!clean.length || clean.length > 15360 || !IsBaseline240(clean)) return nil;
  return [clean copy];
}
