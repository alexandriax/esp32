# Moss display protocol, version 1

The device remains a pet until the user explicitly chooses
**Menu → Utilities → Remote Display**. Firmware selects saved Wi-Fi or USB;
its connection options also allow an explicit USB choice or a network change.
The companion discovers the device over its native USB CDC serial port or a previously paired Bonjour endpoint, but creates and
captures a virtual desktop only after a device `REQUEST` or authenticated
`WIFI_REQUEST`. USB establishes initial trust; firmware v1.13 carries all Wi-Fi
session traffic over authenticated TLS. Legacy pet commands are isolated from
display traffic. The extensions below describe firmware v1.28
while retaining the original version-1 handshake and packet framing.

## Transport and isolation

Device-to-host messages are ASCII lines terminated by newline. Host-to-device
packets have this framing:

```
00 | COBS(header + payload + CRC32) | 00
```

Both delimiters are required when transmitting, including retries. A fresh
leading delimiter allows recovery after a dropped or interrupted write. Zero
bytes never appear inside COBS data. CRC32 is CRC-32/ISO-HDLC: reflected
polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`.
It covers the decoded header and payload, excluding the CRC itself.

The firmware latches a **global binary transport lock at the first zero byte**,
even outside display mode. While locked it routes every byte to this parser;
there are no ASCII exceptions. Button exit, host timeout, and malformed data
leave the lock set. Only a valid `RELEASE` event or device reset unlocks it.
Thus queued pixel bytes cannot become Feed, Play, Nap, or settings commands.
The portable parser does not implement the ASCII command gate; its caller must.

A companion initially sends only the read-only ASCII probe `?\n`, which older
pet firmware ignores. It must observe a recognized DISPLAY capability/status
line before sending any binary packet: old firmware does not understand the
binary lock and could interpret packet bytes as pet commands. A new device
periodically announces REQUEST while waiting, or IDLE while an old binary lock
remains set, so a fresh companion can safely discover and release a stale
session. Once bound, the companion sends only framed packets, including QUERY. Legacy
serial tools require the companion to finish `RELEASE`, or a device reset.

## Decoded header

All multibyte integers are unsigned **little endian**. The header is exactly
32 bytes; payload follows, then a four-byte little-endian CRC.

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 4 | ASCII `MOSD` |
| 4 | 1 | Protocol version: `1` |
| 5 | 1 | Packet type below |
| 6 | 2 | Reserved: zero |
| 8 | 8 | Session nonce |
| 16 | 4 | Sequence |
| 20 | 2 | Rectangle x |
| 22 | 2 | Rectangle y |
| 24 | 2 | Rectangle width |
| 26 | 2 | Rectangle height |
| 28 | 2 | Payload length |
| 30 | 2 | Reserved: zero |

The maximum payload is 15,360 bytes, maximum decoded packet 15,396 bytes, and
maximum COBS body 15,457 bytes, or 15,459 bytes with both delimiters. The
receiver decodes COBS in place in one 15,457-byte buffer. LZ4 borrows the
application's existing 240×240 RGB565 pet framebuffer instead of reserving a
permanent 15,360-byte output stripe. The complete receiver object is 15,520
bytes in the v1.28 ESP32-C6 build. Unknown versions/types, nonzero reserved
fields, inconsistent lengths, and bad CRCs are rejected.

Startup allocates a 16,384-byte USB CDC receive queue, reduced from 32,768
bytes. It holds a maximum delimited packet plus at least 512 bytes of control
headroom, enforced by a compile-time assertion. USB permits only one
sequenced packet outstanding, so an acknowledged stripe fits while panel DMA
is busy; Wi-Fi batches use their separate bounded transport queue.

## Packets

| Type | Name | Requirements and effect |
| --- | --- | --- |
| 0 | QUERY | Nonce zero, sequence zero, empty payload. Reports current mode; never starts a session or refreshes its lease. |
| 1 | HELLO | Current nonce, sequence zero. Eight-byte capability payload described below. Accepted only while waiting; starts the connected lease. |
| 2 | RECT | Current connected nonce, next sequence. Native RGB565 little-endian stripe described below. |
| 3 | PING | Current connected nonce, next sequence, empty payload. Refreshes lease; produces ACK. |
| 4 | STOP | Current nonce, next connected sequence, empty payload. Before HELLO, sequence zero is accepted. Ends display; preserves nonce for cleanup. |
| 5 | RELEASE | Current or retained nonce, any sequence, empty payload. Ends display and releases the binary lock. Nonce zero is valid before any session has begun. |
| 6 | HOST_STATUS | Current waiting/connected nonce, sequence zero, one-byte payload: `1` permission needed or `2` host error. Does not advance sequence or refresh/start lease. |
| 7 | RLE_RECT | Native rectangle geometry; payload contains RGB565 runs. Requires advertised RLE support. |
| 8 | SCALED_RECT | Logical 240×240 geometry; raw pixels expand 2×2 on the physical panel. Requires advertised scale2 support. |
| 9 | SCALED_RLE_RECT | Logical 240×240 geometry with RGB565 runs; requires RLE and scale2 support. |
| 10 | CONFIGURE_WIFI | Current waiting nonce, sequence zero, zero geometry; bounded credentials described under Wi-Fi setup. Does not start or renew the lease. |
| 11 | LZ4_RECT | Native rectangle geometry; one independent raw LZ4 block. Requires LZ4 support. |
| 12 | SCALED_LZ4_RECT | Logical 240×240 rectangle geometry with one raw LZ4 block; requires LZ4 and scale2 support. |
| 13 | SCALED_JPEG_FRAME | Complete logical 240×240 JPEG frame, expanded 2×2; requires JPEG240 support. |
| 14 | AUDIO_CONFIG | Connected current nonce, sequence zero, zero geometry; four bytes `version=1, enabled=0..1, volume=0..60, reserved=0`. Requires Audio and authenticated wireless control. |
| 15 | AUDIO_PCM | Connected current nonce, sequence zero, zero geometry; 2..3200 even bytes of mono 16 kHz PCM16LE. Requires active Audio and authenticated wireless control. |

Every packet except pixel types 2, 7, 8, 9, 11, 12, and 13 has all four
rectangle fields zero. HELLO capabilities are
`<uint16 width=480, uint16 height=480, uint16 maxPayload=15360,
uint8 pixelFormat=1, uint8 reserved=0>`, or hex `e0 01 e0 01 00 3c 01 00`.
Pixel format 1 is RGB565 little endian, red in bits 15..11, green 10..5, blue 4..0.

Native lossless rectangle types 2, 7, and 11 have positive `width`, even `x`,
`y`, `width`, and `height`, height from 2 through 16, `x+width <= 480`, and
`y+height <= 480`. The maximum decoded stripe contains 7,680 pixels. Type 2's
raw payload is exactly `width*height*2` bytes. Rows are top to bottom, pixels
left to right, with no padding between cropped rows. A full frame is normally
thirty 480×16 stripes. Even alignment is required by the AMOLED panel driver.

Scaled lossless rectangle types 8, 9, and 12 have positive `width`, height from
1 through 8, `x+width <= 240`, and `y+height <= 240`. These are **logical**
coordinates: each logical pixel becomes a 2×2 physical block, at physical
`x*2`, `y*2`, width `width*2`, and height `height*2`. Odd logical coordinates
and sizes are valid because physical boundaries are even. The maximum decoded
stripe contains 1,920 logical pixels. Type 8's raw payload is exactly
`width*height*2` bytes. Scaling trades detail for one quarter of the raw pixel
bandwidth.

Horizontal cropping is a separate capability (`8`): without it, the host must
use `x=0,width=480` for native stripes and `x=0,width=240` for scaled stripes,
including compressed stripes. Existing full-width packets remain valid. With
cropping enabled, the host packs just the changed horizontal range across each
band, respecting the geometry above. JPEG frames are always complete frames
and do not use the cropping extension.

RLE rectangle types 7 and 9 contain consecutive four-byte runs:

```
uint16 count (little endian), uint16 RGB565 pixel (little endian)
```

Each count is positive and expands to that many consecutive logical pixels in
row-major order; a run may cross a row boundary. The payload must be nonempty,
have a length divisible by four, and sum to exactly `width*height` logical
pixels. Zero counts, truncated runs, underfill, overflow, and extra trailing
runs are rejected before drawing. Every encoded payload remains bounded by
15,360 bytes. The receiver validates counts without allocating or expanding a
frame; the board expands directly into its existing DMA stripe. A host should
use RLE only when it is smaller than raw pixels, since varied images may
compress poorly. RLE is lossless for the selected logical resolution.

LZ4 types 11 and 12 contain one independent **raw LZ4 block**, without an LZ4
frame header, dictionary, or state carried between packets. Encoded length is
1..15,360 bytes; decoded output must be exactly `width*height*2` RGB565 bytes.
The receiver uses the vendored upstream LZ4 1.10.0 `LZ4_decompress_safe` with
that exact output bound and the caller-provided workspace. Session, state,
and sequence checks precede decompression, so stale or ineligible packets
cannot overwrite the pet canvas. Identical latest-packet retries acknowledge
without decompressing or writing the workspace again. Invalid blocks,
truncation, back-references outside the block, and incorrect output lengths
are rejected before accepting the sequence or renewing the lease. A malformed
block can modify scratch bytes, but never exposes a rectangle or presents them.
Raw, RLE, and LZ4 are lossless; the companion chooses the smallest useful encoding and
falls back to raw when compression does not help.

Type 13 has exactly `x=0,y=0,width=240,height=240`. Its JPEG payload is
256..15,360 bytes, begins with SOI (`ff d8`), and ends with EOI (`ff d9`). It
uses the usual CRC, nonce, and sequence rules. The stream parser checks those
bounds and markers, then emits `JpegFrame`; **the application must validate and
decode the image before presenting or acknowledging it**.

The supported JPEG subset is an 8-bit baseline, single-scan, 240×240 YCbCr
image with component IDs 1/2/3 and 4:2:0 sampling. Quantization and Huffman table
IDs are limited to 0/1; quantization values must be nonzero 8-bit values.
Segment lengths, canonical Huffman counts and symbols, referenced tables, and
scan fields are checked before the codec runs. APP0 is allowed; the host strips
APP1..APP15 and COM metadata. Progressive, EXIF, multiscan, other dimensions,
and unsupported markers are rejected. Optional restart markers must have the
expected cycle and count.

`sloth::jpeg_display::decode` uses vendored JPEGDEC 1.8.4 and checks decoded
block bounds, order, and complete coverage. It writes the entire image into
the application's existing 240×240 RGB565 pet buffer before any panel write;
display mode owns that buffer until exit. `board::decodeJpeg` borrows the panel's
DMA allocation for the temporary 17,884-byte decoder on ESP32-C6. The allocation
is sized for the larger of that object and the unchanged 15,360-byte DMA stripe.
The decoder is constructed and destroyed during each borrow; decoding is
rejected during panel presentation or sleep. Input/output/scratch overlap,
alignment, capacity and integer overflow are checked before decoding. This
saves approximately 15 KiB versus independent decoder and stripe allocations,
with no additional image framebuffer or per-frame allocation. On success,
the application presents the complete image at 2× scale and only then sends
ACK. On decode failure it exits display mode, returns to the Remote Display
connection flow, and sends no ACK for that frame; it must not continue with a
partially decoded image or an advanced host baseline. The local UI redraws
the reused buffer on exit. JPEG and LZ4 use this canvas sequentially, with
presentation complete before another packet is fed. JPEG is lossy and is
unavailable for native 480×480 frames.

The first sequenced packet after HELLO is 1. All pixel types, PING, and
connected STOP must use exactly the next sequence. Sequence wrap is not accepted; start a new
session before exhausting uint32. A retry of the **most recently accepted**
packet with identical type, sequence, and CRC is idempotent. HELLO retries
produce READY again; pixel/PING retries produce ACK without drawing again. An
identical repeated STOP produces STOP again. Older sequences and same-sequence
packets with different contents are rejected. CRC is transport integrity, not
authentication; the trusted companion owns the USB connection.

RELEASE deliberately ignores sequence because firmware button exit may race a
partially transmitted stripe. Its CRC, structure, and nonce still must match.
The receiver retains nonce and accepted sequence after cancellation/release.
A new user-initiated session replaces the nonce, so stale cleanup cannot close
that new session. A nonzero random nonce is required when entering display mode.

## Device replies and lifecycle

Nonces in replies are exactly 16 hexadecimal digits; sequences are decimal.

```
DISPLAY REQUEST <nonce> 480 480 15360
DISPLAY CAPS <nonce> 31
DISPLAY IDLE <retained-nonce> <last-accepted-sequence>
DISPLAY READY <nonce> 0
DISPLAY ACK <nonce> <last-accepted-sequence>
DISPLAY STOP <nonce> <last-accepted-sequence>
DISPLAY RELEASED <nonce> <last-accepted-sequence>
DISPLAY NACK <retained-nonce> <last-accepted-sequence> <error-code>
```

REQUEST is repeated while the device waits for the companion. Discovery while
idle reports IDLE, and IDLE is repeated while the idle binary lock remains set;
it must not trigger creation of a virtual display. The companion can use its
retained nonce for RELEASE after reconnecting, without entering display mode. READY
acknowledges HELLO. ACK for a pixel packet is sent only after its stripe or
complete JPEG frame has successfully been presented. HOST_STATUS may receive an
informational ACK with the unchanged last sequence; it does not belong to the sequenced acknowledgement stream.
NACK likewise reports the last accepted sequence, not a rejected packet's claim.

CAPS is an optional, separate four-field line. Its final field is a decimal
bitmask:

| Value | Capability | Enables |
| --- | --- | --- |
| 1 | RLE | Type 7; with scale2, type 9 |
| 2 | Scale2 | Logical 240×240 output, including type 8 |
| 4 | LZ4 | Type 11; with scale2, type 12 |
| 8 | Cropped rectangles | Horizontal cropping for all lossless rectangle types |
| 16 | JPEG240 | Full logical 240×240 JPEG frames, type 13 |
| 32 | WirelessControl | All session traffic uses the authenticated TLS channel |
| 64 | Audio | Optional output-only PCM16LE mono audio, types 14/15 |

Firmware v1.13 advertises `31` in USB mode and `127` in Wi-Fi mode. Firmware
v1.12 advertised `31` in both modes. New firmware Wi-Fi mode requires companion
v1.5; the new companion can still use older USB-control Wi-Fi firmware.
REQUEST and HELLO retain their original structure. A companion treats omitted CAPS as
zero, associate flags only with the matching current nonce, and ignore unknown
flag bits. Capabilities reset for each new session. The companion gates each
encoding and cropping independently; its JPEG path also requires the selected
logical resolution to be 240×240. Switching supported encodings does not reset
sequences or require another HELLO. A logical resolution change requires a
complete refresh before relying on dirty-region comparisons at the new size.

USB keeps one sequenced packet outstanding and waits for its matching
nonce/sequence ACK. Wi-Fi may send one bounded batch of consecutive lossless
rectangle packets and wait for its final ACK, as described below. The host
keeps a current frame and at most one newer replacement capture, retaining the
original source needed for rotation and exact refresh. Superseded captures
are dropped instead of growing a queue. Capture pacing is host policy, not a
protocol field. Unchanged regions may be omitted while PING keeps an idle
session alive. Native dirty bands group rows in pairs; scaled bands may start
and end on any logical row.

### Adaptive JPEG and exact refresh policy

The companion's optional adaptive compression applies only to Wi-Fi at logical
240×240 with JPEG240 support. It first estimates the changed bands' lossless
raw/RLE/LZ4 wire size, including supported cropping. If that estimate exceeds
20,000 bytes, it tries JPEG quality `0.5`. It sends a single JPEG frame only
when the result fits the payload limit and `JPEG bytes + 100` is less than
60% of the lossless estimate. Otherwise it uses the lossless rectangle path.
These thresholds are companion policy, not protocol requirements. Small
changes generally remain lossless; native 480×480 output always does.

After a JPEG ACK, the host records the original source snapshot for subsequent
change detection and marks the display as needing an exact refresh. Once the
source has been unchanged for at least 0.25 seconds and the current frame has
finished, it invalidates its delta baseline and resends the retained source
using only lossless rectangles. This refresh does not require a new capture
callback. Disabling adaptive compression also requests exact recovery after
any pending JPEG completes. JPEG is disabled during this refinement; completion
of a full lossless refresh clears the approximation state. The idle interval
is the trigger for sending, not a guarantee that the refresh finishes in that
time.

On device STOP, the companion immediately stops capture and removes its virtual
display, drops queued writes/frames, flushes pending host output, and sends a
fresh delimited RELEASE for that nonce. Already transmitted bytes remain
quarantined. On companion Quit/Disconnect, it stops capture and removes the
virtual display immediately, lets an outstanding packet finish if possible,
then sends STOP and waits for device STOP before RELEASE. If USB is gone, it
closes the connection and the device lease handles recovery.

## Timeouts and integration

The device waits indefinitely before HELLO; its ordinary screen idle policy
may independently cancel display mode. HELLO starts a **3,000 ms** lease, renewed
only by accepted HELLO, any pixel type, PING, or an identical latest-packet
retry. Invalid traffic, QUERY, and HOST_STATUS cannot keep an abandoned display alive. At the
lease boundary the receiver emits Timeout once, cancels the session, and
retains its nonce. The application restores the Remote Display connection
page and sends STOP.

An incomplete encoded frame has **500 ms total** from its first nonzero byte
to its terminating delimiter, independent of inter-byte activity. Expired or
oversized frames are discarded through the next delimiter. A complete corrupt
frame is rejected and the next delimited packet can proceed. All timing uses
unsigned millisecond subtraction and supports millis rollover.

`sloth::DisplayStream` exposes `begin(nonce, now)`, `cancel()`, `feed(byte, now)`,
`poll(now)`, `nonce()`, `sequence()`, `connected()`, `waiting()`, `lastType()`,
`error()`, `rectangle()`, and `setDecodeWorkspace(bytes, capacity)`. The caller
must poll while idle as well as feeding input. It must call `begin` only for a
local user's Remote Display choice.

Bind the decode workspace while idle, before `begin`, with no partial frame
in progress. It must provide at least 15,360 writable bytes, be aligned for
`uint16_t`, not overlap the receiver, and remain alive until unbound. Larger
buffers do not enlarge the protocol limits. `begin` and `cancel` retain the
binding; `nullptr, 0` unbinds it. Busy calls fail without changing the binding.
Other invalid idle bindings clear it and fail. Missing or insufficient scratch
rejects LZ4 safely; raw, RLE, JPEG, and audio parsing remain available. Firmware
binds its existing pet framebuffer once after allocation, and Remote Display
exclusively owns the canvas until exit. No extra framebuffer or allocation
occurs during decompression.

Rectangle pixels are valid until the next `feed`; present the region
synchronously in the main loop before accepting another byte. Never render from a DMA callback.
The receiver performs no display, serial, pet, or settings actions itself.
`DisplayRectangle` preserves logical `x`, `y`, `width`, and `height`; `scale` is
1 or 2 and `rle` identifies run encoding. Raw and RLE formats expose their
payload and encoded length. LZ4 exposes decoded RGB565 pixels in its bound
workspace, decoded length, and `rle=false`. All six lossless formats emit
`Rectangle`. Type 13 emits `JpegFrame` with the compressed JPEG pointer and
length, full 240×240 geometry, `scale=2`, and `rle=false`; it requires the
application validation/decode path above. Both payload and decoded-buffer
pointers are valid only until the next `feed`. Duplicates emit `Duplicate`
without another drawing payload. The parser itself does not send ACKs.

Errors are stable numbers: 1 frame too large, 2 frame timeout, 3 malformed COBS,
4 bad length, 5 wrong magic, 6 unsupported version, 7 reserved field nonzero,
8 checksum failure, 9 unknown type, 10 wrong nonce, 11 invalid session state,
12 invalid sequence, 13 invalid coordinates, 14 incompatible capabilities,
15 invalid payload. Zero means no error.

## Golden HELLO fixture

For nonce `0123456789abcdef`, sequence 0, the CRC is `7f589f01`. The full
delimited COBS packet is:

```
00 07 4d 4f 53 44 01 01 01 09 ef cd ab 89 67 45 23 01
01 01 01 01 01 01 01 01 01 01 01 02 08 01 01 05 e0 01 e0
01 03 3c 01 05 01 9f 58 7f 00
```

`tests/display_stream_test.cpp` validates this independent struct/zlib fixture,
maximum stripes, retry semantics, bounds, corruption, arbitrary noise, timeout
boundaries/rollover, cancellation during a frame, and retained-nonce cleanup.
`host/macos/wire_test.cpp` checks the actual companion encoder against the
firmware receiver across a complete native frame.

For the same nonce, a type 9 packet with sequence 1, logical row 239, height 1,
and one run of 240 red (`0xf800`) pixels has CRC `adbfc4fb`. Its complete framed
fixture is:

```
00 07 4d 4f 53 44 01 09 01 0a ef cd ab 89 67 45 23 01
01 01 01 01 01 02 ef 02 f0 02 01 02 04 01 01 02 f0 01
06 f8 fb c4 bf ad 00
```

Extended tests cover both fixtures, mixed-format sequences, unchanged legacy
handshakes, maximum RLE payload, row-spanning runs, each geometry's edges,
malformed counts/lengths/checksums, stale nonces, duplicate suppression, and
lease renewal/cancellation for each new format. Additional fixtures cover
cropped edges, LZ4 bounded output and malformed blocks, and JPEG packet framing.
Workspace fixtures cover binding lifetime, size/alignment/alias rejection,
canaries, maximum output, independent receiver storage, and no scratch writes
for wrong-session, waiting, wrong-sequence, or duplicate packets. Both Mac
wire and transport fixtures bind their own scratch and check LZ4 output
ownership; transport coverage includes Wi-Fi batching, JPEG, and audio.
`tests/jpeg_display_test.cpp` validates a procedural baseline JPEG against a
golden RGB565 checksum, rejects malformed headers and truncations, checks output
canaries under mutations, and verifies recovery with a valid image afterward.

### Orientation announcement (firmware v1.39; wire format since v1.10)

After REQUEST/CAPS and in periodic beacons, firmware sends
`DISPLAY ROTATION <session:16hex> <clockwise-quarter-turns:decimal>`. Turns are
0..3 and the session must match the current non-stopping connection. Repeated
values are idempotent. The value comes from **Settings → Screen Rotation**,
default 0, shared with every local screen and persisted in settings record v3.
Records v1/v2 migrate to 0 while retaining other preferences. Older firmware
used a display-only default of 1; v1.10–v1.13 also changed it with +/KEY.
Current KEY/BOOT control volume during streaming; PWR leaves the session.

The host rotates before changed-row detection/compression. Rectangle geometry
and ACK framing are unchanged. Firmware applies its configured orientation to
local full frames and native/scaled browser rectangles in the existing DMA
stripe, while received already-oriented desktop frames explicitly bypass that
transform. Native touch coordinates (including release) are inversely mapped;
tilt toys use the same inverse rotation. A changed angle requires a full redraw
from the retained original capture after any pending strip ACK. Older companions
that ignore the announcement cannot provide this orientation support.

### Wi-Fi setup and transport (firmware v1.13)

Physical Wi-Fi Display entry emits `DISPLAY WIFI_REQUEST <nonce> 480 480 15360`
instead of REQUEST. CAPS and ROTATION are unchanged. The companion first obtains
normal Screen Recording permission, then uses a selected saved network or asks
the user for network details.
`ConfigureWiFi` type10 uses sequence0, zero geometry, and payload
`ssidLength:u8,passwordLength:u8,SSID UTF-8 bytes,password UTF-8 bytes`. SSID is
1..32 bytes, password is 0 or 8..63 bytes, and embedded NULs are rejected.
It is accepted only in the waiting state and does not establish a display lease.
The main loop permits it only over physical USB in explicitly selected Wi-Fi
mode, once per session, copies to bounded RAM, and wipes temporary buffers.
The selected network is persisted before listening, so future explicit mode
entries can start without USB. **Wi-Fi setup / USB** bypasses the saved-network
start to accept updated credentials, preserving a valid saved identity.

After association/certificate generation, USB announces
`DISPLAY WIFI_READY <nonce> <IPv4> <port> <SHA256-DER:64hex> <token:64hex>`.
It also emits `DISPLAY PAIRING <nonce> <deviceID:32hex> <SHA256-DER:64hex> <token:64hex>`
over USB while listening. **Never log credentials or complete pairing lines.**
The Mac stores only validated pairing fields in its app-specific Keychain
service. The device stores a bounded, versioned, checksummed network/identity NVS
blob, distinct from pet data; this record is not encrypted at rest. Invalid
records fail closed rather than silently changing identity. Storage failures
must not be treated as empty storage. The Mac checks
the current nonce, pins the TLS leaf certificate, and sends `MOSS AUTH <token>\n`
inside TLS. Exact `MOSS AUTH OK\n` is required before HELLO. Once authenticated,
the device resets the parser for the session, announces WIFI_REQUEST/CAPS/ROTATION
over TLS, and ignores USB input while TLS owns the parser. All framed controls,
pixels, and audio travel over TLS; newline-terminated ASCII responses return on
the same channel. This prevents interleaved USB/network packet fragments. Host
reply parsing is bounded to 512 bytes per line and whitelists session messages;
PAIRING and WIFI_READY are never trusted from the network.

Only while the local user has chosen Wi-Fi Display, Bonjour advertises
`_moss-display._tcp` with `v=1`, `id=<32hex>`, and `nonce=<16hex>`. The advertisement
is an untrusted endpoint hint. A matching Keychain pairing, exact certificate
pin, TLS token exchange, and authenticated matching WIFI_REQUEST are required
before capture starts. A saved-network USB beacon `DISPLAY WIFI_SAVED <nonce> 1`
lets the companion wait instead of reprovisioning. Listening persists until mode
exit. After authentication the listener closes and discovery is freed to reclaim
its task, buffers, and sockets during streaming; the next local mode entry
recreates it. The radio stops on exit. No credentials or trust keys are broadcast.

The worker exposes authenticated bytes through an 8KiB bounded queue; the main
loop validates, decodes, and presents pixel packets before TLS ACK. Network
failure ends the mode. Removing USB detaches only the serial handle while an
authenticated wireless session is active. Host Disconnect sends sequence-independent
RELEASE over TLS after any pending write. Device exit may close TLS before its
best-effort STOP drains, so EOF also removes the host display and capture. A new
local Wi-Fi Display entry creates a new nonce and permits cable-free reconnection.
There is no unencrypted or USB pixel fallback after a network failure.

The current companion batches at most 30 consecutive lossless rectangle
packets into one TLS send, bounded to 65,536 encoded bytes. USB remains one
rectangle per send. JPEG uses a single full-frame packet.
With audio active, the batch has an 8,192-byte soft ceiling: a single valid
larger rectangle/JPEG remains allowed. Image and audio writes are serialized.
Each rectangle retains its own CRC, sequence and device ACK. Only the final
sequence ACK commits the entire immutable batch to the host's delta baseline;
the receiver's contiguous-sequence rule proves all preceding packets succeeded.
A whole-batch retry may produce harmless BadSequence responses for older already
accepted packets, followed by the final duplicate ACK. Partial delivery retries
resume through the missing sequences. Rotation/quality changes wait for the
final ACK and invalidate the old baseline. No USB heartbeat interleaves an
unacknowledged network batch. Independent sequence-zero audio may be interleaved
between complete writes without changing the image retry baseline.

### Optional audio

Firmware v1.14 emits `DISPLAY VOLUME <nonce> <0..60>` on a hardware volume
press (right/KEY +5; left/BOOT -5, clamped). Gain changes locally when audio
is active; changing volume never enables audio. The companion accepts only
strictly shaped messages for its current, non-stopping session on the owning
transport, including the pre-HELLO waiting state. It updates the desired volume,
Mac menu, and saved preference without ordinarily echoing AUDIO_CONFIG. An old
configuration acknowledgment cannot override a newer VOLUME or Mac selection;
if an older in-flight configuration could overwrite the device gain, the host
sends the latest desired setting afterward. AUDIO's volume field acknowledges
configuration rather than setting the user's desired preference.

Companion v1.5.2 composites a one-second volume card into outgoing pixels on
VOLUME notifications or Mac volume selections. The source desktop stays intact;
expiry restores its pixels even if capture has stopped producing changed frames.
The card is drawn before the existing rotation transform, at the chosen transport
resolution, and needs no new packet type or device framebuffer. Firmware v1.15
uses the same artwork and a local timer while its own waiting screen is visible.
The displayed range is 0–60, and pressing again restarts the timeout.

AUDIO_CONFIG and AUDIO_PCM do not advance sequence, replace the last image
type/CRC, or renew the session lease. Audio alone cannot keep an abandoned
desktop alive. Malformed payloads, wrong nonce/state/sequence, and nonzero
geometry are rejected. The application additionally requires authenticated
network ownership; USB cannot activate audio.

Configuration replies are `DISPLAY AUDIO <nonce> <active:0..1> <volume:0..60> <error:0..1>`.
After an accepted PCM packet is copied into the ring, firmware defers
`DISPLAY AUDIO_ACK <nonce>` until there is room for another maximum-size packet.
This is playback-buffer credit, not just receipt acknowledgment. The host permits one unacknowledged PCM packet and
coalesces at least 1,600 bytes (50 ms) before sending, avoiding a growing TCP audio
queue. Audio is not retransmitted; TLS provides reliable ordered delivery. Capture
and transport each retain at most 6,400 bytes of recent sound. Disabling audio
clears pending sound; an ordered configuration reply gates later re-enabling.

The output-only driver uses a 12,288-byte ring, 240 ms prefill, four 16 ms DMA
blocks, and short fades for gaps/dropped samples. It never configures I2S RX or
microphone ADC capture. Resources are allocated only while enabled and released
on disable/exit. Once per second while active, numeric telemetry reports:

```
DISPLAY AUDIO_PERF <nonce> <queuedSamples> <underruns> <overflows> <droppedSamples> <writeErrors> <freeHeap> <minimumHeap> <receivedSamples> <renderedSamples>
```

Sample counters wrap modulo 2^32. Rendered samples count consumed source PCM,
excluding generated silence/fade-out; received samples include those later
dropped. Minimum heap is since boot, not the current audio interval. This allows
hardware measurements to distinguish lost input, queue overflow, and playback gaps.

Firmware v1.11.1 emits an optional numeric performance sample at most once per
second while authenticated Wi-Fi rectangles arrive:

```
DISPLAY PERF <nonce> <elapsed_ms> <rectangles> <wire_bytes> <feed_us> <panel_us> <ack_us> <tls_us> <tls_calls> <socket_bytes> <socket_again> <queue_full> <queue_high> <rssi> <heap>
```

Counters are cumulative unsigned 32-bit values (compare modulo-2^32 deltas).
`feed_us` includes panel drawing and ACK writes; subtract their deltas to estimate
parser/decode time (including JPEG decoding when used). Timings are wall-clock
durations and may include task preemption; they are not mutually exclusive CPU
utilization measurements. Socket bytes also
include TLS setup and record overhead. `queue_full` counts one-tick waits and
`queue_high` is a byte high-water mark. RSSI is the association-time signed dBm
sample; heap is current free bytes. The companion accepts only exactly shaped,
current-session, active Wi-Fi samples, then logs normalized numbers. No pairing
secrets or pixel contents are included.

## Automatic Remote Display entry (v1.16; menu updated in v1.28)

**Menu → Utilities** has one **Remote Display** entry. Firmware chooses saved
Wi-Fi first, otherwise connected CDC USB, otherwise its local Wi-Fi Networks
picker. The
picker is also available directly under **Menu → Wi-Fi Networks**, with
scan/select, manual entry, saved-network reconnect, and confirmed Forget. Its
credential editor retains the TLS identity when changing or forgetting a
network; first trust still travels over USB. No pairing secrets are added
to discovery or UI diagnostics.

Before any image is displayed, failed Wi-Fi startup or a companion discovery
deadline may switch to USB. Firmware stops the worker and starts a fresh nonce;
the companion discards old session state and validates a new handshake. Once
pixels arrive, there is no automatic transport migration. USB-without-host and
Wi-Fi-without-host screens offer retry/network options while continuing to
advertise until the user leaves or the existing idle timeout ends the mode.

KEY/BOOT navigate connection and network entry screens; they change speaker
volume only while desktop pixels are visible. A short PWR returns a live desktop
to connection options, then Utilities. Nested keyboard/network screens
unwind one level at a time. No wire packet or authentication changes are needed.

### C6 foreground network budget (v1.38)

The production link wraps `esp_wifi_init` to cap dynamic RX/TX pools at eight
packets each, including Arduino station joins and passive scans. It preserves
the caller's other configuration and any smaller positive limits. This bounds
packet-burst allocations on the no-PSRAM C6; it does not reserve eight buffers
permanently. The [ESP-IDF Wi-Fi configuration reference](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-reference/kconfig-reference.html#config-esp-wifi-dynamic-tx-buffer-num)
documents these dynamic packet pools.

With audio active, companion image batches target 4 KiB instead of 8 KiB, so
audio can be interleaved more frequently. A single larger image packet is still
allowed. With audio enabled, JPEG quality steps down from 0.25 to 0.15 as needed
to meet a 4,096-byte payload limit; larger candidates use lossless strips with
audio interleaved. Without audio, the existing quality 0.5 / 15,360-byte JPEG
limit remains. Idle refinement still restores exact pixels. The PCM ring adds
6 KiB only while audio is enabled and releases it on exit.

The Wi-Fi worker uses a 10 KiB stack. The preceding 12 KiB build measured
5,468 bytes unused through TLS handshake and streaming; the 2 KiB reduction
leaves roughly 3.3 KiB of observed margin. `APP_MEMORY` reports the worker's
stack minimum and sampled low heap after a session so this can be audited.

After each accepted PCM packet, optional authenticated
`DISPLAY AUDIO_BUFFER <nonce> <queuedSamples>` telemetry lets the companion
estimate playback reserve. During an active audio source, low reserve defers
image packets for at most 150 ms to allow sound to refill; stalled capture can
never freeze video. It does not change image ACKs or the heartbeat lease. Old
companions ignore this extra status line; new companions retain legacy pacing
when firmware does not provide it.
