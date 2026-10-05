#!/bin/bash
set -euo pipefail
: "${RUNNER_TEMP:?}" "${NOTARY_KEY_P8:?}" "${NOTARY_KEY_ID:?}" "${NOTARY_ISSUER_ID:?}"
MOSS_NOTARY_KEY="$RUNNER_TEMP/moss-notary.p8"
trap 'rm -f "$MOSS_NOTARY_KEY"' EXIT
umask 077
printf '%s' "$NOTARY_KEY_P8" > "$MOSS_NOTARY_KEY"
xcrun notarytool submit build/release/Moss-Display-macOS.zip \
  --key "$MOSS_NOTARY_KEY" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID" \
  --wait --timeout 30m --output-format json > "$RUNNER_TEMP/moss-notary-result.json"
python3 - "$RUNNER_TEMP/moss-notary-result.json" <<'PY'
import json,sys
result=json.load(open(sys.argv[1]))
if result.get('status')!='Accepted':
    raise SystemExit('Apple notarization was not accepted; release will not publish.')
print('Apple notarization accepted.')
PY
xcrun stapler staple 'build/macos/Moss Display.app'
xcrun stapler validate 'build/macos/Moss Display.app'
scripts/package-display-host.sh
