#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moss-display-benchmark.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 \
  "$ROOT/tools/macos_display_benchmark.m" -framework Cocoa -framework QuartzCore -framework AVFoundation \
  -o "$OUT/benchmark"
"$OUT/benchmark" "$@"
