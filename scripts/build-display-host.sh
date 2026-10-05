#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Fail before touching the installed build if its stable identity is unavailable.
UNSIGNED=0
if [[ "${1:-}" == --unsigned && $# == 1 ]]; then
  UNSIGNED=1 # Explicit CI/test build; never a fallback for a missing identity.
elif [[ $# != 0 ]]; then
  printf 'Usage: %s [--unsigned]\n' "$0" >&2; exit 2
fi
if [[ "$UNSIGNED" == 0 ]]; then
  SIGNING_IDENTITY="$("$ROOT/scripts/configure-display-signing.sh" --check)"
fi
ARCH_FLAGS=()
for ARCH in ${MOSS_ARCHS:-$(uname -m)}; do
  case "$ARCH" in arm64|x86_64) ARCH_FLAGS+=(-arch "$ARCH");;
    *) printf 'Unsupported architecture\n' >&2; exit 2;; esac
done
OUT="$ROOT/build/macos"
APP="$OUT/Moss Display.app"
mkdir -p "$OUT/objects" "$APP/Contents/MacOS" "$APP/Contents/Resources"
COMMON=(-fobjc-arc -Wall -Wextra -Werror -O2 -mmacosx-version-min=15.0 "-ffile-prefix-map=$ROOT=." "${ARCH_FLAGS[@]}")
xcrun clang -std=c11 -Wall -Wextra -Werror -O2 -mmacosx-version-min=15.0 "${ARCH_FLAGS[@]}" "-ffile-prefix-map=$ROOT=." \
  -c "$ROOT/firmware/sloth_pet/src/vendor/lz4/lz4.c" -o "$OUT/objects/lz4.o"
for SOURCE in main VirtualDisplay DisplayCapture WiFiChannel NetworkStore ImageCodec DevicePairingStore DeviceDiscovery AudioConverter; do
  xcrun clang "${COMMON[@]}" -c "$ROOT/host/macos/$SOURCE.m" -o "$OUT/objects/$SOURCE.o"
done
xcrun clang++ "${COMMON[@]}" -std=c++17 -c "$ROOT/host/macos/USBTransport.mm" -o "$OUT/objects/USBTransport.o"
xcrun clang++ -mmacosx-version-min=15.0 "${ARCH_FLAGS[@]}" "$OUT/objects/main.o" "$OUT/objects/VirtualDisplay.o" \
  "$OUT/objects/DisplayCapture.o" "$OUT/objects/USBTransport.o" "$OUT/objects/WiFiChannel.o" "$OUT/objects/NetworkStore.o" "$OUT/objects/ImageCodec.o" "$OUT/objects/DevicePairingStore.o" "$OUT/objects/DeviceDiscovery.o" "$OUT/objects/AudioConverter.o" "$OUT/objects/lz4.o" \
  -framework Cocoa -framework CoreGraphics -framework ScreenCaptureKit \
  -framework CoreMedia -framework CoreVideo -framework IOKit -framework Network -framework Security -framework ImageIO -framework AVFoundation -framework AudioToolbox \
  -o "$APP/Contents/MacOS/MossDisplay"
cp "$ROOT/host/macos/Info.plist" "$APP/Contents/Info.plist"
cp "$ROOT/host/macos/MossDisplay.icns" "$APP/Contents/Resources/MossDisplay.icns"
/usr/bin/plutil -lint "$APP/Contents/Info.plist"
if [[ "$UNSIGNED" == 1 ]]; then
  /usr/bin/codesign --force --sign - --identifier org.moss.usb-display "$APP"
else
  /usr/bin/codesign --force --sign "$SIGNING_IDENTITY" --options runtime --timestamp \
    --identifier org.moss.usb-display "$APP"
fi
/usr/bin/codesign --verify --deep --strict "$APP"
/usr/bin/codesign --display -r- "$APP"
xcrun clang -std=c11 -Wall -Wextra -Werror -mmacosx-version-min=15.0 -fsanitize=address,undefined \
  -c "$ROOT/firmware/sloth_pet/src/vendor/lz4/lz4.c" -o "$OUT/objects/lz4-test.o"
xcrun clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  "$ROOT/host/macos/wire_test.cpp" "$ROOT/firmware/sloth_pet/display_stream.cpp" "$OUT/objects/lz4-test.o" -o "$OUT/wire_test"
"$OUT/wire_test"
xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 \
  -fsanitize=address,undefined -c "$ROOT/host/macos/WiFiChannel.m" -o "$OUT/objects/WiFiChannel-test.o"
xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 \
  -fsanitize=address,undefined -c "$ROOT/host/macos/ImageCodec.m" -o "$OUT/objects/ImageCodec-test.o"
xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 -fsanitize=address,undefined \
  "$ROOT/host/macos/image_codec_test.m" "$OUT/objects/ImageCodec-test.o" \
  -framework Foundation -framework CoreGraphics -framework ImageIO -o "$OUT/image_codec_test"
"$OUT/image_codec_test"
xcrun clang++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-function -mmacosx-version-min=15.0 -fsanitize=address,undefined \
  -c "$ROOT/firmware/sloth_pet/src/vendor/jpegdec/JPEGDEC.cpp" -o "$OUT/objects/JPEGDEC-test.o"
for SOURCE in DevicePairingStore DeviceDiscovery; do
  xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 -fsanitize=address,undefined \
    -c "$ROOT/host/macos/$SOURCE.m" -o "$OUT/objects/$SOURCE-test.o"
done
xcrun clang++ -std=c++17 -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 \
  -fsanitize=address,undefined "$ROOT/host/macos/transport_test.mm" \
  "$ROOT/host/macos/USBTransport.mm" "$OUT/objects/WiFiChannel-test.o" "$OUT/objects/ImageCodec-test.o" "$OUT/objects/DevicePairingStore-test.o" "$OUT/objects/DeviceDiscovery-test.o" \
  "$ROOT/firmware/sloth_pet/display_stream.cpp" "$ROOT/firmware/sloth_pet/jpeg_display.cpp" \
  "$OUT/objects/lz4-test.o" "$OUT/objects/JPEGDEC-test.o" \
  -framework Foundation -framework IOKit -framework Network -framework Security -framework ImageIO -framework CoreGraphics -o "$OUT/transport_test"
"$OUT/transport_test"
"$APP/Contents/MacOS/MossDisplay" --probe
printf 'Built %s\n' "$APP"
