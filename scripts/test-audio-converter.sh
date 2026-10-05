#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moss-audio-converter-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 \
  -fsanitize=address,undefined "$ROOT/host/macos/audio_converter_test.m" \
  "$ROOT/host/macos/AudioConverter.m" -framework Foundation -framework AVFoundation \
  -framework CoreMedia -framework AudioToolbox -o "$OUT/audio_converter_test"
# Synthetic PCM only; no capture, audio device, microphone, permissions or files.
"$OUT/audio_converter_test"
