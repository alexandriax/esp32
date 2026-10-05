# ESP32-C6 offline LHP target

This separate Arduino sketch compiles the real LHP parser, CSS layout, font
renderer and display-list implementation for Moss. Its embedded HTML fixture
is parsed on the device and sent through the native 480-pixel RGB565 stripe
adapter. It is not yet a navigable browser or part of the pet firmware.

No build command opens a serial port, queries hardware, uploads firmware or
changes the running device. Physical execution and memory measurements remain
unverified until the device is available for a separately authorized test.

## Build

Prerequisites: Python 3, CMake 3.24 or later, Arduino CLI, and the already-installed
Arduino ESP32 3.3.0 stack (IDF 5.5 libraries and RISC-V toolchain 2411). The build
script does not install or upgrade tools. It accepts explicit dependency paths.

From the repository root, fetch the checksum-pinned source through the shared
host helper, then compile:

```sh
python3 experiments/lhp_browser/host/probe.py --work build/lhp-browser/host fetch
python3 scripts/lhp-target-build.py --archive build/lhp-browser/host/upstream.tar.gz
```

The default tool locations are `.tools/arduino-cli` and `.tools/arduino-data`.
For tools already installed in another checkout:

```sh
python3 scripts/lhp-target-build.py \
  --archive /path/to/verified/upstream.tar.gz \
  --arduino-cli /path/to/.tools/arduino-cli \
  --arduino-data /path/to/.tools/arduino-data \
  --work /tmp/moss-lhp-target
```

`--toolchain` can override the RISC-V `bin` directory and `--sdk` the precompiled
IDF `esp32c6` directory. The tested ABI remains Arduino 3.3.0 / IDF 5.5.
The output directory must be empty or carry this tool's ownership marker.
The archive is checked and re-extracted on each build; mutable source caches
are not trusted. No upstream source patches are applied.

Outputs stay under `build/lhp-browser/target` by default:

- `firmware/lhp_browser.ino.bin` and `.elf`: separate experiment application.
- `build-report.json`: source pin, image length and SHA-256, no-flash status.
- `configure.log`, `library.log`, `firmware.log`: configure, compiler and size evidence.

The normal `scripts/build.sh` and its `build/sloth_pet.ino.*` release files are
unchanged. The experiment uses a copy of the same partition table and board
driver. It is a separate application image, not an extra installed app or a
runtime switch within the existing pet firmware.

## Behavior after a future explicit hardware test

Boot renders one local HTML/CSS fixture from constant flash data. **KEY** renders
it again after completion. **BOOT** cancels an active render. Hardware long-hold
power behavior stays with the PMIC. Touch navigation, link activation, URL entry
and scrolling are not implemented in this first target probe.

The render has a 15-second deadline, yields every four rows, and schedules a
10ms event-loop tick so the main loop can poll controls. A fatal parser/render
result or allocation failure ends the attempt. Partial-start resources are
released. The timeout and control responsiveness still require on-device tests.

The panel stays awake because the production driver's wake/show contract
requires its full-frame API; a sequence of region writes does not authorize
waking the panel. This avoids introducing an unverified power-state change in
the experiment.

## Resource boundaries

- One LHP RGB888 row: 1,440 bytes.
- Adapter RGB565 scratch: 1,920 bytes for two rows.
- Reused board DMA stripe: 15,360 bytes.
- No 240x240 or 480x480 application framebuffer.
- Requested LWS allocations have a 192KiB ceiling; font blobs stay in flash.
- Before render, after render and after teardown, serial diagnostics report
  free heap, boot minimum heap, largest 8-bit block, requested LWS live/peak
  allocation totals and allocation failures.

The ceiling excludes allocator bookkeeping, RTOS stacks, SDK/lwIP, TLS and
board allocations. It does **not** enforce the future 64KiB spare-heap gate in
the experiment plan. Compile-time "memory remaining" is not runtime free heap.

The app initializes lwIP for LWS's local event-loop wakeup sockets. It never
starts Wi-Fi, loads credentials, connects to an external server or plays audio.
The document filter rejects HTTP and HTTPS assets. The board's existing bounded
SD SPI preparation is reused; the app never mounts a filesystem or saves
profiles, pet settings or card data.

The library includes mbedTLS because its current FreeRTOS file module references
TLS fields even in an offline build. PNG/JPEG are included because current LHP
contains references to both image APIs. Inclusion establishes build/link support;
this sketch neither implements nor verifies public HTTPS trust or image loading.
HTTP/2 is disabled in this target while the host probe enables it. Optional
platform drivers, system messaging, connection monitoring, Wake-on-LAN, cookie
storage, HTTP authentication and uncommon headers are disabled.

## Host validation of the same engine

The host suite compiles this exact `lhp_engine.c`, sends its output through the
real `StripeSink` and checks lifecycle behavior without opening a device:

```sh
python3 experiments/lhp_browser/host/probe.py --work build/lhp-browser/host build
python3 experiments/lhp_browser/host/probe.py --work build/lhp-browser/host run
```

This establishes fixture parsing, rendering and resource cleanup on the host.
It does not establish C6 runtime heap, stack usage, DMA behavior, timing or
physical text readability. Those checks and direct HTTPS are later gates in
[the experiment plan](../README.md).
