// Synthetic images only: no screen capture, network, device, or user files.
// CSV sizes exclude MOSD/COBS/TLS headers. JPEG is a feasibility comparison;
// it is not a supported wire codec and ESP32-C6 decode speed is unmeasured.
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#include "../host/macos/WireProtocol.hpp"
#import "../host/macos/ImageCodec.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

using Clock = std::chrono::steady_clock;
using Pixels = std::vector<uint8_t>;

static double milliseconds(Clock::time_point began) {
  return std::chrono::duration<double, std::milli>(Clock::now() - began).count();
}
static uint8_t channel(double value) {
  return static_cast<uint8_t>(std::max(0.0, std::min(255.0, value)));
}
static Pixels synthetic(const std::string& workload, unsigned size, unsigned frame) {
  Pixels rgba(size * size * 4, 255);
  if (workload == "textured-pan") {
    // Smooth multiscale terrain/cloud texture. Translate the sample positions
    // between frames, preserving the same underlying synthetic scene.
    for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x) {
      const double u = x * 480.0 / size + frame * 7.0;
      const double v = y * 480.0 / size + frame * 3.0;
      const double coarse = 30 * sin(u * .015 + sin(v * .023)) + 22 * cos(v * .02 - u * .009);
      const double detail = 9 * sin(u * .13 + cos(v * .081)) + 6 * cos(v * .19 + u * .072)
                          + 3 * sin(u * .59 + v * .29) + 2 * cos(v * .91 - u * .51);
      const size_t at = (y * size + x) * 4;
      rgba[at] = channel(100 + coarse + detail);
      rgba[at + 1] = channel(132 + coarse * .75 + detail);
      rgba[at + 2] = channel(151 + coarse * .4 + detail);
    }
    return rgba;
  }
  CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(rgba.data(), size, size, 8, size * 4,
      colors, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
  assert(context);
  CGColorSpaceRelease(colors);
  CGContextScaleCTM(context, size / 480.0, size / 480.0);
  NSGraphicsContext *previous = NSGraphicsContext.currentContext;
  NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
  [[NSColor colorWithCalibratedWhite:.12 alpha:1] setFill];
  NSRectFill(NSMakeRect(0, 0, 480, 480));
  NSDictionary *title = @{NSFontAttributeName:[NSFont systemFontOfSize:24 weight:NSFontWeightSemibold],
      NSForegroundColorAttributeName:NSColor.whiteColor};
  NSDictionary *body = @{NSFontAttributeName:[NSFont monospacedSystemFontOfSize:15 weight:NSFontWeightRegular],
      NSForegroundColorAttributeName:[NSColor colorWithCalibratedWhite:.85 alpha:1]};
  [@"Moss synthetic desktop" drawAtPoint:NSMakePoint(24, 435) withAttributes:title];
  [NSGraphicsContext saveGraphicsState];
  NSRectClip(NSMakeRect(18, 30, 444, 385));
  const CGFloat offset = workload == "scroll-ui" ? frame * 7 : 0;
  for (unsigned row = 0; row < 12; ++row) {
    const CGFloat y = 365 - row * 48 + offset;
    [[NSColor colorWithCalibratedWhite:row % 2 ? .20 : .25 alpha:1] setFill];
    NSRectFill(NSMakeRect(24, y, 432, 42));
    [[NSString stringWithFormat:@"%02u  Notes, tasks, and quiet things", row + 1]
      drawAtPoint:NSMakePoint(33, y + 13) withAttributes:body];
  }
  [NSGraphicsContext restoreGraphicsState];
  if (workload == "cursor-update") {
    const CGFloat x = 190 + frame * 13, y = 260 + frame * 7;
    NSBezierPath *cursor = [NSBezierPath bezierPath];
    [cursor moveToPoint:NSMakePoint(x, y)];
    [cursor lineToPoint:NSMakePoint(x + 2, y - 25)];
    [cursor lineToPoint:NSMakePoint(x + 8, y - 18)];
    [cursor lineToPoint:NSMakePoint(x + 16, y - 29)];
    [cursor lineToPoint:NSMakePoint(x + 21, y - 26)];
    [cursor lineToPoint:NSMakePoint(x + 12, y - 15)];
    [cursor lineToPoint:NSMakePoint(x + 23, y - 14)];
    [cursor closePath];
    [NSColor.whiteColor setFill]; [cursor fill];
    [NSColor.blackColor setStroke]; cursor.lineWidth = 1; [cursor stroke];
  }
  CGContextFlush(context);
  NSGraphicsContext.currentContext = previous;
  CGContextRelease(context);
  return rgba;
}

