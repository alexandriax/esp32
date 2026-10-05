# Moss Display for macOS

This local menu-bar companion turns the device into an **extended Mac desktop** with a 480 × 480 physical screen. The desktop initially sits to the right of the existing displays. It captures only that virtual display, includes the pointer, and sends pixels over USB or paired Wi-Fi. Capture supplies up to 30 frames per second; changed-region detection, adaptive compression, decoding, panel drawing, and ACK pacing determine actual updates. Optional Mac/system audio is off by default; microphones are never captured.

## Build and use

Requires macOS 15 or later and Xcode command-line tools. Development and runtime API probing were performed on macOS 26.5.1. Run from the repository:

```sh
scripts/build-display-host.sh
open "build/macos/Moss Display.app"
```

1. Open **Menu → Remote Display** on firmware v1.16. It prefers saved Wi-Fi and falls back to connected USB before streaming. Use the connection page to enter a new network or explicitly choose USB.
2. Approve the native macOS Screen Recording request for **Moss Display**. If macOS sends you to System Settings, grant access there, relaunch the companion if required, then select Remote Display on the device again. The menu has a shortcut to those settings; the app never changes permissions itself.
3. Drag a window onto the new display. Use the menu's **Display Settings** item to arrange it.
4. While streaming, **left (BOOT/−)** lowers speaker volume and **right (+/KEY)** raises it. With v1.16, a quick **PWR** tap removes the Mac display/capture and returns to connection options; another PWR returns to the main menu. The companion's **Disconnect display** and **Quit** items also end the session. Long-hold hardware power is unchanged.

The companion waits for an explicit device request. Reconnecting the cable does not resume a previous session. With firmware v1.13, a paired device can instead announce a new Wi-Fi Display session through Bonjour, without USB. Close the companion before flashing firmware or using another serial monitor: it holds an attached serial device exclusively. No login item or background service is installed.

Version 1.1 adds two menu controls. **Desktop size (next connection)** selects 800 × 800 (default, larger UI) or 960 × 960 (more workspace). **Image quality** switches live between Sharp 480 × 480 and Fast 240 × 240 enlarged 2× on the panel. Fast requires firmware v1.9; older firmware remains compatible with native raw changed-row updates. Both preferences are saved locally on the Mac. Desktop size is independent of USB pixel count.

Keep the app at the same path and use the configured Developer ID identity. The build requires a valid signing identity and never silently falls back to ad-hoc signing. See [stable signing](../../docs/macos-signing.md); migration from the old build can require one final approval.

## Implementation and limitations

The virtual display uses **private CoreGraphics APIs**, dynamically checked at runtime. Apple does not promise their stability across macOS updates. Missing APIs fail without falling back to capturing an existing display. An explicit session-only arrangement removes mirroring; if the user later switches it to mirroring, streaming stops.

