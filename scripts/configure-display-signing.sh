#!/bin/bash
# Store only a public certificate fingerprint; private keys stay in Keychain.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="$ROOT/.cache/macos-signing-identity"
MODE="${1:-}"
if [[ "$MODE" == --help ]]; then
  printf 'Usage: %s [certificate SHA-1 | --check]\n' "$0"
  printf 'Configure a valid Apple signing identity, preferring a unique Developer ID Application identity.\n'
  exit 0
fi
IDENTITIES="$(/usr/bin/security find-identity -v -p codesigning)"
if [[ "$MODE" == --check ]]; then
  if [[ ! -f "$CONFIG" ]]; then
    printf 'Configure stable signing first: scripts/configure-display-signing.sh\n' >&2
    exit 1
  fi
  IDENTITY="$(cat "$CONFIG")"
elif [[ -n "$MODE" ]]; then
  IDENTITY="$MODE"
else
  CANDIDATES="$(printf '%s\n' "$IDENTITIES" | /usr/bin/sed -nE 's/^[[:space:]]*[0-9]+\) ([0-9A-F]{40}) "Developer ID Application:.*$/\1/p')"
  if [[ -z "$CANDIDATES" ]]; then
    CANDIDATES="$(printf '%s\n' "$IDENTITIES" | /usr/bin/sed -nE 's/^[[:space:]]*[0-9]+\) ([0-9A-F]{40}) "Apple Development:.*$/\1/p')"
  fi
  IDENTITY="$CANDIDATES"
fi
if [[ ! "$IDENTITY" =~ ^[0-9A-Fa-f]{40}$ ]]; then
  printf 'Choose exactly one valid Apple signing certificate using its SHA-1 fingerprint.\n' >&2
  printf 'Run: security find-identity -v -p codesigning\n' >&2
  printf 'Then: scripts/configure-display-signing.sh <SHA-1>\n' >&2
  printf 'A sandbox may hide Keychain identities; run the command with normal Keychain access. No ad-hoc fallback is permitted.\n' >&2
  exit 1
fi
IDENTITY="$(printf '%s' "$IDENTITY" | /usr/bin/tr '[:lower:]' '[:upper:]')"
MATCH="$(printf '%s\n' "$IDENTITIES" | /usr/bin/sed -nE "s/^[[:space:]]*[0-9]+\\) $IDENTITY \"((Developer ID Application|Apple Development):.*)\"$/\\1/p")"
if [[ -z "$MATCH" ]]; then
  printf 'Configured signing certificate %s is unavailable or invalid.\n' "$IDENTITY" >&2
  printf 'Unlock the login Keychain and allow normal Keychain access, or configure a renewed Apple certificate. No ad-hoc fallback is permitted.\n' >&2
  exit 1
fi
if [[ "$MODE" == --check ]]; then
  printf '%s\n' "$IDENTITY"
else
  mkdir -p "$(dirname "$CONFIG")"
  printf '%s\n' "$IDENTITY" > "$CONFIG"
  printf 'Configured %s\nPublic fingerprint saved in %s\n' "$MATCH" "$CONFIG"
fi
