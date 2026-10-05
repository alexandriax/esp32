#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moss-device-pairing-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 \
  -fsanitize=address,undefined "$ROOT/host/macos/device_pairing_test.m" \
  "$ROOT/host/macos/DevicePairingStore.m" "$ROOT/host/macos/DeviceDiscovery.m" \
  -framework Foundation -framework Security -o "$OUT/device_pairing_test"
# Accesses only two fresh UUID test services, deleted at completion. No Bonjour browsing.
"$OUT/device_pairing_test"