static Pixels rgb565(const Pixels& rgba) {
  Pixels result(rgba.size() / 2);
  for (size_t i = 0; i < rgba.size() / 4; ++i) {
    const uint16_t pixel = ((rgba[i * 4] >> 3) << 11) |
        ((rgba[i * 4 + 1] >> 2) << 5) | (rgba[i * 4 + 2] >> 3);
    result[i * 2] = pixel; result[i * 2 + 1] = pixel >> 8;
  }
  return result;
}

struct Result { size_t bytes = 0; unsigned packets = 0; double encodeMs = 0; };

static Result lossless(const Pixels& current, const Pixels* previous, unsigned size,
                       unsigned capabilities, bool crop) {
  moss_wire::DeltaFrame baseline;
  baseline.resize(size);
  moss_wire::Band band;
  if (previous) {
    for (unsigned y = 0; baseline.next(previous->data(), previous->size(), y, band); y = band.y + band.height)
      assert(baseline.acknowledge(previous->data(), previous->size(), band));
  }
  Result result;
  for (unsigned y = 0; baseline.next(current.data(), current.size(), y, band, crop); y = band.y + band.height) {
    const auto began = Clock::now();
    const Pixels packed = moss_wire::pack(current.data(), current.size(), size, band);
    const auto encoded = moss_wire::encodePixels(packed.data(), packed.size(), size, capabilities);
    result.encodeMs += milliseconds(began);
    assert(!encoded.payload.empty());
    if (encoded.type == moss_wire::Lz4Rect || encoded.type == moss_wire::ScaledLz4Rect) {
      Pixels restored(packed.size());
      assert(LZ4_decompress_safe(reinterpret_cast<const char*>(encoded.payload.data()),
          reinterpret_cast<char*>(restored.data()), static_cast<int>(encoded.payload.size()),
          static_cast<int>(restored.size())) == static_cast<int>(packed.size()));
      assert(restored == packed);
    }
    result.bytes += encoded.payload.size();
    ++result.packets;
  }
  return result;
}

static bool baselineJPEG(NSData* data) {
  const auto* bytes = static_cast<const uint8_t*>(data.bytes);
  if (data.length < 4 || bytes[0] != 0xff || bytes[1] != 0xd8) return false;
  for (size_t at = 2; at + 4 <= data.length;) {
    if (bytes[at++] != 0xff) return false;
    while (at < data.length && bytes[at] == 0xff) ++at;
    if (at >= data.length) return false;
    const uint8_t marker = bytes[at++];
    if (marker == 0xc0) return true;
    if (marker == 0xc2 || marker == 0xda || at + 2 > data.length) return false;
    const size_t length = (static_cast<size_t>(bytes[at]) << 8) | bytes[at + 1];
    if (length < 2 || length > data.length - at) return false;
    at += length;
  }
  return false;
}

static Result jpeg(const Pixels& rgba, unsigned size, double quality, bool strips, NSString* fixture) {
  Result result;
  const unsigned height = strips ? size / 30 : size;
  for (unsigned y = 0; y < size; y += height) {
    const auto began = Clock::now();
    CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr,
        rgba.data() + y * size * 4, size * height * 4, nullptr);
    CGImageRef image = CGImageCreate(size, height, 8, 32, size * 4, colors,
        kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big, provider, nullptr, false,
        kCGRenderingIntentDefault);
    assert(image);
    NSMutableData *data = [NSMutableData data];
    CGImageDestinationRef destination = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data,
        CFSTR("public.jpeg"), 1, nullptr);
    assert(destination);
    NSDictionary *options = @{(__bridge NSString *)kCGImageDestinationLossyCompressionQuality:@(quality),
      (__bridge NSString *)kCGImagePropertyJFIFDictionary:@{(__bridge NSString *)kCGImagePropertyJFIFIsProgressive:@NO}};
    CGImageDestinationAddImage(destination, image, (__bridge CFDictionaryRef)options);
    assert(CGImageDestinationFinalize(destination));
    result.encodeMs += milliseconds(began);
    assert(baselineJPEG(data));
    if (fixture && !strips) assert([data writeToFile:fixture atomically:YES]);
    result.bytes += data.length;
    ++result.packets;
    CFRelease(destination); CGImageRelease(image); CGDataProviderRelease(provider); CGColorSpaceRelease(colors);
  }
  return result;
}

