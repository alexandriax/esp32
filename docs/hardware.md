# Waveshare ESP32-C6-Touch-AMOLED-2.16

Verified against the board label, ESP USB chip identification, official schematic,
and Waveshare example commit `294543798f1a44e2f2c4d2976522323f2beee11d`.

| Function | Connection |
| --- | --- |
| +/KEY | GPIO10, active low, external pull-up |
| BOOT/- | GPIO9, active low, external pull-up; ROM boot strap at reset |
| PWR | AXP2101 hardware power control |
| I2C | SDA8, SCL7 |
| AXP2101 | I2C address 0x34; chip ID register 0x03 = 0x4A |
| CO5300 panel | 480x480; SPI2 QSPI clock0, D0=1, D1=2, D2=3, D3=4, CS15 |
| Display reset | AXP2101 ALDO3; enable bit2 in 0x90; voltage in 0x94 bits4:0 |
| CST9220 touch | I2C 0x5A, reset GPIO11, interrupt GPIO5; polled |
| QMI8658 accelerometer/gyro | I2C 0x6B (fallback 0x6A), WHO_AM_I=0x05 |
| PCF85063A RTC | I2C 0x51; VDD is VCC-RTC from AXP2101 VRTC pin28 |

Only the display-reset rail is configured: ALDO3 3.3V, then enabled/disabled/enabled
at 100ms intervals. Other power rails and battery charging settings are preserved.
The vendor's SH8601-compatible transport uses the CO5300 initialization commands
from this board's Arduino example, including the 480x480 address range.
Flush rectangles are even-aligned. RGB565 bytes are swapped at transfer time.
A DMA completion semaphore prevents reuse of a buffer before transfer completes.

The application uses a 240x240 framebuffer scaled 2x. Wi-Fi and speaker output
support remote display and game features; microSD supports read-only storage
diagnostics. V1.28 adds passive Wi-Fi and Bluetooth LE discovery. Microphone
capture remains unused. Brightness dims after inactivity.
Core NVS is relocated to 0xFE0000 so Arduino startup does not initialize the old
factory NVS at 0x9000. Pet state uses its own 64KB NVS partition at 0xFF0000.
Both tail regions were verified erased in the original dump. All original flash is backed
up locally before installation; binaries and backups are excluded from Git.

V1.28 reserves an **8 MiB** factory application partition at `0x10000`
(`0x800000` bytes, ending at `0x810000`). The build's maximum image size matches
that reservation. Core NVS at `0xFE0000` and pet NVS at `0xFF0000` remain 64 KiB
each; their offsets and data formats are preserved. The original factory NVS
at `0x9000` is also untouched. The normal flash script writes the bootloader,
partition table, and actual application image, verifies their readback, and
never issues a whole-chip erase. The larger app reservation is not a claim
about the installed image size or free file storage.

## Touch and motion (v1.1)

Outside radio detail pages, the QMI8658 runs accelerometer-only at +/-4g and
125Hz. CTRL1=0x40 enables address
increment with little-endian samples; CTRL2=0x16 selects range/rate; CTRL7=0x01
enables acceleration. Poll STATUS0 bit0 before reading six bytes at 0x35, scale
by 8192 counts/g. The application polls at roughly 50Hz, including between completed
display DMA stripes; final-device sampling was approximately 48 samples/second,
with 24ms maximum gaps in the initial attached-monitor interval.
Gravity is low-pass filtered before detecting motion peaks. In v1.3, three
peaks above 1.2g are required within 1.2 seconds, at least 100ms apart, with motion
below 0.35g to rearm between peaks. Single bumps, gentler oscillations, and gradual
orientation changes are rejected in host tests. Rewards require a quiet interval
and four-second cooldown. A detected shake during sleep is ignored by the care
rules and leaves the display brightness and save queue alone.

CST9220 report 0xD000 is decoded only after a valid 0xAB marker. Release/contact
status, packed coordinate fields, and bounds are checked before converting to
the logical 240x240 UI coordinates. Tap hitboxes use the same geometry as the
drawn buttons, with disjoint padded targets in one row.
In v1.6, logical x targets are Feed [11,73), Play [73,135), Nap [135,197),
and Gear [197,231), all at y=[190,229). Settings pages
use the same shared layout for their drawn controls and touch hitboxes.
Adjacent targets do not overlap. Holds and drags do not trigger repeated care actions. Sensor I2C
transactions use a bounded 10ms timeout; failed reads never synthesize releases.

