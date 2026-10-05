#import "AudioConverter.h"
#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <assert.h>
#include <math.h>

static CMSampleBufferRef sample(unsigned frames, BOOL planar, double frequency, unsigned offset, double rate, float amplitude) {
  AudioStreamBasicDescription format = {0};
  format.mSampleRate = rate; format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | (planar ? kAudioFormatFlagIsNonInterleaved : 0);
  format.mBytesPerPacket = format.mBytesPerFrame = 4;
  format.mFramesPerPacket = format.mChannelsPerFrame = 1; format.mBitsPerChannel = 32;
  CMAudioFormatDescriptionRef description = NULL;
  assert(CMAudioFormatDescriptionCreate(NULL, &format, 0, NULL, 0, NULL, NULL, &description) == noErr);
  CMBlockBufferRef block = NULL;
  assert(CMBlockBufferCreateWithMemoryBlock(NULL, NULL, frames * 4, NULL, NULL, 0, frames * 4, 0, &block) == noErr);
  NSMutableData *samples = [NSMutableData dataWithLength:frames * 4];
  float *values = samples.mutableBytes;
  for (unsigned i = 0; i < frames; ++i) values[i] = amplitude * sin(2 * M_PI * frequency * (offset + i) / rate);
  assert(CMBlockBufferReplaceDataBytes(samples.bytes, block, 0, samples.length) == noErr);
  CMSampleBufferRef result = NULL;
  assert(CMAudioSampleBufferCreateReadyWithPacketDescriptions(NULL, block, description, frames, CMTimeMake(offset, (int32_t)rate), NULL, &result) == noErr);
  CFRelease(description); CFRelease(block); return result;
}
static double rms(NSData *pcm) {
  const uint8_t *bytes = pcm.bytes;
  double squares = 0;
  const NSUInteger frames = pcm.length / 2;
  assert(frames > 1000);
  for (NSUInteger i = 1000; i < frames; ++i) {
    int16_t value = (int16_t)((uint16_t)bytes[i * 2] | (uint16_t)bytes[i * 2 + 1] << 8);
    double normalized = value / 32768.0; squares += normalized * normalized;
  }
  return sqrt(squares / (frames - 1000));
}
static NSData *tone(BOOL planar, double frequency) {
  MossAudioConverter *converter = [[MossAudioConverter alloc] init];
  NSMutableData *output = [NSMutableData data];
  for (unsigned offset = 0; offset < 48000; offset += 480) {
    CMSampleBufferRef input = sample(480, planar, frequency, offset, 48000, 0.5);
    NSError *error = nil;
    NSData *pcm = [converter convertSample:input error:&error];
    assert(pcm && !error && pcm.length <= (160 + 256) * 2 && !(pcm.length & 1));
    [output appendData:pcm]; CFRelease(input);
  }
  assert(output.length / 2 >= 15900 && output.length / 2 <= 16000);
  return output;
}
int main(void) {
  @autoreleasepool {
    NSData *planar = tone(YES, 1000), *interleaved = tone(NO, 1000), *aliased = tone(YES, 12000);
    assert([planar isEqual:interleaved]);
    assert(rms(planar) > 0.34 && rms(planar) < 0.37);
    assert(rms(aliased) < 0.005); // Anti-alias filtering, not every-third-sample decimation.
    // Irregular SCK-sized chunks may flush buffered filter output. Bound each
    // conversion, and verify the production100ms transport chunking preserves it.
    MossAudioConverter *irregular = [[MossAudioConverter alloc] init];
    NSUInteger offset = 0, converted = 0;
    for (unsigned i = 0; i < 100; ++i) {
      const unsigned count = i % 3 == 0 ? 4800 : 1 + (i * 1279) % 4800;
      CMSampleBufferRef input = sample(count, YES, 1000, (unsigned)offset, 48000, 0.5);
      NSData *data = [irregular convertSample:input error:NULL];
      assert(data && data.length <= 3712 && !(data.length & 1));
      NSMutableData *rejoined = [NSMutableData data];
      for (NSUInteger start = 0; start < data.length; start += 3200) {
        NSData *chunk = [data subdataWithRange:NSMakeRange(start, MIN((NSUInteger)3200, data.length - start))];
        assert(chunk.length <= 3200 && !(chunk.length & 1)); [rejoined appendData:chunk];
      }
      assert([rejoined isEqual:data]); converted += data.length / 2;
      offset += count; CFRelease(input);
    }
    assert(converted <= (offset + 2) / 3 && offset / 3 - converted < 512);
    MossAudioConverter *converter = [[MossAudioConverter alloc] init];
    NSError *error = nil;
    assert(![converter convertSample:NULL error:&error] && error);
    CMSampleBufferRef huge = sample(4801, YES, 1000, 0, 48000, 0.5);
    assert(![converter convertSample:huge error:NULL]); CFRelease(huge);
    CMSampleBufferRef wrongRate = sample(480, YES, 1000, 0, 44100, 0.5);
    assert(![converter convertSample:wrongRate error:NULL]); CFRelease(wrongRate);
    CMSampleBufferRef nonfinite = sample(4800, YES, 1000, 0, 48000, NAN);
    NSData *silent = [converter convertSample:nonfinite error:NULL];
    assert(silent.length > 0);
    for (NSUInteger i = 0; i < silent.length; ++i) assert(((const uint8_t *)silent.bytes)[i] == 0);
    CFRelease(nonfinite);
    printf("Audio converter passed: Float32 mono planar/interleaved48k→PCM16LE16k, %lu samples/s, RMS %.5f,12kHz stopband %.6f, bounded invalid/nonfinite input\n", (unsigned long)planar.length / 2, rms(planar), rms(aliased));
  }
}
