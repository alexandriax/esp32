#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/benchmarks
xcrun clang -O2 -Wall -Wextra -Werror -c firmware/sloth_pet/src/vendor/lz4/lz4.c \
  -o build/benchmarks/lz4.o
xcrun clang -O2 -fobjc-arc -Wall -Wextra -Werror -c host/macos/ImageCodec.m \
  -o build/benchmarks/ImageCodec.o
xcrun clang++ -O2 -std=c++17 -fobjc-arc -Wall -Wextra -Werror \
  tools/benchmark_image_codecs.mm build/benchmarks/lz4.o build/benchmarks/ImageCodec.o \
  -framework AppKit -framework ImageIO -o build/benchmarks/image-codecs
build/benchmarks/image-codecs "$@"