The implementation follows the API shape demonstrated in [Chromium's virtual-display utility](https://chromium.googlesource.com/chromium/src/+/HEAD/ui/display/mac/test/virtual_display_util_mac.mm). Capture uses the public [ScreenCaptureKit API](https://developer.apple.com/documentation/screencapturekit/capturing-screen-content-in-macos), filtered by the new display ID. It creates no kernel driver and needs no third-party runtime.

The host waits asynchronously for WindowServer attachment and explicitly selects the requested usable mode before arranging the desktop. It advertises 800 × 800 and 960 × 960. Both passed actual display probes on this Mac; 480 and 720 were marked unusable, including 480 HiDPI backed by 960 pixels. Forcing that mode returned CoreGraphics error 1001. These are observed macOS 26.5.1 results, not a universal documented minimum. An unavailable 800 mode falls back to 960. The synthetic monitor descriptor uses conventional pixel density.

USB carries acknowledged bands, up to 16 native rows or 8 Fast rows. Only changed rows are sent, aligned to two native rows or one Fast row. The acknowledged baseline advances only after the exact packet's ACK, so retries, newer capture frames, and quality changes cannot desynchronize it. An independent immutable snapshot and bounded latest pending frame prevent a growing queue. Same-session CAPS negotiation enables supported compression, cropping, and 2× scaling. Lossless updates use the smallest raw, RLE, or LZ4 encoding; Wi-Fi Fast mode can additionally use adaptive JPEG as described below. COBS, CRC-32, session nonces, sequences, and STOP/RELEASE match the receiver. Idle heartbeats maintain the three-second lease; ordinary care commands are never sent.

Synthetic on-device tests: a flat native UI fell from 2.06 seconds raw to 0.123 seconds compressed. A random full image took 2.01 seconds native or 0.586 seconds Fast. A two-row native update took 9 ms. Actual desktop refresh depends on changed content and capture cadence; these are transfer measurements, not promised frame rates. The PTY suite separately verifies exact reconstructed frames for clock, cursor, scrolling, and full-motion patterns.

Discovery initially sends only `?`, which older firmware ignores. Binary traffic begins only after a recognized firmware capability line. An `IDLE <nonce> <sequence>` beacon allows cleanup after a crashed host without creating a desktop; a new `REQUEST` is required to start one.

## Verification

The build runs sanitizer-enabled tests for image encoding, host packet encoding against the actual firmware parser, and the real host connection state machine against a pseudo-terminal firmware simulator. They cover complete native frames, cropped/LZ4/JPEG updates, exact idle refinement, dropped ACKs, duplicate requests, heartbeat, device button exit, host disconnect, and permission decline. JPEG transport tests decode with the actual firmware library. Tests do not open USB, create displays, or capture content. The bundled `--probe` option only checks runtime API availability.

`NSLog` records session readiness, the first acknowledged full frame, periodic frame counts, display creation/removal, and RELEASE cleanup. Creation diagnostics include active/online display IDs, AppKit screens, logical and backing mode dimensions, and ScreenCaptureKit display IDs. A hardware run is still required to verify display creation, Screen Recording approval, and physical transfer on a particular Mac.

The final build was verified end to end on macOS 26.5.1 on 2026-10-02: a 960 × 960 extended desktop was captured by exact display ID and its first complete 480 × 480 frame was acknowledged by the device. See [device verification](../../docs/verification.md) for evidence and limits.

The sloth icon is reproducible vector artwork. Regenerate its committed ICNS with `swift host/macos/icon.swift` from the repository root; the app build copies it into the signed bundle.

## Rotation (firmware v1.39)

Choose **Settings → Screen Rotation → 0 / 90 / 180 / 270 degrees**, then Save.
The saved clockwise orientation applies to every local screen and Remote Display,
including waiting/permission screens, and survives reboot. The default is 0°
(buttons across the top). Touch and tilt input map back into the same logical
space. The firmware advertises `DISPLAY ROTATION <nonce> <0..3>`; the companion
applies it before transmission, so firmware does not rotate received pixels twice.
During streaming KEY/BOOT continue to control volume; PWR ends the session.

The companion rotates the retained unmodified capture before delta/RLE planning.
An orientation change invalidates the acknowledged image and retires any
in-flight strip after its ACK, then redraws from row zero. The last unrotated
frame is retained so static desktops rotate without another capture callback.
Repeated beacons, stale sessions, and invalid angles cannot trigger a redraw.
The virtual monitor and its windows stay connected. Both 480 and 240 transfers
are supported. Old firmware retains its original orientation.

## Wi-Fi transport (v1.5 / firmware v1.13)

The first Wi-Fi Display connection obtains a 2.4 GHz SSID/password on the Mac
and sends ConfigureWiFi(type10, sequence0) over USB before HELLO. Firmware saves
the network and a stable TLS identity in a bounded, checksummed NVS record.
USB supplies the certificate SHA-256 pin, device ID, and authentication token;
the Mac saves that pairing under `org.moss.usb-display.devices` in Keychain.
Network.framework TLS requires the exact pinned leaf certificate, then exchanges
the token inside TLS. All subsequent controls, ACKs, pixels, and optional audio
use that authenticated stream. Removing USB does not end the session.

On later explicit Wi-Fi Display entries, Bonjour `_moss-display._tcp` advertises
the device ID, fresh session nonce, and endpoint. These advertisements are
untrusted: only a matching Keychain pairing can initiate pinned TLS, and an
authenticated matching request is required before creating/capturing a display.
USB bytes are ignored once TLS owns the firmware parser. There is no cloud relay,
plaintext fallback, trust-store modification, or physical-display capture.
The radio/discovery stop when the user exits the mode. **Wi-Fi setup / USB**
changes the device's saved network, retaining its valid pairing identity.
**Forget wireless pairing…** removes Mac trust, requiring USB to pair again.
The device's NVS record is not encrypted at rest.

Companion v1.5 still supports older firmware's Wi-Fi pixel/USB control transport.
Firmware v1.13 requires companion v1.5 for Wi-Fi mode; install them together.

Run `scripts/test-wifi-channel.sh` for actual loopback TLS authentication/pinning
tests (disposable test keys only). The build also exercises the real firmware
parser and host state machine over pseudo-terminals. Live throughput must be
measured on the user's network; chip benchmark figures are not app guarantees.

Companion v1.4 amortizes network round trips using at most thirty sequential
strips in a single TLS write (65,536-byte ceiling), committing the delta baseline
only after the final contiguous ACK. Whole-batch retry and partial-delivery
recovery are covered by the real-parser PTY tests. USB stays single-strip.
For a repeatable live workload, run `scripts/benchmark-display-host.sh --mode scroll --seconds 20`: it shows a gentle synthetic window only on the uniquely
named Moss virtual display, then closes. Modes `cursor` and `photo` isolate small
updates and textured motion. All imagery is generated locally; the helper does
not capture desktop content. `--list` only lists screen names/geometry.

## Adaptive compression (v1.4 / firmware v1.12)

The host compares raw, RLE, and standard raw-block LZ4 for each cropped band and
sends the smallest representation. Native crops retain even alignment; Fast
crops use logical pixels. Old firmware keeps full-width bands and its supported
encodings. USB remains one outstanding packet; Wi-Fi keeps one immutable batch
bounded by 64 KiB, committing only after its final contiguous ACK.

In Wi-Fi Fast mode, **Compression → Adaptive** can replace an expensive update
with one baseline 240×240 JPEG. The production encoder uses ImageIO quality 0.5,
rejects unsupported sampling and payloads above 15,360 bytes, and selects JPEG
only for updates above 20 KB when it saves at least 40%. The firmware accepts a
restricted baseline 4:2:0 format and uses the existing pet framebuffer; it never
presents a failed decode. Sharp mode and USB stay lossless.

After a JPEG ACK the host tracks the original snapshot for motion comparisons,
but separately marks the panel approximate. After 250 ms without source changes,
it forces a complete lossless refresh, even if ScreenCaptureKit is idle. Choosing
**Lossless · exact colors** also forces that refresh. Recovery remains at the selected 240×240 logical resolution, enlarged 2×; it does not switch Fast mode to native 480×480. Rotation, resolution changes,
disconnects, and in-flight ACKs preserve this rule. Both settings persist on the Mac.

`scripts/benchmark-image-codecs.sh` compares codecs using synthetic patterns.
`scripts/analyze-display-benchmark.py APP_LOG WORKLOAD_LOG` measures completed
changed-update cadence from live logs; its default two-second warmup excludes
window creation. Capture's 30 fps setting is a ceiling, not measured panel FPS.

Add `--audio-silence` to the drawing helper for an inaudible audio source while
testing **Audio on Moss**; verify the device's received/rendered PCM counters
increase before treating the run as an audio workload. `--audio-tone` retains
the optional audible tone. A missing virtual display now returns a nonzero
status instead of letting an automated benchmark report a false success.

## Saved networks (v1.3.2)

The setup dialog can save a network and make it preferred for future device
Wi-Fi Display requests. Passwords are stored in the login Keychain under the
app-specific service `org.moss.usb-display.networks`, never in project files,
logs, or user defaults. The app does not import macOS Wi-Fi passwords. Unsaved
connections are not saved on the Mac. Firmware v1.13 independently remembers
the most recently provisioned device network to support operation without USB.

Open **Manage Networks** in the Moss Display menu to add a network, update its
password, forget it, or change the preferred network and automatic connection
behavior. Automatic connection still requires entering Wi-Fi Display on the
device; it does not start a desktop merely because USB is attached.

## Optional audio (v1.5 / firmware v1.13)

Companion v1.5.2 shows a one-second volume bar over outgoing display pixels after
a hardware volume press or Mac volume selection. Another press restarts the timer,
including at mute/maximum. Its MIN/MAX bar and numeric level use the supported
0–60 range. The Mac composites it before rotation and restores the retained
desktop on expiry, including when the desktop is static. Firmware v1.15 renders
the same card locally on its waiting screen without allocating another canvas.

Companion v1.5.1 adds synchronization for the v1.14 hardware volume buttons.
Levels change in five-point steps from 0 (mute) to 60; the menu displays the
current percentage, including intermediate levels, and remembers it in user
defaults. Neither a hardware volume change nor a preset enables optional audio.
The device applies gain immediately when sound is active; session-bound VOLUME
notifications update the Mac without feeding the same change back unnecessarily.

**Audio on Moss** enables ScreenCaptureKit Mac/system audio, downsampled from
48 kHz Float32 mono to 16 kHz PCM16LE using AVAudioConverter. Screen video remains
filtered to the virtual display, but audio is application/system audio rather
than a promise that only windows on that display are audible. No microphone is
configured and the host's own audio is excluded. Volume choices are 20/35/50/60%,
with a conservative codec gain ceiling. Audio defaults off and uses 32 KB/s
before framing/TLS. Failure to start audio disables it while preserving video.

The host coalesces captured audio into bounded latest buffers and limits PCM to
one packet awaiting a device audio acknowledgment. Image and audio sequencing
remain independent. Audio does not renew the display lease. Image batches use
a 4 KiB soft budget with audio active (one larger valid packet is allowed).
The device lazily allocates its 12 KiB PCM ring, 2 KiB DMA buffers, and worker;
disabling audio releases them and mutes the amplifier. No ADC or I2S RX path is
enabled. Ring overflow drops old samples, and short fades soften discontinuities.
Shared CPU/network work can still produce gaps or lower refresh; this is not a
high-fidelity or synchronized-video audio system.

Run `scripts/test-audio-converter.sh`, `scripts/test-device-pairing.sh`, and the
normal firmware/host suites for conversion, pairing, framing, memory bounds,
and lifecycle coverage. Add `--audio-tone` to the display benchmark to generate
quiet 440 Hz pulses through the Mac's audio output, with no microphone or audio
file. Live speaker output and refresh still require a hardware test.
