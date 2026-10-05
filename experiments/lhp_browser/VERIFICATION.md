# Prepared experiment verification — 2026-10-03

No firmware was flashed and no serial device was opened. This establishes an
offline build and host-rendering baseline, not a working public-web browser.

## Pinned dependency and build

libwebsockets/LHP: `75b415f4afd5b542badbbf87b6ffec56f3cf3cfc`, archive SHA-256
`6a5a4b0c71867dcac5eb89b2ce16010c86d6c3a73631d29085b29f21c8fa52e8`.
Actual upstream sources compiled without compatibility patches. The build keeps
PNG/JPEG and mbedTLS because this upstream/FreeRTOS configuration references them
even with offline input. It does not start Wi-Fi or a network transport.

The C6 build used Arduino ESP32 3.3.0, SDK `idf-release_v5.5-b66b5448-v1`, RISC-V
toolchain `2411`, and the repository's original 3 MiB custom app partition table
at `32ded89`. Main subsequently enlarged its app partition for radio utilities
in `bd71422`; that separate change does not turn this isolated size into a
combined-app memory measurement. New target builds copy the then-current table.

| Measurement | Result |
| --- | ---: |
| Linked program usage | 992,551 bytes |
| Static RAM | 28,980 bytes |
| Generated `.bin` | 992,656 bytes |
| RGB888 scanline | 1,440 bytes |
| Adapter RGB565 scratch | 1,920 bytes |
| Existing board DMA stripe | 15,360 bytes |

Image SHA-256: `b24d015a1b018fc342d31e343e9fbf1a11ac2ef9aa7276c3126ad3a3f1efbda3`.
This image contains an embedded fixture and real LHP, not a prerendered bitmap.
Static RAM and a successful link do not establish available runtime heap. The
192 KiB LWS requested-allocation cap is an experiment limit, not a measured need
or a guarantee of a 64 KiB reserve. TLS, Wi-Fi and SDK allocations need separate
measurement in later stages. This is also not the incremental size in the pet app.

## Host checks completed

- Stripe adapter unit tests passed with ASan/UBSan: conversion, bounds, order,
  viewport offsets, cancellation, incomplete frames and writer failure.
- Six actual LHP fixtures rendered all 480 rows at 480-pixel width: article,
  second page, tall page, PNG image, malformed HTML, and blocked remote assets.
  Text/link/layout assertions passed. The tall document extends to 491.32 pixels.
- Requested LWS heap peaks were 27,832–71,585 bytes across those fixtures; tracked
  live allocations returned to zero. These are native host allocations, excluding
  allocator overhead, libc, stacks, Wi-Fi and TLS; they are not C6 memory figures.
- Outbound HTTP(S) assets were filtered; socket/DNS test guards saw no outbound
  attempts. This harness accepts trusted local fixtures and is not an arbitrary
  filesystem or untrusted HTML sandbox.
- Remote URLs, missing/oversized inputs, corrupt source archive, and a 32 KiB heap
  ceiling failed as expected. Allocation failure released tracked allocations.
- The same C target engine rendered through the real C++ StripeSink into a mock
  panel. Three complete renders had identical pixel hash `b18552bc`; early cancel,
  mid-service cancel, restart, duplicate start, and injected panel failure all
  released tracked allocations. Native peak for this fixture was 32,991 bytes.
- The article and target-fixture images were visually inspected at 480×480.
  Text and links were readable. These are host output, not device screenshots.

## Reproduce without device access

From the repository root, with CMake, a C/C++ compiler and Python available:

```sh
experiments/lhp_browser/tests/run-stripe-tests.sh
python3 experiments/lhp_browser/host/probe.py fetch
python3 experiments/lhp_browser/host/probe.py build
python3 experiments/lhp_browser/host/probe.py run
python3 scripts/lhp-target-build.py
```

The host fetch is the explicit network step; it validates the pinned archive.
Use `--work` before the host subcommand for an alternate output directory. Both
build commands accept `--archive` for a previously fetched archive. The target
script accepts `--arduino-cli` and `--arduino-data` for an existing installation;
it neither installs tooling nor uploads firmware. See each harness README.

Verification used independent `/tmp/moss-lhp-final` and
`/tmp/moss-lhp-target-verify` output directories. Fresh runs generate their own
JSON reports, logs and rendered images. Output binaries remain outside the
ordinary firmware release/flash path.

## Still unverified

Physical panel correctness, C6 render latency and cancellation responsiveness,
runtime/largest-block/stack budgets and repeated navigation on hardware; direct
verified HTTPS, arbitrary-page input bounds, scrolling/link activation, browser
shell, and coexistence with the pet application's resident buffers. The staged
plan in README.md keeps these as explicit gates. No JavaScript or YouTube player
is implemented by this experiment.
