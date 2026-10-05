# LZ4 codec provenance

Unmodified library files from the official LZ4 v1.10.0 release:
https://github.com/lz4/lz4/releases/tag/v1.10.0

Downloaded from `https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/`.
The library is BSD-2-Clause licensed; see LICENSE and source headers.

Firmware calls only `LZ4_decompress_safe` with bounded input and an exact-size
15360-byte maximum output stripe. It uses independent raw LZ4 blocks, without
frame headers, external dictionaries, streaming state, or heap allocations.
The desktop companion uses `LZ4_compress_default` from the same source.

Original file SHA-256 checksums:

- `lz4.c`: `9396f7de527bc8435de9c7569fb7998e56545a84b4f3c2d808c0235c01774539`
- `lz4.h`: `26b82efc53d1570f3b54eef02e9c4764c1ad374ff03cac04e2ced5ea4d4c552f`
- `LICENSE`: `8b58c446121a109ccf32edc094bba3010a3d85e4ee3702950db55e4d3e87736c`
