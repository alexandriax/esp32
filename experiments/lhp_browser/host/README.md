# Offline LHP host experiment

This builds the real libwebsockets LHP parser, CSS layout, font rasterizer, PNG/JPEG
components, and Secure Streams file transport. It renders local fixtures at
480×480 to BMP, writes a display-list dump including link regions, and records
allocation and timing metrics. There is no proxy renderer and no device access.

It is a build-and-measure experiment, not an interactive browser. It does not
implement device navigation, scrolling, Wi-Fi, TLS, cookies, or video playback.
The top-level [experiment plan](../README.md) describes the target work.

## Reproduce

Requires macOS or Linux, Python 3.9+, CMake 3.24+, C11 and C++11 compilers, and curl for the
explicit source download. No Python packages are needed. Run from the repository:

```sh
python3 experiments/lhp_browser/host/probe.py fetch
python3 experiments/lhp_browser/host/probe.py build
python3 experiments/lhp_browser/host/probe.py run
```

All downloaded source, compiled output, and rendered results go under
`$TMPDIR/moss-lhp-browser-<uid>` (or `/tmp` where Python selects it). The `--work`
option must precede the command. To build without network access after obtaining
the pinned archive:

```sh
python3 experiments/lhp_browser/host/probe.py --work /tmp/moss-lhp-test build --archive /path/to/upstream.tar.gz
python3 experiments/lhp_browser/host/probe.py --work /tmp/moss-lhp-test run
```

The source revision and SHA-256 are recorded in `source-pin.json`. `build` verifies
the archive before extracting regular files into a fresh source directory.
It will only replace source directories marked as belonging to this experiment.
The CMake configuration is refreshed on each build, following upstream guidance.
Upstream code is not vendored into firmware, and no upstream device test is built.

`run` emits one BMP, layout text, JSON metrics, and log per fixture, plus a
`summary.json`. It verifies text/link regions, image layout, clipping to exactly
480 output rows, content extending beyond the viewport, memory cleanup, and safe
failure for a 32 KiB heap cap. It also compiles the same embedded-fixture C engine as the target firmware and
passes its real pixels through `StripeSink`, checking repeated renders, cancelled
loads, restart, injected panel-write failure, and zero retained LWS allocations.
The first target render is saved as `target-fixture.ppm`. One HTML fixture ends
without closing tags; this tests that particular malformed input, not arbitrary
malformed-page compatibility. It also checks remote URL rejection, missing/oversized
input rejection, and source checksum rejection. Test failures return nonzero.

For an individual trusted local fixture:

```sh
/tmp/moss-lhp-test/build/moss-lhp-probe /absolute/page.html /tmp/page.bmp /tmp/page.layout.txt /tmp/page.json
```

## Bounds and measurements

- A document is at most 64 KiB; only absolute local document paths are accepted.
- The default LWS requested-allocation quota is 200 KiB. The optional final
  executable argument changes it (32 KiB through 4 MiB).
- Rendering uses one 1,440-byte RGB888 scanline, yields every four rows, and has a
  five-second event-loop deadline. The Python runner adds a ten-second process
  deadline if upstream code fails to return control.
- The host build disables SSL, server, websocket, QUIC/HTTP3, SVG/GIF, disk caching,
  device drivers, OTA, and other unrelated features. LHP requires portions of
  the networking core for Secure Streams' local file transport.
- HTTP(S) image assets are filtered with upstream's case-insensitive URL rules.
  The host executable additionally denies `socket()` and `getaddrinfo()` through
  test-only implementations and reports attempted calls. The fixtures must make
  zero such attempts. These guards are not an untrusted-HTML or filesystem sandbox;
  use the checked-in fixtures or other trusted local HTML.

The allocator records requested bytes, including the RGB scanline. It excludes
allocator bookkeeping, direct libc allocations, C stacks, process/framework
memory, Wi-Fi, TLS, and any later RGB565 stripe adapter. Native pointer sizes and
an upstream 64 KiB host flow-control window differ from the C6 build. Therefore
**these numbers are comparative host measurements, not a device RAM budget**.
The six included Fira Sans Condensed fonts are const data, not tracked heap.

The quota allocator deliberately returns failure instead of invoking upstream's
reclaim machinery. An allocation failure marks the run unsuccessful even if LHP
continues with a partially rendered page. For the checked-in low-memory case,
cleanup must leave zero tracked allocations.

The pinned upstream client-only H2 configuration emits an unused-variable warning.
`DISABLE_WERROR` is enabled for upstream; the harness itself still builds with
`-Wall -Wextra -Werror`. No upstream source patch is applied by this host build.

## Provenance

libwebsockets is fetched from [warmcat/libwebsockets](https://github.com/warmcat/libwebsockets)
at the exact revision in `source-pin.json`. The project uses MIT with the component exceptions recorded in the preserved
`source/LICENSE` (including BSD, zlib, Apache 2.0, and CC0 code). Fira Sans uses
the Open Font License in `source/contrib/mcufont/fonts/OFL.txt`. The scanline setup follows
the upstream CC0 `api-test-lhp-dlo` example. The checked-in gradient PNG is generated
test data (96×48 RGB: red increases with x, green with y, blue is 96).
