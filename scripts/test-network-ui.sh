#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moss-network-ui-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
COMMON=(-fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 -fsanitize=address,undefined)
xcrun clang "${COMMON[@]}" -Dmain=MossApplicationMain -c "$ROOT/host/macos/main.m" -o "$OUT/main.o"
for SOURCE in VirtualDisplay DisplayCapture WiFiChannel NetworkStore ImageCodec DevicePairingStore DeviceDiscovery AudioConverter network_form_test; do
  xcrun clang "${COMMON[@]}" -c "$ROOT/host/macos/$SOURCE.m" -o "$OUT/$SOURCE.o"
done
xcrun clang++ "${COMMON[@]}" -std=c++17 -c "$ROOT/host/macos/USBTransport.mm" -o "$OUT/USBTransport.o"
xcrun clang -std=c11 -Wall -Wextra -Werror -mmacosx-version-min=15.0 -fsanitize=address,undefined \
  -c "$ROOT/firmware/sloth_pet/src/vendor/lz4/lz4.c" -o "$OUT/lz4.o"
xcrun clang++ "${COMMON[@]}" "$OUT"/*.o -framework Cocoa -framework CoreGraphics \
  -framework ScreenCaptureKit -framework CoreMedia -framework CoreVideo -framework IOKit \
  -framework Network -framework Security -framework ImageIO -framework AVFoundation -framework AudioToolbox -o "$OUT/network_form_test"
# Creates only offscreen views: no capture, serial, network, or Keychain access.
"$OUT/network_form_test"
