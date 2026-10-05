# Production browser engine host test

Runs the production C engine against the real pinned LHP parser and renderer.
It checks pixels, text, retained scrolling, link coordinates, resource blocking,
clipping, unsupported layout depth, malformed EOF, cancellation, deadlines, callback failures and cleanup
at 105 construction/layout budgets and 49 paint budgets. A separate test checks
the real arena allocator, chunk accounting, growth failures and cleanup. A fake monotonic clock makes timeout tests immediate.
The socket/DNS guard makes accidental network attempts fail and records them.
A third test runs the production asset scanner/cache and real JPEG/PNG decoders
against an in-memory file backend. It verifies external CSS, transparent pixels,
image links, cache hits, broken entries, write failures, missing SD, cancellation
and complete cleanup. It writes `/tmp/moss-assets-preview.ppm` for visual QA.
The tests do not run a server or access the device.

Fetch and verify the pinned source with the existing host probe first. Replace
`SOURCE` below with the verified source directory, and choose an owned build
location outside the source tree. CMake 3.24+, Python 3, a C/C++ compiler, and
Ninja or Make are required. Configuration makes a private source copy and applies
the target's exact renderer-only patches, including bounded CSS/glyph arenas
and disabled GPIO/event-pipe setup. The input source is not modified.

```sh
cmake --fresh -S tests/browser_engine -B /tmp/moss-browser-engine \
  -DLHP_UPSTREAM_SOURCE=SOURCE
cmake --build /tmp/moss-browser-engine --parallel
ctest --test-dir /tmp/moss-browser-engine --output-on-failure
```

To instrument the engine and upstream library, configure a separate build with
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`.

The test and ESP builds both use 16 levels for the DLO tree walkers. The engine
rejects a deeper retained display list before rendering it, rather than relying
on upstream's silent depth truncation. HTML element nesting can be deeper when
it does not produce nested display objects.

The asset fence intentionally relies on the pinned parser's `base_url == NULL`
check before all image/background/stylesheet requests. Keep this invariant and
the resource tests when updating LHP. The engine creates no Secure Streams
policy and never calls its event service loop. HTML is untrusted native parser
input; these tests do not claim a process sandbox or exhaustive parser audit.

## Production page preview

The same CMake project builds `browser-page-preview`, which renders the actual
production engine and composites its native 480×352 output (468-pixel content width with gutters) with the real browser
UI at 480×480. It only reads a local HTML fixture; the network guard remains active.

```sh
/tmp/moss-browser-engine/browser-page-preview \
  tests/browser_engine/preview.html /tmp/browser.ppm https://example.com/
```

An optional final argument selects a native scroll offset. This is a host preview,
not a screenshot from the device or evidence of a successful HTTPS request.

The asset test also accepts local public-page fixtures for visual QA:

```sh
browser-assets-test page.html https://example.com/ manifest.tsv output.ppm 0
```

The manifest maps each exact resolved image URL to a local filename, separated
by a tab. This mode never fetches the network. It runs the production reader,
asset pipeline, codecs, cache and chrome; the final argument is native scroll
offset. Public fixtures and generated captures are retained only in ignored
artifacts. Indexed PNG fixtures are synthetic; see `tests/fixtures/browser-indexed.txt`.
