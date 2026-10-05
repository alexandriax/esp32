#import "ImageCodec.h"
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void Store(uint8_t *bytes, NSUInteger index, uint16_t pixel) {
  bytes[index * 2] = pixel; bytes[index * 2 + 1] = pixel >> 8;
}
static NSData *Decode(NSData *jpeg) {
  const uint8_t *encoded = jpeg.bytes;
  assert(jpeg.length >= 4 && encoded[0] == 0xff && encoded[1] == 0xd8);
  for (NSUInteger at = 2; at + 4 <= jpeg.length;) {
    assert(encoded[at++] == 0xff);
    while (at < jpeg.length && encoded[at] == 0xff) ++at;
    assert(at < jpeg.length);
    const uint8_t marker = encoded[at++];
    assert(!(marker >= 0xe1 && marker <= 0xef) && marker != 0xfe);
    if (marker == 0xda) break;
    assert(at + 2 <= jpeg.length);
    const NSUInteger length = ((NSUInteger)encoded[at] << 8) | encoded[at + 1];
    assert(length >= 2 && length <= jpeg.length - at);
    if (marker == 0xc0) {
      assert(length == 17 && encoded[at + 7] == 3);
      assert(encoded[at + 8] == 1 && encoded[at + 9] == 0x22 && encoded[at + 10] <= 1);
      assert(encoded[at + 11] == 2 && encoded[at + 12] == 0x11 && encoded[at + 13] <= 1);
      assert(encoded[at + 14] == 3 && encoded[at + 15] == 0x11 && encoded[at + 16] <= 1);
    }
    at += length;
  }
  CGImageSourceRef source = CGImageSourceCreateWithData((__bridge CFDataRef)jpeg, NULL);
  assert(source && CGImageSourceGetCount(source) == 1);
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, NULL);
  assert(image && CGImageGetWidth(image) == 240 && CGImageGetHeight(image) == 240);
  NSMutableData *pixels = [NSMutableData dataWithLength:240 * 240 * 4];
  CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(pixels.mutableBytes, 240, 240, 8, 240 * 4,
      colors, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
  assert(context);
  CGContextDrawImage(context, CGRectMake(0, 0, 240, 240), image);
  CGContextRelease(context); CGColorSpaceRelease(colors); CGImageRelease(image); CFRelease(source);
  return pixels;
}
int main(void) {
  @autoreleasepool {
    assert(!MossEncodeJPEG240(nil, .5));
    assert(!MossEncodeJPEG240([NSData data], .5));
    assert(!MossEncodeJPEG240([NSData dataWithBytes:"x" length:1], .5));
    NSMutableData *input = [NSMutableData dataWithLength:240 * 240 * 2];
    assert(!MossEncodeJPEG240(input, -.1));
    assert(!MossEncodeJPEG240(input, 1.1));
    assert(!MossEncodeJPEG240(input, NAN));
    assert(!MossEncodeJPEG240(input, INFINITY));
    assert(!MossEncodeJPEG240([NSMutableData dataWithLength:480 * 480 * 2], .5));
    uint8_t *bytes = input.mutableBytes;
    for (NSUInteger y = 0; y < 240; ++y) for (NSUInteger x = 0; x < 240; ++x)
      Store(bytes, y * 240 + x, x < 80 ? 0xf800 : x < 160 ? 0x07e0 : 0x001f);
    NSData *jpeg = MossEncodeJPEG240(input, .7);
    assert(jpeg && jpeg.length <= 15360);
    NSData *decoded = Decode(jpeg);
    const uint8_t *rgb = decoded.bytes;
    for (unsigned color = 0; color < 3; ++color) {
      const uint8_t *pixel = rgb + (120 * 240 + 40 + color * 80) * 4;
      for (unsigned channel = 0; channel < 3; ++channel)
        assert(channel == color ? pixel[channel] > 235 : pixel[channel] < 20);
    }
    // The returned packet cannot change when its mutable RGB565 source changes.
    NSData *retained = [jpeg copy];
    memset(input.mutableBytes, 0, input.length);
    assert([jpeg isEqualToData:retained]);
    assert(MossEncodeJPEG240(input, 0));
    // ImageIO quality 1 emits 4:4:4 (11/11/11 sampling), outside the device's
    // bounded 4:2:0 decoder profile. The encoder must request lossless fallback.
    assert(!MossEncodeJPEG240(input, 1));
    // Detailed synthetic motion stays compact, while full random noise must
    // exceed one packet at production quality and select the lossless fallback.
    for (NSUInteger y = 0; y < 240; ++y) for (NSUInteger x = 0; x < 240; ++x) {
      const uint8_t v = (uint8_t)(128 + 45 * sin(x * .11) + 35 * cos(y * .083));
      Store(bytes, y * 240 + x, ((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
    }
    NSData *texture = MossEncodeJPEG240(input, .5);
    assert(texture && texture.length <= 15360);
    (void)Decode(texture);
    uint32_t random = 0x6a09e667;
    for (NSUInteger i = 0; i < 240 * 240; ++i) {
      random ^= random << 13; random ^= random >> 17; random ^= random << 5;
      Store(bytes, i, random);
    }
    assert(!MossEncodeJPEG240(input, .5));
    puts("JPEG240 codec: baseline/dimensions/colors/limits/invalid inputs/immutable payload passed");
  }
  return 0;
}
