# LHP graphical browsing experiment

The production firmware now includes a **Browser** menu and direct Wi-Fi loading;
see [the browser guide](../../docs/browser.md). The files in this directory remain
the original isolated, standalone ESP32-C6 feasibility experiment. It is not a
production browser feature and does not use a companion to fetch, lay out, or
render pages. Host rendering is a development test only. None of its scripts
uploads firmware, opens a serial device, changes partitions, or provisions Wi-Fi.

The active pet firmware and device are being developed separately. Keep this
experiment additive until its resource and lifecycle gates pass. Shared Wi-Fi
setup is a separate production change; this initial LHP target stays offline.

Prepared components: [host harness](host/README.md), [isolated C6 target](target/README.md),
and [measured verification results](VERIFICATION.md).

## Questions the experiment must answer

1. Can the real LHP parser, CSS layout and scanline renderer be built for this
   board's Arduino ESP32 3.3.0 / ESP-IDF 5.5 / RISC-V environment?
2. What code, static RAM, stack, heap and contiguous allocation space does it
   consume, first offline and then with verified public HTTPS active?
3. Can small public HTML pages remain readable at native 480-pixel width, with
   successful cancellation, scrolling and link navigation?
4. Can resource limits reject an unsuitable page without a reset or damaged
   state, and can all browser allocations be recovered on exit?

The experiment does not attempt JavaScript, YouTube playback, tabs, accounts,
password storage, downloads, background browsing, or arbitrary website
compatibility. None of these is a prerequisite for measuring LHP's core fit.

## Dependency choice

Pin libwebsockets to commit `75b415f4afd5b542badbbf87b6ffec56f3cf3cfc`.
Build a private static library; do not install it globally. Keep downloaded
upstream sources and compiled files in ignored build/cache directories.

The published LHP overview lags this source revision. For example, the current
source contains additional CSS, image and allocation features absent from the
older restrictions list. Treat compiled source behavior and the fixtures as
primary evidence. This remains an experimental renderer, not a standards-complete
browser. A passing desktop test is not evidence of an ESP32 memory fit.

Sources:
- https://github.com/warmcat/libwebsockets/tree/75b415f4afd5b542badbbf87b6ffec56f3cf3cfc
- https://libwebsockets.org/lws-api-doc-main/html/md_READMEs_README_html_parser.html
- https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16

## Architecture

The intended on-device path is:

```
saved Wi-Fi profile (read only)
  -> verified HTTPS client / bounded network input
  -> LHP parser and layout
  -> RGB888 scanlines
  -> StripeSink (two rows, RGB565 little endian)
  -> existing board::presentRegion / QSPI DMA
```

The isolated target must reuse board pin definitions and the existing display
transport. It must not pull in the production pet framebuffer, remote-display
packet parser, JPEG state, 32 KiB USB receive queue or audio stack. This measures
LHP without unrelated resident buffers; it does not establish that the same
budget remains available in the combined pet application.

`stripe_sink.h` pairs native 480-pixel RGB888 scanlines into even-aligned RGB565
rectangles. Its pixel scratch space is 1,920 bytes; the whole adapter is less
than 2 KiB. The board itself already owns a 15,360-byte DMA stripe. A 480x480
RGB565 framebuffer would cost 460,800 bytes and must not be introduced. The
writer callback completes synchronously, so the scanline storage can be reused
only after it returns.

The first viewport is the entire 480x480 panel. A later browser shell can reserve
64 rows for navigation and use `begin(64, 416)` without changing the adapter.
Do not draw from another task, an ISR, or an input callback while the panel is
busy. Deliver navigation and cancellation to the rendering task.

Keep the panel awake in the initial target experiment. The current board API
requires a complete `board::present()` before `showDisplay()` after wake;
`presentRegion()` alone does not establish that condition. Supporting sleep/wake
without a full framebuffer requires a separately reviewed completion API.

## Stages and decision gates

### 1. Offline host rendering

Build actual pinned LHP and render deterministic local fixtures at 480x480. No
public-network access is needed. Cover text wrapping, headings, colors, links,
a tall document, malformed input, allocation failure, and timeout behavior.
Record layout, rendered image, elapsed time, and allocation statistics. Inspect
the rendered image as well as the exit status. Remote subresources must not be
fetched by this offline harness.

