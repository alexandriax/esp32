#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moss-wifi-test.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
xcrun clang -fobjc-arc -Wall -Wextra -Werror -mmacosx-version-min=15.0 \
  -fsanitize=address,undefined "$ROOT/host/macos/wifi_channel_test.m" \
  "$ROOT/host/macos/WiFiChannel.m" -framework Foundation -framework Network \
  -framework Security -o "$OUT/wifi_channel_test"
# These disposable test keys belong only to the loopback fixture, never Keychain.
openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 1 -subj /CN=MossLoopbackTest \
  -keyout "$OUT/key.pem" -out "$OUT/cert.pem" >/dev/null 2>&1
/usr/bin/python3 "$ROOT/scripts/wifi-channel-fixture.py" "$OUT"
