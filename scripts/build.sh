#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/firmware
python3 scripts/generate_humor.py --check
# The production browser links the same checksum-pinned LHP engine as the probe.
# --fetch permits the first dependency download; later builds verify the cache.
python3 scripts/lhp-target-build.py --application --fetch
for file in build/lhp-browser/application/firmware/sloth_pet.ino.*; do
  cp "$file" build/firmware/
done
# Arduino cleans its build path. Keep the companion app and host tests outside
# that directory, then publish only the three verified flash inputs on success.
for file in sloth_pet.ino.bootloader.bin sloth_pet.ino.partitions.bin sloth_pet.ino.bin; do
  cp "build/firmware/$file" "build/$file"
done