## Real-time clock

The RTC stores UTC with its two-digit year interpreted as 2000–2099. Ordinary
reads do not write registers or reset the clock. Register 0x00 and the complete
seconds-to-years calendar at 0x04–0x0A are read in one burst, so a rollover cannot
mix fields from different seconds. Transactions use a 10ms timeout. A failed
transaction reports `Unavailable`; STOP, external test mode, the seconds OS flag,
invalid BCD, or an impossible calendar date reports `Invalid`. Existing 12-hour
clocks are decoded correctly. These checks follow the
[NXP PCF85063A datasheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf),
sections 7.2–7.4.

Setting the clock requires explicit host-supplied UTC; firmware never substitutes
its compile time. The driver sets Control_1.STOP (bit5), selects 24-hour mode
(bit1 clear), writes all seven calendar bytes together with seconds.OS (bit7)
clear, and verifies the complete packet before restarting. A partial or failed
write can leave STOP set, making the uncertain clock visibly invalid. CAP_SEL=1
matches the 12.5 pF setting in the board's
[official RTC example](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16/tree/294543798f1a44e2f2c4d2976522323f2beee11d/02_Example/Arduino-v3.3.3/03_I2C_PCF85063).
Alarm, offset, and RAM registers and PMIC charging settings are preserved.

The [board schematic](https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf)
connects the RTC to AXP2101's VRTC output, separately from the switched 3.3V rail.
The [AXP2101 datasheet](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16/blob/294543798f1a44e2f2c4d2976522323f2beee11d/01_Arduino_Libraries/XPowersLib/datasheet/AXP2101_Datasheet_V1.4_en.pdf)
specifies that RTCLDO remains powered in the off state, including with only the
main battery present (page 19, section 6.5.4), and through a system reset (page 21).
The schematic shows only a capacitor on VBackup/VBAT2, with no separate backup
cell. Thus PWR-off can retain the clock while USB or the main battery supplies
the PMIC; removing all supplies loses continuity. No additional PMIC rail or
backup-charger configuration is needed. Physical PWR-off continuity should be
checked on the attached device; an ESP software reset alone does not test it.

Battery diagnostics are read-only: AXP2101 register 0x68 bit0 enables battery
detection; status register 0x00 bit3 reports battery presence. A clear presence
bit is not conclusive if detection is disabled. Both definitions are verified in
the bundled XPowers `enableBattDetection()` and `isBatteryConnect()` methods.

## Clock and battery display (v1.3)