static void output(const std::string& workload, unsigned size, bool warm,
                   const char* codec, Result result, const char* note) {
  printf("%s,%u,%s,%s,%zu,%u,%.3f,%s\n", workload.c_str(), size,
      warm ? "update" : "initial", codec, result.bytes, result.packets, result.encodeMs, note);
}

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    NSString* fixtures = nil;
    if (argc == 3 && !strcmp(argv[1], "--fixtures")) {
      fixtures = [NSString stringWithUTF8String:argv[2]];
      assert([NSFileManager.defaultManager createDirectoryAtPath:fixtures
          withIntermediateDirectories:YES attributes:nil error:nil]);
    } else if (argc != 1) {
      fprintf(stderr, "usage: %s [--fixtures output-directory]\n", argv[0]);
      return 2;
    }
    fprintf(stderr, "Synthetic codec benchmark: payload bytes exclude headers; Mac encoding only. "
        "ESP32-C6 JPEG decoding and end-to-end FPS are UNMEASURED.\n");
    puts("workload,size,phase,codec,payload_bytes,blocks,mac_encode_ms,note");
    for (const std::string workload : {"scroll-ui", "cursor-update", "textured-pan"}) {
      for (const unsigned size : {240u, 480u}) {
        const Pixels before = rgb565(synthetic(workload, size, 0));
        const Pixels rgba = synthetic(workload, size, 1);
        const Pixels current = rgb565(rgba);
        for (const bool warm : {false, true}) {
          const Pixels* previous = warm ? &before : nullptr;
          output(workload, size, warm, "raw", lossless(current, previous, size, 0, false), "changed_rows");
          output(workload, size, warm, "rle", lossless(current, previous, size, moss_wire::Rle, false), "raw_fallback");
          output(workload, size, warm, "lz4", lossless(current, previous, size, moss_wire::Lz4, false), "raw_fallback");
          output(workload, size, warm, "adaptive", lossless(current, previous, size, moss_wire::Rle | moss_wire::Lz4, false), "raw_rle_lz4");
          output(workload, size, warm, "adaptive-crop", lossless(current, previous, size, moss_wire::Rle | moss_wire::Lz4, true), "production_delta_planner");
          for (const double quality : {.5, .7}) {
            if (size == 240) {
              NSData *source = [NSData dataWithBytes:current.data() length:current.size()];
              const auto began = Clock::now();
              NSData *encoded = MossEncodeJPEG240(source, quality);
              const double duration = milliseconds(began);
              if (encoded) assert(baselineJPEG(encoded));
              output(workload, size, warm, quality == .5 ? "jpeg50-production" : "jpeg70-production",
                  Result{encoded.length, encoded ? 1u : 0u, duration},
                  encoded ? "rgb565_baseline_metadata_stripped" : "too_large_use_lossless");
              if (encoded && fixtures) {
                NSString *path = [fixtures stringByAppendingPathComponent:
                    [NSString stringWithFormat:@"%s-%u-q%.0f-production.jpg", workload.c_str(), size, quality * 100]];
                assert([encoded writeToFile:path atomically:YES]);
              }
            }
            const char* codec = quality == .5 ? "jpeg50-frame" : "jpeg70-frame";
            NSString* fixture = fixtures ? [fixtures stringByAppendingPathComponent:
                [NSString stringWithFormat:@"%s-%u-q%.0f.jpg", workload.c_str(), size, quality * 100]] : nil;
            output(workload, size, warm, codec, jpeg(rgba, size, quality, false, fixture), "lossy_full_frame_no_delta_unimplemented");
            codec = quality == .5 ? "jpeg50-strips" : "jpeg70-strips";
            output(workload, size, warm, codec, jpeg(rgba, size, quality, true, nil), "lossy_all_strips_no_delta_unimplemented");
          }
        }
      }
    }
  }
}
