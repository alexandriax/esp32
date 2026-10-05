#!/bin/bash
# Called only by the trusted-main release job, never pull-request workflows.
set -euo pipefail
: "${RUNNER_TEMP:?}" "${GITHUB_ENV:?}" "${MACOS_CERTIFICATE_P12_BASE64:?}" "${MACOS_CERTIFICATE_PASSWORD:?}"
MOSS_KEYCHAIN="$RUNNER_TEMP/moss-signing.keychain-db"
MOSS_P12="$RUNNER_TEMP/moss-signing.p12"
MOSS_KEYCHAIN_PASSWORD="$(openssl rand -hex 24)"
printf '::add-mask::%s\n' "$MOSS_KEYCHAIN_PASSWORD"
trap 'rm -f "$MOSS_P12"' EXIT
printf '%s' "$MACOS_CERTIFICATE_P12_BASE64" | base64 --decode > "$MOSS_P12"
chmod 600 "$MOSS_P12"
security create-keychain -p "$MOSS_KEYCHAIN_PASSWORD" "$MOSS_KEYCHAIN"
security set-keychain-settings -lut 7200 "$MOSS_KEYCHAIN"
security unlock-keychain -p "$MOSS_KEYCHAIN_PASSWORD" "$MOSS_KEYCHAIN"
security import "$MOSS_P12" -P "$MACOS_CERTIFICATE_PASSWORD" -k "$MOSS_KEYCHAIN" -T /usr/bin/codesign > /dev/null
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$MOSS_KEYCHAIN_PASSWORD" "$MOSS_KEYCHAIN" > /dev/null
security list-keychains -d user -s "$MOSS_KEYCHAIN"
printf 'MOSS_SIGNING_KEYCHAIN=%s\n' "$MOSS_KEYCHAIN" >> "$GITHUB_ENV"
scripts/configure-display-signing.sh
