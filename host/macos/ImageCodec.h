#import <Foundation/Foundation.h>

#ifdef __cplusplus
extern "C" {
#endif

// RGB565 little-endian 240x240 input. Returns an immutable baseline JPEG only
// when it fits one 15,360-byte display payload. The caller retains its lossless
// fallback when quality/input is invalid, encoding fails, or JPEG is too large.
// APP1..APP15 and COM metadata are omitted; APP0/JFIF and image data are retained.
NSData * _Nullable MossEncodeJPEG240(NSData * _Nullable rgb565, double quality);

#ifdef __cplusplus
}
#endif
