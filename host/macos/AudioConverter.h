#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>

// Stateful bounded live resampler. Call on one serial queue and discard on capture restart.
// Input: ready PCM Float32 LE mono 48 kHz, <=100 ms per sample buffer.
// Output: PCM signed16 LE mono 16 kHz. Empty output is valid while priming; nil means error.
@interface MossAudioConverter : NSObject
- (NSData *)convertSample:(CMSampleBufferRef)sample error:(NSError **)error;
@end
