# Moss Display releases

The [latest Mac download](https://github.com/alexandriax/esp32/releases/latest/download/Moss-Display-macOS.zip)
is a universal Apple Silicon / Intel app for macOS 15+. Each release includes
`SHA256SUMS`. Install in Applications and keep that location when updating.
The stable bundle ID is `org.moss.usb-display`.

## Build and publish

Every push to `main` and every pull request runs the firmware host tests, the
macOS transport tests, and an explicit unsigned universal build. That CI artifact
is for testing; it is not installed over a developer-signed app. Pull requests
never receive release credentials and cannot publish.

After tests pass on this repository's `main` and the repository variable
`MACOS_RELEASES_ENABLED` is `true`, a separate job imports the release
identity into a temporary Keychain, builds with hardened runtime, obtains Apple
notarization, staples the ticket, and uploads only the app ZIP and checksum.
A separate job with `contents: write` creates a release and updates the stable
latest-download link. There is no unsigned fallback for public CI releases.
Missing credentials or a rejected notarization stop publication. Re-running a
failed build is available under Actions → Moss Display; workflow_dispatch also
builds the current main. A published release is immutable by this workflow;
re-running an already-published build does not replace its assets.

Configure these **GitHub Actions secrets**, never repository files, then set
`MACOS_RELEASES_ENABLED=true` under Actions variables and run the workflow:

| Secret | Value |
| --- | --- |
| `MACOS_CERTIFICATE_P12_BASE64` | Base64-encoded export of the one Developer ID Application identity, including its private key |
| `MACOS_CERTIFICATE_PASSWORD` | Password protecting that export |
| `NOTARY_KEY_P8` | App Store Connect API key contents for Apple's notary service |
| `NOTARY_KEY_ID` | That API key's ID |
| `NOTARY_ISSUER_ID` | The key issuer's ID |

Use an appropriate notarization API role on the Apple developer team. The signing
certificate must remain in the same Developer ID class and team across releases.
The certificate's public signer name/team is inherently visible in a signed app;
private keys, export passwords, and notary credentials never belong in release
assets or logs. Secrets are available only to the trusted release job, not to
untrusted pull requests. Actions are pinned by commit. Restrict who can write
trusted `main`, and review workflow changes before merging them.

GitHub documents [certificate storage in Actions secrets](https://docs.github.com/en/actions/how-tos/deploy/deploy-to-third-party-platforms/sign-xcode-applications).
Apple documents [custom notarization workflows](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow).

## Local verification

```sh
scripts/configure-display-signing.sh
MOSS_ARCHS="arm64 x86_64" scripts/build-display-host.sh
scripts/package-display-host.sh
```

The normal build requires an Apple identity and verifies its signature. The
explicit `--unsigned` switch is for CI/tests without an identity. Packaging
requires both architectures and includes only `Moss Display.app`. Production
compiler paths are remapped to avoid embedding personal build directories.

## Public-source hygiene

The initial import is a reviewed source snapshot that preserves the repository's
existing license text. Its prior license-only commit used a private email, so
the published branch starts fresh with the GitHub no-reply author identity. Local development history, email metadata, binaries, caches,
serial logs, device backups, saved Wi-Fi profiles and signing configuration are
not imported. Personal site references and signing-account identifiers were
removed from historical notes. Existing copyright and third-party license
notices remain. `scripts/check-public-source.py` rejects private file classes
and personal home-directory paths, while Gitleaks scans history for secrets.
Neither check replaces review of new screenshots, examples, or debug logs.
