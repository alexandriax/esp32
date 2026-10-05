#!/bin/bash
# Package exactly the app bundle, never Keychain material, tests or local state.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build/macos/Moss Display.app"
OUT="$ROOT/build/release"
mkdir -p "$OUT"
/usr/bin/codesign --verify --deep --strict "$APP"
/usr/bin/lipo "$APP/Contents/MacOS/MossDisplay" -verify_arch arm64 x86_64
COPYFILE_DISABLE=1 /usr/bin/ditto -c -k --keepParent --norsrc "$APP" "$OUT/Moss-Display-macOS.zip"
(cd "$OUT" && shasum -a 256 Moss-Display-macOS.zip > SHA256SUMS)
printf 'Packaged universal Mac app: %s\n' "$OUT/Moss-Display-macOS.zip"
