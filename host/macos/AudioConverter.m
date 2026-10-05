#import "AudioConverter.h"
#import <AVFoundation/AVFoundation.h>
#include <math.h>

static NSData *InvalidAudio(NSError **error, NSString *message) {
  if (error) *error = [NSError errorWithDomain:@"MossAudio" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
  return nil;
}
@implementation MossAudioConverter {
  AVAudioConverter *_converter;
  AVAudioFormat *_inputFormat, *_outputFormat;
}
- (NSData *)convertSample:(CMSampleBufferRef)sample error:(NSError **)error {
  if (!sample || !CMSampleBufferIsValid(sample) || !CMSampleBufferDataIsReady(sample))
    return InvalidAudio(error, @"Mac audio sample is unavailable.");
  CMAudioFormatDescriptionRef description = CMSampleBufferGetFormatDescription(sample);
  if (!description || CMFormatDescriptionGetMediaType(description) != kCMMediaType_Audio)
    return InvalidAudio(error, @"Mac audio sample has an unsupported format.");
  const AudioStreamBasicDescription *format = CMAudioFormatDescriptionGetStreamBasicDescription(description);
  const CMItemCount frames = CMSampleBufferGetNumSamples(sample);
  const AudioFormatFlags allowedFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
  if (!format || format->mFormatID != kAudioFormatLinearPCM || format->mSampleRate != 48000 ||
      format->mChannelsPerFrame != 1 || format->mBitsPerChannel != 32 || format->mFramesPerPacket != 1 ||
      format->mBytesPerFrame != 4 || format->mBytesPerPacket != 4 ||
      (format->mFormatFlags & (kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked)) != (kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked) ||
      (format->mFormatFlags & ~allowedFlags) || frames <= 0 || frames > 4800)
    return InvalidAudio(error, @"Mac audio must be 48 kHz mono Float32 PCM in bounded buffers.");
  AVAudioFormat *inputFormat = [[AVAudioFormat alloc] initWithStreamDescription:format];
  if (!inputFormat) return InvalidAudio(error, @"Mac audio format could not be read.");
  if (!_converter || ![_inputFormat isEqual:inputFormat]) {
    _inputFormat = inputFormat;
    _outputFormat = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatInt16 sampleRate:16000 channels:1 interleaved:YES];
    _converter = [[AVAudioConverter alloc] initFromFormat:_inputFormat toFormat:_outputFormat];
    if (!_converter) return InvalidAudio(error, @"Mac audio conversion is unavailable.");
    _converter.sampleRateConverterQuality = AVAudioQualityHigh;
    _converter.primeMethod = AVAudioConverterPrimeMethod_None;
  }
  AVAudioPCMBuffer *input = [[AVAudioPCMBuffer alloc] initWithPCMFormat:_inputFormat frameCapacity:(AVAudioFrameCount)frames];
  if (!input) return InvalidAudio(error, @"Mac audio buffer could not be allocated.");
  input.frameLength = (AVAudioFrameCount)frames;
  if (CMSampleBufferCopyPCMDataIntoAudioBufferList(sample, 0, (int32_t)frames, input.mutableAudioBufferList) != noErr)
    return InvalidAudio(error, @"Mac audio sample data could not be read.");
  float *values = input.floatChannelData[0];
  for (CMItemCount i = 0; i < frames; ++i) values[i] = isfinite(values[i]) ? fmaxf(-1, fminf(1, values[i])) : 0;
  // Capacity exceeds this input's maximum output plus a bounded converter tail.
  AVAudioFrameCount capacity = (AVAudioFrameCount)((frames + 2) / 3 + 256);
  AVAudioPCMBuffer *output = [[AVAudioPCMBuffer alloc] initWithPCMFormat:_outputFormat frameCapacity:capacity];
  if (!output) return InvalidAudio(error, @"Mac audio output could not be allocated.");
  __block BOOL supplied = NO;
  NSError *conversionError = nil;
  AVAudioConverterOutputStatus status = [_converter convertToBuffer:output error:&conversionError
      withInputFromBlock:^AVAudioBuffer *(AVAudioPacketCount requested, AVAudioConverterInputStatus *state) {
    (void)requested;
    if (supplied) { *state = AVAudioConverterInputStatus_NoDataNow; return nil; }
    supplied = YES; *state = AVAudioConverterInputStatus_HaveData; return input;
  }];
  if (conversionError || status == AVAudioConverterOutputStatus_Error || output.frameLength > capacity)
    return InvalidAudio(error, @"Mac audio conversion failed.");
  const int16_t *samples = output.int16ChannelData[0];
  NSMutableData *pcm = [NSMutableData dataWithLength:output.frameLength * 2];
  uint8_t *bytes = pcm.mutableBytes;
  for (AVAudioFrameCount i = 0; i < output.frameLength; ++i) {
    const uint16_t value = (uint16_t)samples[i];
    bytes[i * 2] = (uint8_t)value; bytes[i * 2 + 1] = (uint8_t)(value >> 8);
  }
  return [pcm copy];
}
@end
