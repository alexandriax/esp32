# JPEGDEC provenance and local hardening

Official JPEGDEC 1.8.4, pinned commit
`ae9705abeeebd057c3d0a50c1e58bdec532c76c6`:
https://github.com/bitbank2/JPEGDEC/tree/1.8.4

Apache-2.0; see LICENSE and source headers. Only JPEGDEC.cpp, JPEGDEC.h,
and jpeg_impl.h are needed for ESP32-C6 and the macOS portable tests.

Local changes, identified by comparison with this exact upstream revision:
- JPEGDEC.cpp/jpeg_impl.h: rename upstream jpeg.inl to jpeg_impl.h and update
  its include, so Arduino copies the implementation into its sketch staging tree.
- JPEGDEC.cpp: scope GCC `O3` optimization to this implementation when
  `MOSS_DISPLAY_SPEED_OPT=1` (enabled by scripts/build.sh). Push/pop restores
  compiler options; Clang and builds without the macro retain their defaults.
  This does not enable fast-math or change SDK optimization flags.
- jpeg_impl.h: guard two Huffman-table shifts with iBitNum >= 5 / >= 6 to avoid
  negative shift counts, reproduced by UBSan on an ordinary valid fixture.
- JPEGDEC.h: integer byte-load macros use memcpy-backed helpers rather than
  typed unaligned pointer dereferences (alignment/aliasing UB). Our targets
  are little-endian. Width follows the upstream REGISTER_WIDTH definition.

No JPEG algorithm, color conversion, or output format was rewritten.
The surrounding jpeg_display.cpp accepts only the companion's bounded
baseline 240x240 4:2:0 single-scan subset and decodes entirely into the existing
pet framebuffer before any display write. EXIF/progressive/metadata paths are
excluded by checked header validation. Decoder state is 17,884 bytes on C6.

Original upstream SHA-256 before the local hardening:

- `JPEGDEC.cpp`: `cce4a7cd6fe8fe26825276f9c65ed9c96bf1fe31a15c79e0169ee2385543d210`
- `JPEGDEC.h`: `3c7ed1ec539bc581ee189aa27857c4a228cc6668455b8aca2912466efa881d34`
- `jpeg.inl`: `ab6dd18350a8d1ab73ad8b296d7da7274684fa4571f9abdd46d5630709a0c7bc`