Eastern time is display-only; RTC storage and care stay in UTC. The formatter
handles EST/EDT using the [current U.S. daylight-saving rules](https://www.nist.gov/pml/time-and-frequency-division/popular-links/daylight-saving-time-dst)
and the earlier April/October rules for supported years before 2007. Tests compare
the full supported century against the host's America/New_York time-zone database,
including spring's skipped hour, fall's repeated hour, noon, and midnight.

Battery reads use the shared I2C bus at AXP2101 address 0x34, with a bounded 10ms
transaction timeout and no configuration writes. Status 0x00 bit5 reports good
VBUS power; 0x01 bits6:5 report charge direction (01 = charging). Battery presence
is trusted only when detection is enabled in 0x68 bit0. The gauge must also be
enabled in 0x18 bit3 before a full-byte percentage from 0xA4 in range 0–100 is
displayed. Disabled gauges, invalid values, and I2C failures yield unknown charge,
never a percentage estimated from voltage. Main-loop polling occurs every five
seconds, outside display input callbacks; charging and power settings are unchanged.

## Display sleep and hardware wake (v1.4)

Idle policy dims at 60 seconds and sleeps the display at 120 seconds, both measured
from the last accepted interaction. Off stays latched until a hardware press (or
explicit USB wake diagnostic). The first press is consumed; held and simultaneous
KEY/BOOT presses are suppressed until release so they cannot accidentally perform
care. Touch and motion polling stop while off, and gesture state is cleared for
wake. A finger already held on the screen must lift before a new tap can count.

The [CO5300 datasheet](https://files.waveshare.com/wiki/common/CO5300_Datasheet_V0.00.pdf),
sections 7.5.11–12, specifies SLPIN/SLPOUT timing and explains that sleep stops
scanning, the internal oscillator, and converter. The board sends DISPOFF (0x28)
then SLPIN (0x10), waiting 120ms. Wake sends SLPOUT (0x11), waits the board's
conservative 600ms initialization delay, and restores the existing panel parameters
and brightness. A fresh full frame is uploaded before DISPON (0x29) and its 100ms
settling delay. DMA must finish before power transitions; no frames are sent while
asleep. No ALDO rail cycling, RTC reset, or whole-device shutdown is involved.

KEY and BOOT remain active-low GPIO10/GPIO9 with 35ms debounce. PWR is wired to
AXP2101, so wake polling enables its falling-edge IRQ using a preserving RMW of
0x41 bit1. Latched status is read at 0x49 bit1 and acknowledged by writing only
0x02 (write-one-to-clear). Other enable/status bits, charging configuration,
shutdown control (0x22), and long-hold timing (0x27) remain unchanged. Polling every
50ms catches short taps without a duplicate release event or an ESP interrupt pin.
I2C failures are bounded and repeated uncleared latches are suppressed.

The ESP continues running for hardware-button polling, UTC care, minute saves,
and USB diagnostics. No deep-sleep current or battery endurance claim is made.

Sources:
- https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16
- https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16/Arduino
- https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf
- https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16/tree/294543798f1a44e2f2c4d2976522323f2beee11d/02_Example/Arduino-v3.3.3/09_LVGL_V9_Test

## Persistent customization (v1.5)

Preferences namespace `moss-v1` now contains an independent `settings` key, while
`state` retains the unchanged v3 care record. Settings use a 28-byte PETC v1 record
with CRC32, bounded name validation, stable enum/zone IDs, and atomic decode.
Missing or invalid configuration falls back to Sloth/Moss/Eastern/Jungle with
the subtitle visible. Save writes outside display DMA callbacks and applies the
draft only after successful storage; errors retain the draft for retry.

The 14-zone formatter uses explicit UTC arithmetic and never modifies global TZ
or the care timeline. Tests compare every date in 2000–2099 and exact transition
seconds against host IANA data. Rules follow the IANA sources linked in
`local_time.cpp`; future dates project the currently published rules. The clock
face omits the zone abbreviation; USB diagnostics retain it for verification.
The battery lightning icon reflects valid VBUS power, rather than charge direction.

## Additional scenes and compact controls (v1.6)

NYC, Space, Island, and Under the Sea append scene IDs 3–6, retaining the existing
Jungle/Meadow/Night IDs and the PETC v1 storage format. Scene and timezone pickers
share paging behavior when their choices exceed five. The selected scene opens
on its own page; next/previous bounds and Back leave the draft selection intact.
The home screen gear is an icon beside Feed/Play/Nap in a single control row.

## Shake calibration and short PWR taps (v1.7)

The settings record advances to PETC v2, using formerly reserved byte 10 for the
five-level shake sensitivity. V1 records migrate to Normal while retaining the
name, animal, scene, timezone, and subtitle. Care records remain unchanged.

Sensitivity thresholds are 1.60, 1.20, 0.85, 0.60, and 0.40 g of gravity-compensated
motion, from Very Low to Very High. The detector recognizes opposite-direction
peaks even if polling missed the near-zero crossing. Three alternating peaks,
minimum 100ms spacing, a 1.2s window, four-second cooldown, and 300ms settling
reduce accidental repeats. A separate resting-magnitude estimate accounts for
sensor bias when distinguishing translational movement from rotation.

The Motion page samples a separate test detector at the draft sensitivity. It
never calls care actions or saves; only main-menu Save applies the preference.
Raw g, magnitude, filtered motion, peak, tilt, threshold, count, and measured
sample rate are snapshots supplied to the renderer. No valid sample for 250ms
marks readings stale. The bubble uses filtered gravity (atan2); it is a visual
level rather than a calibrated navigation instrument. The driver still uses
±4g/125Hz accelerometer-only configuration; gyro and other IMU functions are not
enabled by this page.

PWR now uses the AXP2101 short-press IRQ: register 0x41 bit3 (0x08) enables it,
and status 0x49 bit3 is acknowledged by writing only 0x08. PMIC-classified short
taps toggle the panel, rather than acting on the initial button-down edge.
The long-press and edge IRQs are ignored, and all unrelated IRQ enable/status
bits are preserved. Shutdown/timing registers 0x22 and 0x27 are never written.
Sources are the bundled AXP2101 v1.4 datasheet pages 19–20, 42, and 44, and
XPowers `isPekeyShortPressIrq()` / `XPOWERS_AXP2101_PKEY_SHORT_IRQ`.

PWR dispatch takes priority over simultaneous KEY/BOOT input. A guard consumes
held or late-debounced GPIO presses before they can change display state; all
keys must release before ordinary actions resume. Panel operations and saves
remain outside display callbacks. The USB `b` command exercises this same toggle
path without changing care or the open menu.

## Forest Fidget (v1.23; tilt correction in v1.28)

Forest Fidget reuses the existing QMI8658 and CST9220 drivers with no sensor,
power-rail, or interrupt configuration changes. The regular 20ms input poll
continues between completed display stripes, sampling both motion and continuous
touch. V1.28 removes the incorrect Y negation: raw X maps to screen-right X and
raw Y to screen-down Y with the buttons across the top. This corrects the
previous top/bottom inversion. The projection has not been independently
checked with a physical directional tilt fixture.

The portable engine advances fixed 20ms steps, limits catch-up to 200ms, and uses
fixed arrays for bodies and sand trails. Invalid/nonfinite motion is rejected;
one second without a fresh sample removes tilt forces, leaving touch usable.
Leaving, cycling, and panel wake cancel contacts; wake also rebases simulation
time. Touch navigation uses the same top-strip boundaries as the rendering.
Each new visit starts fresh; no game records, scores, or audio slots are added.
The mode stays bright while open and exits directly on a short PWR tap. Pet
elapsed-time accounting continues, but its shake recognizer receives no fidget
motion. Existing PMIC long-press behavior remains unchanged.

## Storage diagnostics (v1.38)

The Storage settings page reports physical onboard flash capacity, the current
application image length against its reserved partition, and occupied NVS slots
against total entry space across `nvs` and `pet_nvs`. A slot is 32 bytes and
includes metadata. The unused flash gap is not a mounted file drive. Reading
statistics does not initialize, repair or erase NVS.

The microSD socket shares SPI2 CLK0/MOSI1/MISO2 with the QSPI display and uses
CS6. Its card-detect contact is not connected to an ESP GPIO. A nonresponding card
therefore cannot be distinguished from an empty socket. The backend uses IDF
SDSPI device registration on the existing bus; it never reconfigures the bus,
PMIC rails, audio pins, or display pins. Before the first LCD command at boot,
it makes a bounded attempt to put an inserted card into SPI mode. A manual scan
shows Checking, then holds LCD transfers throughout card initialization and
the filesystem scan. Concurrent SD polling and LCD DMA reproduced a C6 SPI HAL
assertion on a long FAT scan; the display resumes only after filesystem and
SD-device cleanup. Hardware input continues to be serviced, while visual changes
wait for the probe to finish. Onboard results are published before starting the
SD worker. Initialization is bounded to four seconds and the normal filesystem
metadata read to two seconds. The normal Storage page does not scan the FAT.

A private FatFS disk adapter rejects sector writes and trim requests, including
filesystem metadata writes. Capacity comes from validated filesystem geometry.
Usage uses the mounted FAT32 FSInfo count when valid and is labeled EST because
it can be stale; an absent/invalid count shows NOT COUNTED without walking the
whole card. An explicit `storage_status::read(callback, true)` diagnostic still
provides a measured FAT recount with a sixty-second budget. Probes release the
private filesystem and SD device on success or failure. Unsupported/unreadable cards report unavailable usage and are never
formatted. No card contents are listed or logged.

Sources: [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf),
[Espressif shared SPI startup sequence](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-reference/peripherals/sdspi_share.html),
and the installed Arduino ESP32 3.3.0 / ESP-IDF 5.5 headers and storage APIs.

## Passive radio explorers and gyro guidance (v1.28)

Utilities groups Remote Display, Wi-Fi Explorer, and Bluetooth Explorer. A
single radio owner keeps discovery separate from the display transport. Scan
workers stop before ownership changes; leaving an explorer or sleeping the
panel requests shutdown. Discovery never saves observed identifiers to NVS.
No microphone input, Wi-Fi association, BLE pairing, GATT connection, or active
BLE scan request is used by these tools.

Wi-Fi discovery uses passive 2.4 GHz scans with hidden SSIDs included. Selecting
an access point narrows later scans to its channel/BSSID. The worker uses a
fixed 24-record buffer and releases SDK scan results after each scan. Arduino Wi-Fi
stays off while the scanner owns the raw IDF driver; its scan-done callback
otherwise consumes results and allocates an unbounded list. The shared event
loop stays available for later Remote Display sessions. BLE uses
NimBLE passive advertising discovery, with repeated observations enabled so
RSSI can update. It shows only advertised fields, retaining at most 64 payload
bytes and marking truncation. Lists contain at most 24 entries, keyed by
address/type in stable insertion order; Refresh clears discovery results.
Bluetooth Classic discovery is not supported. Driver callbacks copy bounded
metadata and never touch the framebuffer, filesystem, or preferences.

Only an open radio detail page enables the gyro. CTRL3=`0x56` selects
±512 degrees/s and 117.5 Hz, with 64 counts per degree/s; CTRL7=`0x03` enables
both sensors. The accelerometer remains ±4 g; in this six-axis mode its ODR
follows the gyro clock. The driver requires both STATUS0 availability bits and
reads all twelve axis bytes together from `0x35`–`0x40`. Five fresh samples are
discarded after enabling. Failed or incomplete reads leave all outputs alone.
Leaving details returns CTRL7 to `0x01`; uncertain configuration/rollback is
retried. Transactions retain the shared bus's bounded 10 ms timeout and restore
its previous timeout afterward. See the
[QMI8658C register tables and mode timing](https://files.waveshare.com/wiki/common/QMI8658C_datasheet_rev_0.9.pdf),
sections 5.4–5.7 and 7.2.

The sweep estimator projects gyro rotation onto measured gravity, rejecting
large acceleration, fast turns, flat orientation, and stale samples. It matches
fresh RSSI observations to recent gyro headings and requires at least ten
matched samples, turns in both directions, a 25-degree span, strong correlation,
and a signal change of at least 6 dB before showing a left/right hint. Otherwise
the UI says No Clear Direction. The display explicitly labels this as an RSSI
hint rather than a bearing. Multipath, hand position, and antenna response can
produce misleading correlations; neither physical bearing accuracy nor distance
measurement has been established.

## Pet and game presentation (v1.28)

Sun Conure appends animal ID 3, preserving the existing Sloth/Cat/Frog IDs and
settings record format. Ambient movement, grooming, scene accents, and brief
visual dozes derive from rendering time; they never change care values or the
saved sleep flag. The framebuffer, care controls, and HUD geometry remain fixed.
Pong, Tetris, Leaf Sweep, and Forest Fidget add material shading and bounded
animation cues without additional framebuffers or gameplay/scoring changes.

The Tetris left BOOT button acts on release for a short tap: move right once,
wrapping only at the right boundary. Holding for 650 ms drops the current piece
once, with no initial lateral move or drop bonus. Holding through the drop and
then releasing cannot act on the replacement piece. Debounced edges captured
during display transfers retain their piece generation; scene changes, pause,
wake, and a naturally spawned replacement cancel a pending gesture. Right KEY
still rotates once per press. Touch Left/Right remain bounded at the walls;
touch Drop locks one piece. The 350 ms lock delay and reset budget are unchanged.
PWR still pauses/backs out on a short tap and keeps the existing hardware
long-press behavior.

The pinned Arduino 3.3.0 / IDF 5.5 C6 controller archive also contains a scan
allocation defect: it reserves a three-byte prefix and then writes a four-byte
pointer at offset three. Device heap poisoning caught the resulting overwrite
on shutdown; disassembly of the linked controller confirmed the allocation and
write. A linker wrapper reserves seven bytes only for that `(3, 1)` allocation,
leaving allocation failure, ownership and freeing with the vendor driver. Heap
checks remain enabled. The build rejects another target or SDK version until
this workaround is reviewed. Its regression test performs the exact pointer
write under ASan, and the release link is checked for calls to the wrapper.
[Upstream issue 12821](https://github.com/espressif/arduino-esp32/issues/12821)
reports the same signature on C5; the C6 evidence here comes from this device
and its installed archive, rather than assuming the report covers this board.