Host heap measurements use the host's pointer sizes and allocator. They are
useful for comparisons and identifying unexpectedly large pages, not for
subtracting an exact amount from the C6's heap.

### 2. Compile the isolated C6 renderer

Build the same library and a local fixture for C6, with network/audio disabled
initially. Link real LHP; a stub or prerendered bitmap does not satisfy this gate.
Record linked flash usage, static RAM and any required upstream compatibility
patches. Keep outputs separate from `build/sloth_pet.ino.*` so the ordinary
release/flash workflow cannot accidentally pick up an experiment binary.

A successful build is sufficient for this worktree preparation stage. Flashing
and physical measurements are intentionally deferred while another thread uses
the device.

### 3. Later device measurements, after explicit device availability

Measure free heap, minimum free heap, largest 8-bit block, DMA-capable free heap,
and task stack high-water at boot, after board setup, context creation, parse,
layout, first paint, completion and teardown. Repeat navigation/cancel/exit at
least 100 times and check for drift. Long pages must have bounded work and a
working cancellation path.

Start with a 64 KiB uncommitted internal-heap reserve as a conservative experiment
gate, not an asserted library requirement. Reject before starting a load if the
measured configuration cannot preserve that reserve. Revisit only with measured
peak usage. CPU time and largest-block pressure matter alongside total free RAM.

### 4. Direct HTTPS and public-page compatibility

Only after offline rendering fits: reuse the planned Wi-Fi lifecycle through an
explicit adapter. Read saved credentials without logging or rewriting them. If
no valid profile exists, report setup unavailable; do not silently replace it.
Use trusted roots, hostname verification and a valid clock. Certificate failure
must remain failure, with no insecure fallback.

Limit concurrent network activity and redirects. Enforce a total load deadline,
a received-byte ceiling, allocation ceiling and image-dimension limits, including
subresources and decompressed data. LHP's allocator hook by itself does not
bound TLS, Wi-Fi, SDK or display allocations. Do not assume a source-file size
limit protects against compressed HTML or images.

Test a controlled HTTPS fixture before representative public documentation,
articles and simple forms. Include invalid/expired/untrusted certificates,
redirect loops, stalled responses, oversized HTML/CSS, images with oversized
headers and connection loss. Host unit tests must use fixtures rather than
assert that a live website never changes.

### 5. Integrate with Moss only if the gates pass

Add a browser mode and entry point against the then-current main branch and
Wi-Fi interfaces. Preserve pet care and persisted state. The browser should own
reusable buffers exclusively while active, preserve responsive Back/PWR behavior,
and release its context, connections and allocations before returning to the pet.

The initial UI is one page, a URL field, Back/Reload/Cancel, vertical scrolling,
and selectable links. Begin with a small font set. Treat unsupported script-driven
pages and resource-limit failures as ordinary visible outcomes. Add images and
forms individually only after their budgets and interaction behavior are tested.

## Existing budget evidence

The baseline v1.27 notes report 1,570,159 program bytes inside a 3 MiB application
partition and 111,212 static RAM bytes. At that baseline the remaining app room was about
1.50 MiB. The separate Utilities work enlarged the app partition to 8 MiB in
`bd71422`; exact combined usage still needs a fresh link.
The 16 MiB physical flash does not increase RAM, and ESP32-C6 has no PSRAM support.
Do not change the partition table to conceal a failed resource gate.

Older production Wi-Fi+audio tests had only tens of KiB free heap. Those numbers
are not this isolated target's budget. Conversely, a successful isolated target
does not prove simultaneous pet, remote display, audio and browser operation.

## Display adapter verification

Run from the repository root:

```
experiments/lhp_browser/tests/run-stripe-tests.sh
```

The ASan/UBSan tests check color conversion, little-endian output, native panel
geometry, viewport offsets, incomplete/duplicate/out-of-order rows, invalid
lengths, cancellation, write failures and restarting a render. No device is used.

## Prepared-state verification

See `VERIFICATION.md` for commands actually run, observed results, and remaining
limitations. Planned acceptance criteria above must not be recorded as passed
until the corresponding measurement exists.
