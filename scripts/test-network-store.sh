#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moss-network-store-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 \
  -fsanitize=address,undefined "$ROOT/host/macos/network_store_test.m" \
  "$ROOT/host/macos/NetworkStore.m" -framework Foundation -framework Security -o "$OUT/network_store_test"
# Only fresh UUID-prefixed test services are accessed, then deleted by the test.
"$OUT/network_store_test"
