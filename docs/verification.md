# Device verification

Verified on the connected Waveshare ESP32-C6-Touch-AMOLED-2.16, ESP32-C6 QFN40
revision 0.2, 16MB flash, native USB Serial/JTAG.

## v1.0 baseline — 2026-10-01

- Full original flash read: 16,777,216 bytes; device-side digest comparison passed.
  SHA-256: `6e346a9ba8dc2dd36cb5e5a59dbb9ef291d806ddea46e2ed6130b28f01ffa422`.
- Host tests: care boundaries, elapsed-time overflow/equivalence, nap recovery,
  record validation, CRC corruption, and saved fractional timing all passed.
  Strict C++11 warnings, AddressSanitizer and UndefinedBehaviorSanitizer passed.
- Idle, feed, play, and sleep previews rendered with the firmware's graphics code
  and passed visual inspection.
- Arduino-ESP32 3.3.0 build: 339,283 bytes of code/data in a 3MB app partition;
  static globals 15,784 bytes. Runtime free heap: 296,052 bytes, including display
  framebuffer and DMA buffers already allocated.
- Bootloader, partition table, and application readback digests all matched.
- Boot log: PMIC ID `0x4A`, `DISPLAY ready`, `RESTORE ok`, `READY`.
- Feed and Play exercised through USB; saves completed. Physical KEY selection
  and BOOT actions were captured in logs for Nap, Wake, and Play. The user confirmed
  the visible pet and controls.
- A 45-second runtime check received continuous USB replies, advancing active
  time, and stable free heap. Button actions continued working during the check.
- After the final flash/reboot, saved food/joy/energy restored as **97/100/59**.
  The menu intentionally returns to Feed on boot. Fractional time may roll back
  by up to the one-minute periodic save interval on abrupt reset.

## USB reset fix

An initial multi-process verify sequence used `--before no_reset` on its final
connection. On macOS, pyserial opens with DTR asserted; esptool 4.9's C6 hard reset
only toggles RTS. The resulting boot strap state can leave the chip in download
mode, retaining the previous AMOLED image and appearing frozen.

The final verifier now uses `--before usb_reset --after hard_reset`, explicitly
normalizing DTR/RTS before returning to the app. The full corrected flash workflow
passed all digests and automatically produced the successful boot/restore log.

Local evidence is under Git-ignored `backups/` and `artifacts/`: backup read/verify
logs, `flash-verified.log`, `runtime-actions.log`, `soak.log`, and `reboot.log`.
Battery runtime and a prolonged soak have not been measured. The touchscreen,
speaker, microphones, radio, RTC, and IMU were unused in v1.0.

## v1.1 — touch, shake, and faster naps

- New game rules: shake adds 8 food / 12 joy, wakes Moss, and costs no energy.
  Values clamp at 100. Nap restores one rest point every three seconds.
- Existing version-1 saves migrate to version 2 without losing needs, sleep
  state, or accumulated active time. Old sleep fractions scale from a 30-second
  tick to a 3-second tick. Migration boundary and CRC tests passed.
- Host tests passed with strict C++11 warnings, AddressSanitizer, and
  UndefinedBehaviorSanitizer: care and migration, shake/cooldown/settling,
  stationary/tilt/bump rejection, tap/hold/drag/release handling, touch bounds,
  all dance poses, percentage changes, and framebuffer bounds. Mocked I2C tests
  also exercise production sensor decoding, absent devices, short reads,
  touch release packets, and shared-bus timeout restoration.
- Final build: 344,715 bytes in the 3MB app partition; 15,888 bytes of globals.
  Flash readback verified bootloader, partition table, and application.
- Hardware startup found CST9220 at 0x5A and QMI8658 at 0x6B. Stationary
  acceleration was approximately (0.13, -0.15, -1.04)g. Sensor failures are bounded
  and leave physical-button controls available.
- The original sleeping save restored successfully; USB logs showed energy
  advancing 69 → 70 → 71 at three-second intervals, then continuing to recover.
- Real touchscreen presses/releases and a Nap/Wake action were recorded.
  Touch targets were padded after observing fingertips land just below the
  artwork. Final targets cover native (400,451), are disjoint, and have host
  regression coverage. Hardware axes matched the Nap/Wake button.
- Polling between completed DMA stripes increased sampling from ~20 to ~48 samples
  per second. Maximum gaps in the initial attached-monitor interval fell from
  108–112ms to 24ms; a later diagnostic reconnect interval included a 105ms gap.
  Verbose diagnostics are off in normal use. Free heap stabilized at 295,532
  bytes initially and 295,444 after subsequent persistence activity.
- A USB-triggered shake reward ran the complete four-second dance and saved
  successfully while sensor sampling and state replies continued. Physical shake
  sensitivity remains subject to the user's confirmation; the sensor readings
  and gesture detector's motion/cooldown tests are verified.
- The v1 pet NVS was backed up separately at `backups/moss-v1-pet-nvs.bin`; v1
  binaries are retained locally in `artifacts/releases/v1.0/`. Original factory
  backup remains intact. v1.1 state survives a subsequent firmware reinstall.

Evidence: `artifacts/boot-v1.1.log`, `inputs-v1.1.log`, `flash-v1.1-final.log`,
`final-v1.1-runtime.log`, `dance-v1.1.log`, and the corresponding build logs
(all Git-ignored).
The speaker, microphones, radios, RTC, and gyro remain unused.

## v1.2 — real-time care — 2026-10-02

- Food now decreases one point per 180 seconds; joy one per 300 seconds.
  Awake energy retains its 600-second interval; sleep restores one rest point
  per three seconds. Known time away applies to all three meters using the saved
  sleep state. Needs clamp at zero and never kill or reset Moss.
- Version-3 records atomically pair the pet snapshot with its accounted UTC
  timestamp. v1/v2 records migrate without changing meter values; old fractional
  food/joy progress is scaled to the new intervals. Legacy saves establish their
  first clock anchor on synchronization, without inventing prior offline time.
- Startup compares the saved timestamp with the valid PCF85063 clock and commits
  catch-up immediately. Runtime uses monotonic elapsed seconds, so RTC reads or
  clock synchronization do not independently apply the same elapsed time twice.
  Actions queue saves in the main loop; periodic checkpoints remain once a minute.
- Strict C++11, AddressSanitizer, and UndefinedBehaviorSanitizer tests passed:
  elapsed-time arithmetic and saturation, v1/v2/v3 persistence and CRC corruption,
  timestamp bounds, fractional accounting, millis rollover, hourly offline care,
  offline sleep, action/save/reboot sequences, invalid RTC, clock correction,
  and exactly-once catch-up. Existing sensor, gesture, renderer, and preview
  checks also passed.
- Calendar conversion covers every date in 2000–2099 against the host UTC
  calendar, leap years, 2038, 12-hour decoding, invalid BCD, STOP, and oscillator
  failure. Production RTC-driver mock tests inject failures at each I2C step,
  partially written dates, ignored STOP writes, bad readback, and uncleared OS;
  reads do not modify clock registers and bus timeouts are restored.
- Ten standard-library Python protocol tests verify host synchronization,
  USB re-enumeration retries, 9/10-digit timestamps, explicit acknowledgments,
  firmware/save errors, invalid readback, clock skew, and timeouts.
- Final firmware build: 348,465 bytes in the 3MB app partition; 15,912 bytes of
  globals. Flash readback digests matched bootloader, partition table, and app.
  The normal USB reset workflow restarted the app and the new host sync script
  verified the RTC at **2026-10-02 11:27:03 UTC**.
- Live elapsed-time test: a checkpoint held food/joy **100/100**, fractions
  **40/235 seconds**, and UTC **1790940506**. The application was stopped in the
  ROM loader for 141 seconds while the RTC ran. After reset, firmware restored
  the same checkpoint and accounted **145 seconds** including transition time:
  food/joy became **99/99**, fractions **5/80 seconds**, and lifetime time advanced
  **9541 → 9686**. All values matched the expected arithmetic exactly. This tests
  the actual RTC, saved timestamp, application downtime, boot catch-up, and save;
  it does not substitute a software reset for a physical PMIC-off test.
- Hardware boot reported battery detection enabled and a battery present,
  valid RTC, restored pet state, and both touch and acceleration sensors ready.
- A subsequent 12-second read-only check received four state/clock replies:
  accounted time advanced nine seconds between samples, sensor samples advanced
  1457 → 1889 (48/second), maximum sample gap was 26ms, and heap settled at
  295,284 bytes. RTC readings matched the current host clock within three seconds.
- Pre-upgrade v1.1 pet NVS is backed up at `backups/moss-v1.1-pet-nvs.bin`, with
  its SHA-256 file; v1.1 firmware is retained in `artifacts/releases/v1.1/`.
  The original factory image and its verified hash remain intact.

Evidence is retained locally under Git-ignored `artifacts/`: `build-v1.2.log`,
`tests-v1.2.log`, `flash-v1.2.log`, `baseline-v1.2.log`, and the live elapsed-time
verification in `offline-v1.2.log` (baseline JSON and verification script retained).
The post-catch-up runtime check is in `final-v1.2-runtime.log`.
PWR-button-off continuity and battery endurance have not been
physically measured; the RTC's separate power rail is documented in the verified
schematic and PMIC datasheet. Losing all USB and battery power can invalidate the
RTC, in which case firmware requests USB synchronization and skips unknown time.

## v1.3 — quiet naps, Eastern clock, and battery — 2026-10-02

- A sleeping shake returns an ignored response without changing needs, sleep
  state, or fractional clocks. The application also preserves dimming, feedback,
  animation, and the save queue. Awake rewards remain +8 food / +12 joy.
- The detector now requires three peaks above 1.2g within 1.2 seconds, at least
  100ms apart, versus two peaks above 0.85g previously. The 0.35g rearm threshold,
  four-second cooldown, and quiet settling interval remain. Host tests reject
  1.05g oscillations in five orientations, one/two bumps, rapid ringing, and
  widely separated peaks; accept deliberate 1.7g oscillations in five
  orientations; and cover cooldown, sensor gaps, and millis rollover.
- The upper-left clock uses 12-hour Eastern time with EST/EDT, while care and RTC
  persistence remain UTC. Tests cover exact 2026 DST transitions, pre-2007 rules,
  noon/midnight, invalid clock values, and six samples per day throughout
  2000–2099 against independent America/New_York host time-zone data.
- The upper-right icon and percentage use read-only AXP2101 telemetry, with a
  charging indicator and USB-only/unknown states. Driver tests cover every I2C
  failure, short reads, disabled detection/gauge, reserved values, charge
  direction, 0/100/out-of-range percentages, and restoration of bus timeouts.
  Tests explicitly assert that no PMIC register data is written.
- Renderer tests and visual inspection passed for charging, full, low, unknown,
  USB-only, and sleeping states. The longest time (`12:59 PM`) and `100%` charging
  fit simultaneously around MOSS. Header contents stay clear of all animation
  frames, meters, and unchanged touch targets. README preview updated.
- Full host suite passed with strict C++11 warnings, AddressSanitizer, and
  UndefinedBehaviorSanitizer; Python clock-protocol tests also passed.
  Firmware build uses **351,125 bytes** and **15,976 bytes** of globals.
- Flash readback matched bootloader, partition table, and application; the clock
  synchronized to **2026-10-02 11:39:51 UTC**. Existing v3 state was preserved.
  The pre-flash checkpoint was sleeping with food/joy/rest **91/98/100**; after
  normal elapsed-time decay and upgrade, the test saw **90/98/100**, still asleep.
- Three USB shake commands exercised the same action path and all reported
  `Shh... Moss is napping.` Sleep remained set with no food/joy reward. Time and
  sensor samples kept advancing; maximum sample gap was **29ms** and free heap
  **295,220 bytes**. The header reported **7:40 AM EDT**, matching the independent
  host conversion, and **82% battery, charging, USB present** from the hardware.
  These checks verify the action path; the feel of the new physical gesture has
  not been confirmed by the user yet.

Local evidence: `artifacts/build-v1.3.log`, `tests-v1.3.log`, `flash-v1.3.log`,
`pre-v1.3-state.log`, and `runtime-v1.3.log`, plus the live verification script.
Versioned binaries and SHA-256 checksums are in `artifacts/releases/v1.3/`.

## v1.4 — display sleep and button wake — 2026-10-02

- Added an idle policy with 60-second dim and 120-second screen-off deadlines.
  Tests cover boundaries, uint32 clock rollover, latched off state, hidden-input
  rejection, held buttons, and a wake-only press preserving the sleeping pet.
- Production board-code mock tests verify DISPOFF/SLPIN, 120ms sleep entry,
  600ms wake exit, board parameter/brightness restoration, a complete fresh frame
  before DISPON, and the 100ms display-on delay. They also reject power changes
  during frame callbacks, prevent DMA while asleep, and verify unchanged PMIC rails.
- Production PMIC tests verify PWR falling-edge enable/status handling, preserved
  unrelated IRQ bits and power-off settings, individual I2C failures, ambiguous
  write acknowledgments, and suppression of repeated uncleared button events.
- Hardware button events are batched. Wake consumes the press and holds GPIO
  actions until release. Hidden touch/motion input is disabled; a held touchscreen
  contact must release after wake. Runtime time is sampled again after blocking
  panel operations to prevent stale-millis underflow in care accounting.
- Full host suite passed with strict C++11 warnings, AddressSanitizer, and
  UndefinedBehaviorSanitizer, plus all Python clock-protocol checks. Build uses
  **354,621 bytes** of firmware and **16,008 bytes** of globals.
- Flash readback digests matched bootloader, partition table, and application.
  USB synchronization verified the RTC at **2026-10-02 11:56:10 UTC**. The existing
  v3 save format and pet progress are retained.
- The real idle countdown passed on the connected device: bright, then dim after
  the 60-second deadline, then panel sleep after 120 seconds. Read-only status
  queries did not prolong activity. During a four-second off interval, frame and
  accelerometer sample counters stayed fixed while care time advanced. A USB
  shake command was ignored while off.
- Three additional real panel sleep/wake cycles completed without a reset or
  care/menu action. Rendering and sensor polling resumed on each wake, with
  maximum observed active sample gap **38ms** and free heap **295,188 bytes**.
  Each cycle ended with the panel off and no more frames. PWR falling-edge
  reporting initialized successfully (`pwr_button=1`).
- The physical PWR-press/visible-panel check is awaiting user confirmation;
  automated cycles used the USB wake-only diagnostic. Earlier hardware GPIO
  input checks and the new wake-consumption policy tests cover KEY/BOOT handling.

Local evidence: `artifacts/build-v1.4.log`, `tests-v1.4.log`, `flash-v1.4.log`,
`pre-v1.4-state.log`, and `screen-v1.4.log`; the display verification script is
retained alongside those logs. Physical-button observation is recorded separately
in `physical-wake-v1.4.log`. The MCU remains awake; battery-current reduction
and runtime endurance have not been measured.

## v1.5 — customizable pocket pets — 2026-10-02

- Added Sloth/Cat/Frog and Jungle/Meadow/Night rendering, each animal's feeding,
  playing, sleeping, and dancing poses, bounded custom names, and an optional
  subtitle. Clock and battery now each occupy one line; the battery's internal
  lightning bolt indicates USB power, and the clock has no timezone label.
- Added a gear target, touch settings pages, a 12-character name keyboard with
  letters/numbers/punctuation, paged timezone choices, and complete KEY/BOOT
  navigation. Main Save/Cancel controls apply or discard a draft; storage errors
  leave it editable. The draft also survives display sleep and wake.
- Settings use an independent CRC-protected record. Tests cover all combinations,
  corrupted bits and valid-CRC invalid fields, bounds, and atomic fallback.
  The formatter covers 14 zones, every supported date from 2000–2099, and 1,600
  exact daylight-saving transitions against independent host IANA data. Care
  state and UTC accounting are unchanged when preferences change.
- Full host suite passed with strict C++11 warnings, AddressSanitizer, and
  UndefinedBehaviorSanitizer, plus all Python clock-protocol tests. UI tests cover
  hardware-only navigation, keyboard validation, cancellation, pagination,
  transactional save handoff, touch geometry, and framebuffer bounds. Renderer
  tests cover animal × scene × dance poses, other animations, long names, hidden
  subtitles, compact HUD separation, and disjoint settings/care hitboxes.
- Visual inspection passed for all nine settings preview modes and Sloth/Jungle,
  Cat/Meadow, Frog/Night, feeding, dance, and sleep examples. The build completed
  without warnings: **368,833 bytes** firmware and **16,136 bytes** globals.
- Flash readback matched all three binaries. RTC sync verified
  **2026-10-02 12:13:37 UTC**. The pre-flash pet was awake at **87/94/97**
  food/joy/rest; progress was preserved and normal elapsed-time decay continued.
- Live USB navigation exercised the same menu controller as KEY/BOOT: entered
  PIP on the name keyboard, accepted it into the draft, then canceled globally
  and confirmed Moss was unchanged. Saved Cat/Night/UTC/hidden subtitle; menu
  sleep/wake preserved the draft, page, and focus. The initial automation checked
  the queued off request too early; checking the completed panel transition
  confirmed the off state and unchanged draft. Free heap was **295,056 bytes**.
- A hardware reset retained Cat/Night/UTC/hidden subtitle and displayed the UTC
  clock correctly. The same menu then restored Sloth/Moss/Jungle/Eastern with
  the subtitle visible. Final telemetry showed **8:15 AM EDT**, **97% battery**,
  USB power, and an awake pet at **86/93/96** after normal elapsed-time decay.
  These automated checks use USB equivalents of menu button actions; physical
  touchscreen observation of the new settings screens remains a user check.

Local evidence: `artifacts/build-v1.5.log`, `flash-v1.5.log`,
`pre-v1.5-state.log`, `settings-v1.5.log`, and `restart-v1.5.log`, plus
`verify_settings_v15.py` and `restore_settings_v15.py`. Versioned binaries and
SHA-256 checksums are in `artifacts/releases/v1.5/`. The complete host test command
was `./scripts/test.sh`; its successful output is retained in the task transcript.

## v1.6 — more scenes and a gear-only control — 2026-10-02

- Feed, Play, Nap/Wake, and the gear now occupy one control row. The gear has no
  label or second footer row; all four padded touch targets remain disjoint.
- Added NYC (skyline/taxi), Space (planets/rocket), Island (palms/ocean), and Under
  the Sea (fish/coral). Seven total scenes are available through a two-page
  picker with bounded Prev/Next, Back, and saved-selection focus. Existing scene
  IDs, preferences, care records, and UTC accounting remain compatible.
- Settings serialization/validation and settings UI tests passed under strict
  C++11, AddressSanitizer, and UndefinedBehaviorSanitizer. New paging tests cover
  both pages, disabled arrows, hardware navigation, Back, and persistence of the
  last scene. Renderer tests cover all seven scenes × three animals × twelve
  dance poses, feed/play/sleep, distinct artwork, compact control hitboxes,
  selection highlighting, and absence of the old footer label.
- Visually inspected all four new scene previews and both picker pages, including
  the long Under the Sea label on the main settings screen. Updated the README
  previews. Firmware builds without warnings: **373,519 bytes** program and
  **16,136 bytes** globals.
- Flash readback matched bootloader, partitions, and application. Clock sync
  verified **2026-10-02 12:27:52 UTC**. The saved pet and Sloth/Moss/Jungle/Eastern
  preferences were preserved during upgrade.
- Live USB navigation used the same controller as KEY/BOOT to select and save
  each of the four new scenes. Under the Sea persisted across a hardware reset;
  the original Jungle scene was then restored. Name, animal, timezone, subtitle,
  and nap state were unchanged, with normal time-based care decay continuing.
  Final telemetry: **82/91/95** food/joy/rest, awake, **100% battery**, USB present,
  **295,060 bytes** free heap. Physical touch was covered by host geometry tests,
  rather than an observed finger tap during this automated check.

Local evidence: `artifacts/tests-v1.6.log`, `build-v1.6.log`, `flash-v1.6.log`,
`pre-v1.6-state.log`, `scenes-v1.6.log`, and `restart-v1.6.log`, plus the scene
verification scripts. Binaries and checksums are in `artifacts/releases/v1.6/`.

## v1.7 — shake tuning and PWR screen toggle — 2026-10-02

- Diagnosis: the connected IMU produced live readings near (−0.05,−0.01,−1.08)g
  at rest. The pet was napping, which intentionally rejects shake rewards. The
  old fixed-threshold detector could also miss a valid oscillation if sampling
  never observed its brief below-rearm crossing.
- Added five sensitivity levels, a separate gravity-magnitude calibration for
  sensor bias, opposite-direction peak recognition, and read-only telemetry.
  Regression tests reproduce missed crossings, compare sensitivity thresholds,
  reject single bumps/subthreshold oscillations, reject rotated 1.08g gravity,
  and retain timing/cooldown/settling/rollover behavior. Test and gameplay use
  independent detector instances; test triggers cannot call the pet-care path.
- Added a live Shake test settings page: raw axes, total/motion/peak g, threshold,
  measured polling rate, sample and trigger counts, peak progress, bubble level,
  and detection feedback. The draft sensitivity is separate from saved settings.
  Stale, missing, and nonfinite data are hidden or marked. Six main settings rows
  retain readable values and bounded touch targets. Preview states and all
  hardware-only navigation/Save/Cancel paths passed visual and sanitizer checks.
- PETC v2 stores sensitivity in formerly reserved byte 10. Independent CRC
  fixtures and tests cover v1 migration preserving customization, all sensitivity
  values, malformed IDs/versions/reserved fields, and atomic corrupt fallback.
- PWR short taps use only PMIC short-press flag 0x08, ignoring edge/long events.
  Driver tests verify preservation of unrelated IRQs and long-hold settings,
  stale/stuck flags, and I2C failures. Screen toggle tests cover Bright/Dim/Off,
  rollover, hidden inputs, and unchanged sleeping care state. Review caught and
  fixed a late-debounced KEY/BOOT edge that could otherwise undo PWR-off: the
  existing wake guard now rejects it before changing the display policy.
- Full host suite passed with strict C++11 warnings, ASan/UBSan, Python protocol
  checks, driver mocks, renderer tests, and all generated preview modes. Firmware
  built without warnings: **381,451 bytes** program, **16,352 bytes** globals.
- Flash readback matched all binaries; RTC sync verified
  **2026-10-02 13:04:10 UTC**. Migration preserved the user's SLOTH name, Sloth
  animal, NYC scene, Eastern timezone, hidden subtitle, and nap. The pre-flash
  checkpoint was **96/98/100** food/joy/rest, asleep.
- Live device checks used the settings controller to change Normal→High as a
  draft, verified that live preferences remained unchanged until Save, then
  tested the shared short-tap toggle path off/on. The sleeping pet and open
  Motion draft were preserved, and valid sampling resumed on wake. High survived
  a hardware reset; Normal was restored, a later draft was canceled successfully,
  and the Shake test was left open for physical evaluation.
- Final sensor readout was approximately **47.6 Hz**, total **1.077g**, filtered
  motion **0.002g**, threshold **0.85g**, and tilt **−2.9°/−0.6°**, with fresh sample
  counts increasing. No care rewards were awarded by these menu tests. Pet state
  remained asleep at **96/97/100** after normal elapsed-time decay; free heap was
  **294,600 bytes**. Original customization remained intact.
- Automated screen toggles used USB `b`, the same application handler as the
  PMIC short-tap event. Physical gesture feel and actual PWR taps are a separate
  user check; shutdown registers are verified unchanged by driver tests.

Local evidence: `artifacts/tests-v1.7.log`, `build-v1.7.log`, `flash-v1.7.log`,
`pre-v1.7-state.log`, `pre-v1.7-checkpoint.log`, `motion-v1.7.log`, and
`restart-v1.7.log`, plus the motion verification scripts and `physical-v1.7.log`.
Binaries and checksums are retained under `artifacts/releases/v1.7/`.

## v1.8 — USB extended display — 2026-10-02

- Added a seventh Settings row, USB Display, with waiting/permission/error
  screens and a handoff event that preserves the menu draft. KEY or BOOT exits
  directly to that menu; short PWR exits and switches the panel off. Pet care,
  RTC accounting, and saves continue while the host uses the screen.
- Native 480×480 RGB565 transfer uses even, full-width stripes up to 16 rows.
  No second full framebuffer is allocated on the device. Each region is copied
  into the existing DMA buffer and completes before ACK or buffer reuse.
- A bounded 15,528-byte portable receiver validates COBS framing, CRC32, session
  nonces, sequence numbers, geometry, and payload lengths. Tests cover malformed
  packets, corruption, retransmission, noise, expiry, timer rollover, cancellation
  mid-packet, and stale sessions. Binary data stays quarantined after exit until
  a matching RELEASE; pixel bytes cannot become pet commands. Periodic IDLE
  beacons permit a relaunched companion to recover a stale lock safely.
- USB RX capacity is explicitly 32 KiB. Reading 256-byte chunks with one clock
  sample per binary chunk reduced an observed full frame from **4.354 seconds**
  to **2.089 seconds**. Legacy commands retain fresh timestamps. Input service
  remains bounded by a 12 ms / 8192-byte budget between loop iterations.
- Full host suite passed with strict warnings, ASan/UBSan, Python checks, panel
  mocks, all settings navigation paths, and renderer previews. Native panel tests
  cover geometry, byte order, sleeping-panel rejection, and unchanged power rails.
  Independent review checked buffer lifetimes, button guards, and elapsed care.
- Final firmware build: **387,063 bytes** program, **31,880 bytes** globals.
  Flash readback matched bootloader, partitions, and application. Final RTC sync
  verified **2026-10-02 13:43:39 UTC**. User customization remained Sloth,
  SLOTH, NYC, Eastern, hidden subtitle, Normal shake sensitivity, and napping.
- On-device synthetic pattern test acknowledged all 30 native stripes; replaying
  the last stripe did not redraw it. A damaged checksum was rejected, a valid
  next PING succeeded, and three seconds without traffic returned to Settings.
  Care-like bytes sent while quarantined caused no actions. RELEASE restored
  diagnostics; the original menu focus and settings persisted. A second fresh
  session passed explicit STOP/RELEASE cleanup. Physical button presses are
  distinct from this automated protocol check.

Local firmware evidence: `artifacts/tests-v1.8.log`, `build-v1.8.log`,
`flash-v1.8.log`, `pre-v1.8-device.log`, and `display-device-v1.8.log`.
Versioned binaries and SHA-256 sums are under `artifacts/releases/v1.8/`.

- The macOS companion builds with strict Clang warnings, has an ad-hoc verified
  signature, and passes host encoder/firmware interoperability plus real transport
  pseudo-terminal tests under ASan/UBSan. Cases include lost ACK, duplicate
  REQUEST, device exit during transfer, host disconnect, permission refusal,
  stale binary-lock cleanup, and initial nonce-zero cleanup. It captures only
  the newly created display ID, with audio and microphone disabled.
- Initial live testing on macOS **26.5.1** exposed an unusable automatically
  selected 240-point mode. The companion now waits asynchronously for attachment,
  selects an explicit usable mode, and provides a 960-square fallback. A separate
  no-capture AppKit probe confirmed the final code creates a **960×960**, 1×,
  active, online, unmirrored desktop at **(1800,0)**. This Mac did not expose the
  requested 480×480 desktop mode. Removing the owner invalidated the display ID.
  USB output remains native 480×480.
- Screen Recording was manually enabled for the first build. Rebuilding changed
  the ad-hoc signature and left a stale enabled permission entry. The user removed
  that entry, approved the final sloth-icon build, and the binary was then kept
  unchanged. At **13:59:11 UTC**, macOS created display **11**, active and
  unmirrored at **960×960**, and ScreenCaptureKit enumerated that exact display.
  Capture started successfully. At **13:59:13 UTC**, the device acknowledged
  the first complete **480×480 RGB565** desktop frame. This verifies the full
  Mac desktop → scoped capture → USB → panel-transfer path. No physical-display
  or audio capture fallback is implemented.
- The app includes a deterministic vector sloth icon with all standard 16–1024
  pixel and Retina representations. AppKit and `sips` validate the ICNS; full,
  128-pixel, and 32-pixel previews were inspected. The icon source and asset are
  committed, and the app bundle signature verifies after integration.

Mac evidence: `artifacts/display-host-wire-v1.8.log`,
`display-host-transport-v1.8.log`, `display-creation-probe-v1.8.log`, and the
`display-approved-v1.8.stderr.log` end-to-end log. Build output is
`build/macos/Moss Display.app`; rebuilding may require native permission again.

## v1.9 display throughput / companion v1.1 — 2026-10-02

Firmware build and readback verification passed (387,641 program bytes, 31,880
global bytes). Strict host builds, firmware/host protocol interoperability,
and ASan/UBSan tests passed. Actual USB synthetic measurements: flat native
480 raw 2.0604 s versus RLE 0.1226 s; seeded noise native 2.0104 s versus
240 enlarged 0.5856 s. These measure transfer workloads, not general desktop FPS.
Two changed native rows took 0.0089 s and one changed fast row 0.0032 s.

The unchanged final companion successfully captured its own 800-square virtual
display at 10:31 EDT. The device acknowledged the first Sharp 480 frame; a live
menu switch to Fast 240 at 10:32 EDT also produced acknowledged frames. Evidence:
`/tmp/moss-display-relaunch.stderr.log`, `artifacts/display-benchmark-v1.9.csv`,
`artifacts/display-host-build-v1.9.log`, and `artifacts/flash-v1.9.log`.
Wi-Fi and Bluetooth were researched, not implemented or benchmarked.

## v1.10 USB rotation / companion v1.2 — 2026-10-02

- +/KEY (front-right) rotates USB Display clockwise; BOOT/− exits. Pet and
  settings behavior remains unchanged. Default USB angle is 90° clockwise,
  placing the top toward the speaker. Mode reentry retains the runtime angle;
  device reboot restores the default.
- A shared pure coordinate transform renders rotated device status screens
  into the existing DMA stripe and rotates Mac captures before delta encoding.
  No extra device framebuffer is allocated.
- Board tests independently forward-map every pixel at all four angles and
  validate exact scaled DMA output under ASan/UBSan. Settings UI tests pass.
- Real host transport/firmware parser tests over pseudo-terminals verify all
  four rotations in 480 and 240 modes, redraw without a new capture callback,
  repeated/invalid/wrong-session announcements, and three rapid rotations while
  an old strip awaits a deliberately dropped ACK. Exact final panel pixels are
  compared with an independent reference; the session stays connected.
- Firmware compiles to 387,869 program bytes and 31,888 global bytes.

Evidence: `artifacts/board-tests-v1.10.log`, `settings-tests-v1.10.log`,
`display-host-build-v1.10.log`, and `build-v1.10.log`.

The device was flashed and readback-verified at 20:48 EDT on October 2; RTC
synchronization passed and the saved SLOTH/NYC/Eastern settings were unchanged.
A real device mode entry advertised `ROTATION ... 1`, the expected default.
The rebuilt companion launched at 20:49 EDT, but macOS denied Screen Recording
and it cleanly released the session. Live desktop rotation and physical-button
visual confirmation remain pending renewed user permission. The final binary
is kept unchanged for approval. `artifacts/flash-v1.10.log`,
`device-v1.10.log`, and `display-entry-v1.10.log` record these checks.
The release bundle and firmware hashes are in `artifacts/releases/v1.10/`.

## v1.11 Wi-Fi, scrolling settings, stable signing — October 2, 2026

- Firmware compiled (1,153,261 program bytes, 69,492 globals), flashed, and
  readback-verified at 22:33 EDT. RTC sync passed. Device boot reported 178,492
  bytes free heap before activating Wi-Fi. Saved SLOTH/NYC/Eastern/hidden-subtitle
  settings and current pet state survived.
- The complete portable firmware suite passed with strict compiler warnings and
  ASan/UBSan, including care/RTC/time zones, UI transactions, scrolling/swipe tap
  suppression, larger keyboard navigation, sensors/power, rotated DMA, bounded
  display parsing and length/nonce/state validation of Wi-Fi provisioning.
- Visual previews of main/top/bottom, keyboard/top/bottom/numeric, long zone/scene
  lists, and Wi-Fi waiting screen were inspected. Device touch is integrated
  with last-contact release coordinates and page-transition diagnostic reset.
- Actual Network.framework-to-Python TLS loopback tests passed: exact USB-provided
  certificate pin, wrong pin rejected, token authentication, fragmented replies,
  malformed/oversized/overflow replies, mutable input snapshots, transmitted
  packet contents, and remote disconnect. These tests use disposable test keys.
- Host pseudo-terminal tests use the real firmware parser and an injected Wi-Fi
  channel to prove rectangle-only network routing, USB control/ACK handling,
  provisioning byte layout/wipe, stale sessions, network/setup failure cleanup,
  both quality modes, rotations, retry, and an ACK that precedes asynchronous
  send completion. All prior USB tests and byte benchmarks still pass.
- The final companion v1.3 has a verified Developer ID Application signature,
  Apple certificate chain, and Apple timestamp at 22:32 EDT. Its designated
  requirement binds the existing bundle identifier, Apple Developer ID certificate
  class and the configured signing team, rather than a build-specific ad-hoc hash. The signing
  identity is locally pinned; builds cannot silently fall back to ad-hoc signing.
- At 22:34 EDT the signed app received the device's Wi-Fi request. macOS denied
  Screen Recording until the user migrates permission to this signing identity;
  the app cleanly released the session. Actual network association, on-device TLS
  heap/throughput, desktop streaming and tactile scrolling remain pending this
  approval and user-entered network details. No live Wi-Fi FPS is claimed.

Evidence: `artifacts/build-v1.11.log`, `flash-v1.11.log`, `device-v1.11.log`,
`tests-v1.11.log`, `display-host-build-v1.11.log`, `wifi-transport-tests-v1.11.log`,
and the release binaries/bundle/hash manifest in `artifacts/releases/v1.11/`.
Run `scripts/test-wifi-channel.sh` to repeat the real TLS loopback checks.

At 22:36 EDT the user completed the signing-identity permission migration and
entered network details in the Mac dialog. Actual device Wi-Fi association,
pinned TLS pairing, token authentication, USB HELLO, and desktop streaming
succeeded. The first Fast 240 frame was acknowledged at 22:36:28 EDT (116,767
wire bytes, 760 ms). A temporary 15-second synthetic scrolling window on only
the Moss virtual display produced a later 28-strip/83,944-byte update in
664.1 ms. This confirms connectivity but does **not** yet demonstrate a speed
advantage over USB. The user also reported slow refresh; bounded Wi-Fi strip
batching is being evaluated to reduce acknowledgment round trips.

### Companion v1.3.1 Wi-Fi batching follow-up

The Mac now sends up to four sequential strips per bounded TLS write, reducing
a 30-strip frame to eight send/wait cycles. Exact-pixel tests cover final-ACK-only
baseline commits, partial-batch delivery, retransmission after a lost final ACK,
disjoint changed strips, rotation, resolution changes, delayed send completion,
and cancellation with a pending batch. Cancellation discards pending pixels and
sends USB RELEASE directly; it cannot retry network pixels over USB. The actual
TLS loopback suite also transfers exactly 65,536 bytes and rejects 65,537.

The signed app was rebuilt, with an identical designated requirement, and
launched at 22:45 EDT. It passed the native Screen Recording preflight without
another approval and proceeded directly to the network-details dialog. This
verifies permission retention across this Developer ID-signed update.

Evidence: `artifacts/display-host-build-v1.11.1.log`,
`wifi-transport-batching-tests.log`, `wifi-motion-before-batching.log`.
The initial signed companion archive is under `artifacts/releases/v1.11.1/`;
that first host-only test used firmware v1.11. The reproducible live workload is
`scripts/benchmark-display-host.sh` (15 seconds, only the Moss virtual display).

At 22:46 EDT the batched companion reconnected successfully. Its first full
240-pixel frame took 745.1 ms; the same synthetic scrolling workload produced
28-strip updates of 82–84 kB in 470.2, 520.3 and 550.5 ms. This is a modest
improvement over 664.1 ms before batching, still roughly two updates per second.
These are frame transfer/acknowledgment durations, not a capture frame-rate claim.

A diagnostic firmware v1.11.1 was then built and readback-verified at 22:52 EDT,
with saved pet settings preserved. It adds bounded numeric timing counters for
TLS reads, socket delivery, receive-queue pressure, parsing, panel submission and
ACK writes. The companion validates their shape and session before logging.
PTY tests cover malformed values, stale sessions, unsigned overflow/wraparound,
and non-Wi-Fi session rejection, with ASan/UBSan enabled.

The live diagnostic session at 22:53 EDT associated at RSSI -44 dBm and retained
roughly 74 kB free heap while streaming. During the synthetic scrolling workload,
one-second samples carried 117–171 kB. Feed time minus panel/ACK time was
192–277 ms per sample, panel time 86–125 ms, ACK writes 2.5–3.5 ms, and TLS read
time 134–179 ms. Timers include scheduling/preemption and may overlap; they do
not sum to CPU utilization. These readings support optimizing the packet parser
before treating Wi-Fi association or panel bandwidth as the sole bottleneck.

The decoder now uses a 1 KiB read-only IEEE CRC-32 lookup table and avoids
rewriting already-empty rectangle metadata on each payload byte. Independent
bitwise-reference packets, 96 deterministic randomized native/scaled payloads,
corruption rejection, duplicate retries and rectangle invalidation tests pass
under ASan/UBSan. The receiver allocation remains 15,528 bytes. Final firmware
build uses 1,155,111 bytes of program storage and 69,532 bytes of static RAM.

The optimized firmware was flashed with readback verification and RTC sync at
22:57 EDT. The complete portable suite passed, including the new CRC fixtures,
time zones through 2099, scrolling/name keyboard, care persistence, power/button
handling, renderer bounds and display DMA guards. Evidence:
`artifacts/build-wifi-optimized.log`, `flash-wifi-optimized.log`,
`tests-wifi-optimized.log`, and `wifi-performance-before-crc.log`.

### Companion v1.3.2 saved networks and final live checks

- Credentials saved through the visible Remember option use exact app-specific
  login-Keychain service/accounts. Defaults, logs, the repo and device NVS contain
  no Wi-Fi passwords. Listing names reads Keychain attributes only; automatic
  connection reads only the preferred network's credential. The app does not
  import system Wi-Fi passwords.
- Manage Networks provides Add, Update Password, Forget and a preferred-network
  automatic-connection checkbox. Failed automatic joins offer manual setup on
  the next device request. An unrelated network editor defers pending Wi-Fi setup
  until it closes, then verifies that the requesting session still exists.
- Fresh UUID-scoped Keychain tests passed CRUD, Unicode/open networks, preferred
  persistence/reopening, forgetting and namespace isolation. Disposable entries
  were removed. Offscreen AppKit tests passed secure empty password fields,
  Remember defaults, picker/editor modes, tab order, layout bounds and nonoverlap.
- The signed final build and full PTY suite passed. The app connected at 23:00:56
  EDT after the user entered/saved the network. After a clean quit and a fresh
  device request, app relaunch at 23:02:15 immediately began automatic Wi-Fi setup
  (28 ms after the request), reached TLS/USB READY, and streamed by 23:02:18.
  No password or Screen Recording prompt was needed. The app remains connected.
- Before/after diagnostic samples with more than 100 kB arriving in roughly one second
  show estimated parser wall time per byte falling from 1.646 to 1.177 microseconds
  (about 29%). These include scheduling effects and are not isolated CPU cycles.
  Motion-frame timings remained variable: the final workload logged 595.2,
  695.3 and 495.0 ms for 28-strip updates. The first frame after automatic
  reconnection took 629.8 ms. Wi-Fi connectivity and reduced parser work are
  verified; a consistently high desktop refresh rate is **not** achieved.

Evidence: `artifacts/network-store-test-v1.3.2.log`,
`network-ui-test-v1.3.2.log`, `display-host-build-saved-networks.log`,
`wifi-performance-after-crc.log`, `wifi-autoreconnect-v1.3.2.log` and
`wifi-optimized-workload.log`. Repeat isolated tests with
`scripts/test-network-store.sh` and `scripts/test-network-ui.sh`.
Final firmware v1.11.1 and signed companion v1.3.2 are packaged with hashes under
`artifacts/releases/v1.11.1/`.

## v1.12 compressed Wi-Fi display / companion v1.4 — October 2, 2026

- Added capability-negotiated cropped rectangles and standard raw-block LZ4.
  The receiver is 30,888 bytes, including its separate fixed 15,360-byte decode
  buffer. Malformed blocks, output-size mismatches, crop bounds, alignment,
  CRCs, retries and metadata invalidation pass ASan/UBSan tests.
- Added one-packet 240×240 baseline JPEG for detailed moving images. JPEGDEC
  is pinned, with documented alignment and negative-shift fixes. The wrapper
  bounds every header, accepts only the companion's baseline 4:2:0 subset,
  validates output blocks, and uses the existing pet framebuffer. Failed
  decoding exits without presenting or acknowledging a frame. Tests cover
  golden pixels, every fixture truncation, 1,500 bit mutations, canaries and
  recovery. The fixture is procedural artwork, not captured screen content.
- The host selects raw/RLE/LZ4 per cropped band. Adaptive JPEG is Wi-Fi Fast240
  only, above 20 KB estimated lossless traffic and at least 40% savings. After
  250 ms without content changes, it queues a complete lossless refresh. The
  real host-to-firmware PTY suite verifies bit-exact refinement after actual
  JPEG decoding, disabling Adaptive during a pending ACK, rotation, resolution
  changes, retries, partial batches, cancellation and legacy/USB fallback.
- Wi-Fi batches up to 30 packets within 65,536 bytes; USB remains single-packet.
  Capture is capped at 30 fps with a bounded latest-frame queue. New
  `changed_frames` diagnostics count only completed updates with actual regions;
  unchanged capture callbacks do not inflate the measured update cadence.
- The complete portable firmware suite, signed Mac build, image codec tests,
  real-parser transport tests and offscreen network UI tests passed. The signing
  requirement remains identical to v1.3.2; relaunch automatically reused the
  preferred Keychain network and Screen Recording permission.
- Pixel conversion uses contiguous rows and row duplication. Scoped GCC O3
  applies only to the JPEG implementation and board rendering, retaining normal
  compiler semantics and the vendor 40 MHz panel clock. C6 stack reports show
  a maximum JPEG function frame of 368 bytes and an estimated deepest baseline
  codec path of 544 bytes, excluding wrapper/runtime frames, within the 8 KiB
  loop stack. This is static analysis, not a whole-task high-water measurement.
- The TLS worker now waits for socket readiness for at most 10 ms after
  `WANT_READ`, reducing idle polling while retaining immediate buffered reads,
  queue backpressure, cancellation and authenticated transport.

Initial v1.12 live tests reduced scrolling updates from roughly 83 KB / 0.5–0.7 s
to roughly 8 KB / 0.10 s. Textured motion used roughly 6 KB JPEGs instead of
108 KB lossless updates; the earlier v1.3.2 photo workload logged 559–606 ms
per update. These are synthetic, repeatable moving windows on only the Moss
virtual display, not general application or video playback guarantees.

Evidence: `artifacts/tests-v1.12.log`, `display-host-build-v1.4-final.log`,
`network-ui-v1.4.log`, `image-codec-benchmark.csv`,
`jpegdec-c6-optimized-stack.txt`, `wifi-v1.3.2-baseline.log`, and
`wifi-v1.4-before-tuning.log`. `scripts/benchmark-display-host.sh` provides
scroll, cursor and photo workloads; the analyzer excludes two seconds of startup.

### Final installed release

The final firmware was flashed and readback-verified at 23:33 EDT; RTC sync passed.
It uses 1,209,527 program bytes and 102,788 static bytes. The last drawing change
removes per-pixel division from scaled crops and duplicates completed rows.
Optimized sanitizer checks cover a bottom-right crop, runs crossing multiple rows,
raw/RLE equivalence, physical coordinates, exact output bytes and unused DMA
buffer canaries. Existing full-frame, rotation, power and DMA checks also pass.

The signed companion relaunched at 23:33 EDT, reused the preferred saved network
and existing recording permission, and reached authenticated Wi-Fi streaming.
Final measurements count completed **changed** frames, not unchanged callbacks:

| Workload | Test / warmup | Changed updates/s | Median transfer ms | Median wire bytes/update |
| --- | --- | ---: | ---: | ---: |
| Scroll | 25 s / 2 s | 8.89 | 95.3 | 7,746 |
| Small moving marker | 20 s / 2 s | 25.68 | 10.4 | 250 |
| Textured image motion | 20 s / 2 s | 7.45 | 130.1 | 6,015 |

Rates use differences of cumulative acknowledged changed-frame counters between
periodic log samples inside each workload window. Transfer medians describe only
the logged nonempty updates; occasional network/capture stalls remain. These are
separate synthetic workload results, not panel scan-rate or video guarantees.
The first JPEG was followed by an acknowledged 30-band lossless refresh. Tests
independently verify that the refined 240×240 pixels are exact; live ACKs do not
read back panel pixels. The physical panel continues to display 2×2 enlarged pixels.

Observed free heap during this final session ranged from 33,592 to 40,860 bytes,
usually about 40,644. No session failure or device STOP was logged during the
workloads. JPEG intervals spend roughly 55 ms per frame drawing and 39 ms in
decode/parser work; these inclusive wall-clock timers are not isolated CPU cycles.
This makes device processing a substantial remaining limit. The app remains
connected after the benchmark windows close.

Final evidence: `artifacts/build-v1.12-release.log`, `flash-v1.12-release.log`,
`board-display-v1.12-release.log`, `wifi-v1.4-release.log`,
`wifi-v1.4-release-{scroll,cursor,photo}-workload.log`, and
`wifi-v1.4-release-summary.txt`. The signed companion v1.4/build 7, firmware
binaries, and verified SHA-256 manifest are in `artifacts/releases/v1.12/`.

## v1.13 wireless operation and optional audio / companion v1.5 — October 3, 2026

- The device saves its network and stable TLS identity in one bounded,
  checksummed NVS record, separate from pet saves. The Mac keeps certificate pin,
  token, and device ID in its own Keychain service. Bonjour routing hints cannot
  create a display until pinned TLS, token authentication, and a matching device
  request succeed. USB alone can provision or update credentials. A new
  **Wi-Fi setup / USB** settings row changes the device network while preserving
  its valid identity. Records are not encrypted at rest on the device.
- Controls, image ACKs, rotation, heartbeats, and optional sound now share the
  authenticated Wi-Fi connection. After TLS takes ownership, physical USB bytes
  cannot interleave with the parser. Audio has independent sequence-zero framing,
  does not replace an image's retry baseline, and cannot extend the video lease.
  Host and device teardown handle EOF as well as best-effort STOP/RELEASE.
- The user physically unplugged the running device and confirmed the desktop
  continued updating on battery. After final installation, an additional fresh
  startup at 00:13:46 EDT held `/dev/cu.usbmodem101` exclusively in a test process,
  preventing the real signed app from opening USB. Its log reported USB busy,
  discovered the paired device, authenticated, and received the wireless request.
  The first desktop frame was acknowledged at 00:13:47.603. Subsequent moving
  marker updates succeeded. This verifies startup independent of the serial
  connection; power was supplied by the attached cable during this second test.
- Audio is optional and off by default. ScreenCaptureKit captures Mac/system
  audio (not microphones), converts 48 kHz mono float to 16 kHz PCM16LE, and
  sends it to the ES8311 DAC/NS4150B speaker through an output-only I2S channel.
  Source audio is not restricted to windows physically on the virtual display.
  Driver pin, power-rail, clock, mute, and volume configuration was checked
  against the vendor schematic and driver. No I2S RX or microphone ADC capture
  path is configured. Speaker resources are allocated only while audio is on.
- The first tone helper also played on the Mac, so hearing that tone was not
  accepted as device-speaker evidence. `tools/wifi_audio_probe.py` then sent
  synthetic PCM directly over pinned Wi-Fi without opening any Mac audio output.
  The user confirmed beeps came from Moss. The probe acknowledged 320,000 source
  samples with zero driver write errors. Its initial 40 ms packets were slower
  than real time due to acknowledgment pacing; the reusable helper now uses
  100 ms packets and a wall-time limit. This was a speaker/path test, not a
  latency or uninterrupted-playback guarantee.
- Initial simultaneous audio/image stress runs exhausted free memory and lost
  their sessions. Fixes retain a bounded 200 ms of recent audio, continue
  resampling while delivery is pending, allow only one PCM packet awaiting a
  device acknowledgment, reduce image batch size while audio is active, and
  free Bonjour's task/buffers after authentication. Discovery returns on the
  next explicit mode entry. Subsequent audio-on scrolling/photo workloads and
  several minutes of streaming completed without a disconnect. Audio gaps and
  drops still occurred under heavy motion, so this is best-effort sound rather
  than a synchronized or high-fidelity movie system.
- Active audio free heap was usually about 21–23 KB after the final fix;
  the boot-wide low-water mark reached 1,072 bytes during stress. No driver
  write errors or disconnects occurred in the final audio workload session.
  The memory margin under bursts is small; these bounded tests do not establish
  hours-long stability. Audio off recovered roughly 12 KB of memory. The final
  running companion is left with audio disabled for maximum display performance.

Completed changed-update measurements, Fast 240 + Adaptive, with two seconds
of warmup excluded:

| Workload | Audio | Test duration | Updates/s | Median transfer ms | Median wire bytes/update |
| --- | --- | ---: | ---: | ---: | ---: |
| Scroll | Off | 25 s | 7.41 | 110.3 | 7,870 |
| Scroll | On | 25 s | 5.53 | 144.9 | 7,981 |
| Textured motion | Off | 25 s | 5.83 | 140.3 | 6,016 |
| Textured motion | On | 30 s | 5.72 | 154.0 | 6,010 |

The counter method is unchanged from v1.12. These compare the same synthetic
workloads, but association RSSI varied (audio-on -55 dBm, off -44 dBm), so the
differences are observations rather than a universal isolated audio cost.
Audio traffic continues during these tests even when the source is silent.

The full portable firmware suite and signed companion build passed, including
real-parser PTY regression tests for USB, legacy Wi-Fi, cable removal, fresh
wireless startup, authentication failure, credit-bound audio, independent
image/audio ACKs, and network completion races. Additional sanitizer suites
cover saved-pairing corruption/failures and Keychain isolation, actual loopback
TLS certificate/token validation, resampling, bounded capture lifecycle,
video-only fallback after audio startup failure, audio ring conservation,
output-driver resource cleanup, and settings scrolling. The network editor's
offscreen tests passed. USB monitor tests verify pairing-secret redaction even
across split, oversized, and incomplete serial lines.

The final firmware uses 1,275,671 program bytes and 106,348 static bytes. All
flashed regions passed digest verification. The automatic post-flash clock
step timed out; after cleanly releasing the display binary lock, standalone
RTC synchronization/readback passed at 2026-10-03 04:10:59 UTC. Pet state and
saved pairing survived flashing. The existing Developer ID signing requirement
remained unchanged, and the app reused its recording permission without a new
approval in these launches.

Evidence: `artifacts/tests-v1.13-release.log`, `build-v1.13-release.log`,
`flash-v1.13-release.log`, `rtc-v1.13-release.log`,
`display-host-build-v1.5-release.log`, `network-ui-v1.5.log`,
`capture-lifecycle-test.log`, `device-speaker-probe-v1.13.log`,
`wifi-v1.5-{audio-on,audio-off,wireless-start}.log`,
`wifi-v1.13-release-{audio,off}-{scroll,photo}.log`, and
`wifi-v1.13-summary.txt`. The signed companion v1.5/build 8 and firmware binaries
are packaged with a verified SHA-256 manifest in `artifacts/releases/v1.13/`.

## v1.14 / companion v1.5.1 — display volume and PWR back

- Display mode now maps front-left BOOT/− to volume down and front-right +/KEY
  to volume up. Each debounced press changes gain by five points, bounded to
  0–60. Simultaneous edges and wake-guard edges are ignored. Volume changes do
  not enable optional audio. The display's top remains the speaker edge.
- A short PWR tap ends the display session and returns to visible Settings,
  with a fresh idle timeout. Outside display mode, PWR still toggles the screen.
  The PMIC long-hold path is unchanged. The waiting-screen footer and committed
  preview now show these controls; the generated preview was visually checked.
- Portable sanitizer tests passed for visible Settings exit from dim/off,
  clock rollover, ordinary screen toggling, every supported volume, repeated
  clamping, and defensive handling of an out-of-range starting gain. Fresh
  PMIC and settings UI sanitizer tests passed, covering short/long press
  exclusion, IRQ handling, scrolling, transactions, and rendering bounds.
- On the flashed device, USB diagnostic commands exercised the same handlers
  dispatched by the physical buttons: 35 → 40 → 35, repeated steps to 60 and
  0, and restoration to 35. The PWR handler ended the session with Settings open
  and the screen bright; subsequent ordinary PWR calls toggled off/on. These
  are live shared-handler checks, not observations of physical button presses
  or a hardware power-off test. Binary display traffic remains isolated from
  ASCII diagnostic commands.
- The companion accepts volume notifications only from the current session's
  owning transport, persists the desired gain, and shows the current percentage
  in its menu. Stale audio configuration replies cannot undo newer choices.
  The signed build's full ASan/UBSan PTY transport suite passed, including
  malformed/stale/wrong-channel notifications, configuration races and missing
  acknowledgments, plus existing USB, Wi-Fi, image, and audio transport cases.
- Firmware uses 1,275,919 program bytes and 106,348 static bytes. Bootloader,
  partition table, and app digest verification passed; RTC synchronization
  succeeded at 2026-10-03 04:22:20 UTC. The companion was rebuilt with the
  existing Developer ID identity as v1.5.1/build 9 and relaunched. It connected
  through saved Wi-Fi pairing at 00:23:39 EDT and acknowledged the first image
  at 00:23:39.379. Subsequent updates completed; audio is left disabled.

Evidence: `artifacts/build-v1.14.log`, `flash-v1.14.log`,
`display-controls-v1.14.log`, `display-host-build-v1.5.1.log`,
`wifi-entry-v1.14.log`, and `wifi-v1.5.1.log`. Firmware and the signed companion
are packaged with a verified SHA-256 manifest in `artifacts/releases/v1.14/`.

## v1.15 / companion v1.5.2 — transient volume bar

- Hardware volume presses now show an opaque volume card with the numeric
  gain, a proportional bar, and MIN/MAX labels. Zero is empty and the existing
  60-point ceiling fills the bar. Every press resets the one-second lifetime,
  including repeated presses at either limit. Mac volume selections use the
  same card while a desktop session is active; audio remains opt-in.
- The Mac draws the shared RGB565 artwork over a retained desktop snapshot,
  before rotation, and sends it through the existing image pipeline. Expiry
  recomposes the clean source even when capture produces no new frames. Packets
  awaiting acknowledgment remain immutable; remaining strips of an old card
  are retired after the current ACK, preserving the acknowledged delta baseline.
- The firmware draws the same card into its existing canvas while its waiting
  screen is visible. Its one-second timer uses unsigned elapsed time and clears
  on display entry/exit. Expiry appears on the next waiting-screen redraw,
  normally within 125 ms of the deadline. No additional device framebuffer or
  continuous redraw path was added.
- Strict portable ASan/UBSan tests passed for timer expiry, repeated presses,
  wraparound, drawing bounds, every possible byte-valued gain, and proportional
  fill. The real host-parser transport tests passed for USB/Wi-Fi, 240/480 pixels,
  all four orientations, mute/mid/max, max re-press, moving and static desktops,
  clean-source preservation, expiry without fresh capture, pending-packet retry,
  session cleanup, and audio staying off. Existing transport tests also passed.
- On the flashed device, serial commands invoked the same handlers as its
  buttons. Diagnostics confirmed overlay appearance at 40, extension at 35,
  expiry, repeated presses at 60 extending the timer, and exit clearing it.
  Volume was restored to 35. This verifies the on-device timer/handler state,
  not physical button actuation; the shared generated artwork was visually
  inspected in `docs/volume-preview.png`.
- Firmware uses 1,276,679 program bytes and 106,364 static bytes, an increase
  of 760 and 16 bytes respectively. Flash verification passed for all three
  regions. The automatic clock step encountered USB re-enumeration; a separate
  retry synchronized and read back 2026-10-03 14:08:31 UTC successfully.
- The signed companion v1.5.2/build 10 reused the existing Developer ID identity
  and passed its full build/test script. After relaunch it joined the saved
  Wi-Fi session at 10:10:36 EDT, created the extended display, and received image
  acknowledgments. The user's saved audio preference was retained. A separate
  preview of actual host-composited pixels was also visually inspected.

Evidence: `artifacts/tests-overlay-v1.15.log`, `build-v1.15.log`,
`flash-v1.15.log`, `rtc-v1.15.log`, `volume-overlay-live-v1.15.log`, and
`display-host-build-v1.5.2.log`, plus `wifi-entry-v1.15.log` and
`wifi-v1.5.2.log`. Firmware and the signed companion are packaged with a verified
SHA-256 manifest in `artifacts/releases/v1.15/`.

## v1.16 / companion v1.6 — main menu, automatic display, Moss Pong

- The home gear is now a hamburger opening Settings, Remote Display, and Games.
  Display entries were removed from pet Settings. Settings remains transactional;
  PWR leaves a subpage first, then cancels the root draft back to Menu. Other
  menus, keyboards, and Pong unwind one parent at a time. A live desktop returns
  to connection options before Menu, keeping network setup reachable after a
  quick automatic connection. Home PWR screen toggling and PMIC long holds remain.
- Remote Display defers its choice until the old Wi-Fi worker finishes cleanup,
  then prefers saved Wi-Fi, otherwise attached CDC USB, otherwise network entry.
  Before any desktop pixels, an unsuccessful Wi-Fi attempt may select USB using
  a fresh nonce/handshake. No live session silently migrates. Bounded no-host
  prompts retain advertisements so a companion opened later can connect.
- The onboard network editor supports all 95 printable ASCII characters,
  case-sensitive names up to 32 bytes and passwords up to 63 bytes, touch
  scrolling, and physical-key navigation. Passwords stay masked; buffers clear
  on exit. A submitted network name becomes the next editor prefill. Existing
  TLS identity and USB-established Mac trust remain intact. First trust still
  requires USB; no LAN advertisement carries credentials or pairing secrets.
- Moss Pong has two button-driven leafy paddles, a fruit ball, and a Moss face.
  Either player serves. A subsequent press starts their paddle, reverses it
  while moving, or sends it away from an endpoint. Paddles stop at endpoints.
  First to seven wins; either button restarts. Each player's speed is independently
  configurable from 1–5 and saved in a separate NVS word, with invalid values
  falling back to 3. Gameplay uses no dynamic allocation, fixed 8 ms steps,
  swept collisions, and bounded 128 ms catch-up. The main loop requests a frame
  every 20 ms, subject to actual panel-transfer time; this is not a 50 fps claim.
- Full portable ASan/UBSan tests passed for existing care, settings, sensors,
  display, audio, and pairing plus the new menu, Pong, and network editor.
  Tests cover parent/focus restoration, transactional Back, independent speeds,
  keyboard limits/case/masking/swipes, display bounds, serving/reversal/end stops,
  scoring/restart, 287 paddle returns, 55 wall reflections, and clock wrap/stalls.
  Final targeted remote UI and connection-policy tests cover the Ready/permission
  screens, Wi-Fi preference, USB/network setup choices, deadlines, and no fallback
  after pixels arrive. The signed companion's full transport suite also passed,
  including fresh-nonce Wi-Fi-to-USB replacement and stale-session rejection.
- Live serial commands used the same handlers as the hardware buttons. They
  verified Settings subpage/root back, Games/Pong/settings/game back paths,
  serving, movement and reversal for both players, and home off/on. Separate
  paddle speeds 2/4 survived a firmware flash/restart, then were restored to 3/3.
  Network keyboard → network form → connection options → main Menu was checked.
  With the companion stopped, a real saved-network Wi-Fi attempt fell back to
  USB before streaming. A later observation was interrupted by a return to the
  main menu, so it is not evidence of a late host handshake. Earlier probe runs
  needed timing adjustments to avoid reading after a rally had ended or before
  the existing panel wake delay completed; these were probe timing issues.
- Rendering previews for the hamburger, menu, Pong field, paddle-speed settings,
  and network editor were inspected. Touch/swipe routing and the final integration
  received an independent code review. Physical button actuation and a sustained
  two-person match were not independently observed in these checks.

Final firmware uses 1,292,269 program bytes and 106,812 static bytes. Flashing
preserves pet NVS, game preferences, network credentials, and pairing. Evidence:
`artifacts/tests-v1.16.log`, `remote-policy-v1.16.log`,
`remote-ui-v1.16-final.log`, `menu-pong-live-v1.16-final.log`,
`remote-navigation-live-v1.16.log`, `build-v1.16-final.log`,
`flash-v1.16-final.log`, and `display-host-build-v1.6-final.log`.
All three flashed regions passed digest verification. The post-flash RTC step
needed one retry; readback succeeded at 2026-10-03 16:37:36 UTC in
`artifacts/rtc-v1.16-final.log`. The signed app retains the existing Developer ID
requirement and was reopened ready for Remote Display. The device was left at
the new main menu with paddle speeds 3/3. Firmware and companion are packaged
with a verified SHA-256 manifest in `artifacts/releases/v1.16/`.

## v1.17 — clear menu controls and Moss Pong cheers

- Moss Pong is named consistently in the menu, game, and documentation. The
  scoreboard mascot rests with tucked arms and lowered mint/coral pom-poms.
  Each point starts a 1,200 ms animated cheer. The initial serve screen stays
  relaxed; cheering continues through a quick serve and expires independently
  of play, including at match end and across the millisecond clock rollover.
- Menu, Settings, and Remote setup share a thin reverse-type Next / Back /
  Select strip inside the rounded screen edges. Left BOOT now moves through
  menu options, center PWR goes back, and right KEY selects. Each strip section
  also works by touch. The old clipped footer and generic tap prompts are gone;
  the pet's menu icon uses three separate horizontal bars. Settings counts and
  errors remain visible without changing the scrolling viewport geometry.
- Pet home actions, Pong player assignments, live desktop volume, and PMIC
  long holds retain their behavior. A review caught and fixed a simultaneous
  two-player input regression: Pong still dispatches both players' button edges,
  while menu/display chords cannot accidentally select an option.
- Six targeted portable ASan/UBSan suites passed: controls, menu, Pong, remote
  UI, settings, and pet rendering. They cover touch navigation and cancelled
  drags, transactional Back, disabled choices, both-player input, bounded
  rendering, exact cheer expiry, repeat scoring, restart, and clock wrap.
  Rendered previews of the home icon, all menu families, keyboards, motion
  diagnostics, and resting/cheering Moss were inspected.
- Live USB diagnostics exercised the same routing functions as physical
  BOOT/KEY: main menu → Games → Moss Pong, both players' movement/reversal,
  a real simulated rally scoring and then ending the cheer, paddle settings,
  Settings/name Back, Remote/network/password navigation, and return to Menu.
  Existing name, animal, scene, time zone, subtitle, sensitivity, and paddle
  speeds were unchanged. These checks exercised firmware handlers; they did
  not independently observe a person's physical button presses or touch.

Firmware uses 1,293,319 program bytes and 106,812 static bytes. All three flashed
regions passed digest verification. The initial post-flash RTC command timed
out during USB re-enumeration; a retry read back 2026-10-03 16:47:03 UTC. The
first live probe expected an extra connection-options Back step before any
desktop pixels existed; correcting that probe to the existing navigation
policy produced the full passing run, without another firmware change.

Evidence: `artifacts/tests-v1.17.log`, `build-v1.17.log`, `flash-v1.17.log`,
`rtc-v1.17.log`, `live-v1.17.log`, and its reproducible `probe-v1.17.py`.
The companion remains signed v1.6; its code and permissions were not changed.
The device was left at the main menu with its existing paddle speeds 3/3.
The compiler cleaned the companion's build directory, so the identical v1.6
bundle was restored from the previous release archive and relaunched. macOS
signature verification with normal certificate access passed. Firmware and the
companion are packaged with verified SHA-256 hashes in `artifacts/releases/v1.17/`.

## v1.18 — tap-to-talk personality with persistent variety

- Tapping the pet requests a joke through the existing release/debounce path.
  The card keeps the HUD, meters, and care controls visible, with a small
  deadpan portrait for Sloth, Cat, or Frog. Tapping the card requests another;
  duration scales with text length. Menu/care/power actions dismiss it. Jokes
  preserve care and sleeping state; ignored sleeping shakes preserve speech.
- The bank contains 2,048 distinct normalized lines across 12 topics, with
  272 sloth entries and 1,716 shared-premise families. Authors reviewed semantic
  mood tags, rewrote 20 cross-file near-duplicates, and linked 15 related premise
  pairs across files. A further review sampled 120 lines across all topics.
  The generator checks glyphs, word lengths, display fit, IDs, metadata, and
  uniqueness. Committed flash data must match its JSONL sources and family map.
- Selection uses an unseen bitmap, seeded weighted reservoir sampling, a
  64-line recent ring, and 24-line premise history. Every line appears once per
  complete cycle. Recent lines remain excluded across cycle boundaries; mood
  never restricts selection to a small pool. Food, joy, rest, and sleeping state
  adjust positive mood weights after freshness preferences. Topic spacing is
  also applied before mood weighting.
- A 676-byte versioned record includes the bitmap, PRNG, cycle, recent IDs,
  corpus fingerprint, and CRC. History is saved in the main loop before speech
  appears, outside display DMA callbacks. Save failure holds the selected line
  pending, blocks new selections, and retries without consuming further lines.
  UI cancellation can discard the pending reveal while persistence still retries.
- The complete portable ASan/UBSan suite passed. New selector tests cover two
  2,048-line cycles, 4,096-line capacity, recent-line and premise spacing,
  measurable bias for every mood, deterministic restore, every-byte corruption,
  bank version mismatch, and tiny/empty/oversized fixtures. The real bank test
  traversed all 2,048 lines while changing care values and restoring repeatedly;
  all were unique and fit the card, with only one consecutive same-topic pair.
  Additional speech checks cover all species/naps, timing wrap, single taps,
  drag cancellation, invalid coordinates, long words, malformed bytes, and
  drawing bounds. Existing care, clocks, controls, menus, Pong, display, audio,
  and Wi-Fi store tests also passed.
- The flashed device produced 20 distinct saved jokes, then four further
  distinct lines after a real hardware reset. The restored seen count was 20;
  names, settings, speeds, and nap state remained intact. Requests in menus or
  with the panel off were ignored. Selection took 2,005–4,153 microseconds;
  this excludes flash-write and screen-transfer time. USB diagnostics used the
  same request handler as touch; physical finger taps were not independently
  observed in this run.

Firmware uses 1,464,647 program bytes and 107,508 static bytes, an increase of
171,328 and 696 bytes respectively over v1.17. All three flashed regions passed
digest verification. The first RTC sync timed out during USB re-enumeration;
the retry succeeded at 2026-10-03 17:22:24 UTC. Firmware compilation now uses
`build/firmware/`, preserving the signed companion and host tests in neighboring
directories while copying the three flash inputs to their existing paths.

Evidence: `artifacts/humor-bank-v1.18.log`, `humor-selector-v1.18.log`,
`tests-v1.18.log`, `build-v1.18.log`, `flash-v1.18.log`, `rtc-v1.18.log`,
`humor-live-before-v1.18.log`, `humor-live-after-v1.18.log`, and the reproducible
`probe-humor-v1.18.py`. The device was left on the pet screen. Companion v1.6 is
unchanged; firmware and the companion are archived with verified SHA-256 hashes
in `artifacts/releases/v1.18/`.

## v1.19 — game audio, Sloth Tetris, pause confirmation, and scoreboards

- Main menu order is Games, Remote Display, Settings, then Back to Pet. Back is
  included in the hardware focus cycle on every game/menu page. Both game
  submenus offer Play, Sound / Options, and High Scores. Pong retains independent
  paddle speeds; both games independently save Music and Effects switches.
  Defaults are music off and effects on. Remote desktop audio is unchanged.
- Added allocation-free Sloth Tetris with a deterministic seven-piece bag,
  collision-safe rightward movement with edge wrapping, clockwise wall/floor
  kicks, next piece and ghost, a 350 ms lock delay with at most 15 resets,
  one-to-four line clears, points, levels, and game over. A review caught and
  fixed an edge case where wrapping off a ledge could erase accrued lock time
  after the reset cap. The screen uses a MOVE / PAUSE / TURN header and sloth art.
- Short PWR pauses either active game and opens a confirmation with Continue
  selected. PWR again resumes; Leave Game is explicit. Both simulation clocks
  discard paused wall time, and the speaker stops during the dialog. Final
  scores remain stable until the user continues to optional name entry; Pong
  button handling no longer automatically starts another match.
- A portable bounded synthesizer generates original looping music and nine
  effect cues as 16 kHz signed mono PCM. Independent switches, effect priority,
  attack/release envelopes, and a 6,000-sample amplitude ceiling are tested.
  The main loop alone starts/stops the existing DAC driver and queues samples;
  no NVS/I2S/codec operations occur in panel callbacks. Game sessions use volume
  30 and a 2,304-sample target queue. Leaving a game releases the speaker without
  changing Remote Display's audio preference or volume.
- Two named top-ten tables and four audio switches share a canonical 352-byte
  explicit-endian CRC record, separate from care/settings/joke history. Pong
  ranks completed wins by winning margin; Tetris ranks by points then lines.
  Existing exact ties retain priority. Names can be entered by touch or hardware
  navigation, defaulted, or skipped. Boards show five entries per page and best
  score. Failed writes preserve RAM state, show a retry notice, and retry after
  five seconds.
- Full portable ASan/UBSan suite passed. New coverage includes 700 seeded piece
  draws, 443 randomized locks across 25 games, line compaction and scoring,
  spawn/partial-top-out, lock reset limits, pause/clock wrap, bounds, and game
  events. Audio tests cover chunk invariance, different tunes, switches, cues,
  priorities, envelopes and bounds. Records tests cover full tables, stable
  ranking, names, every-byte corruption and 2,000 CRC-repaired mutations with
  canonical re-encoding or unchanged-output rejection. Modal tests cover
  Continue/Leave, all keyboard keys, limits/fallback, Save/Skip, paging, and
  guarded drawing. All prior pet, speech, clock, display, audio-driver, Wi-Fi,
  sensor and PMIC tests also passed. Previews were visually inspected.

Firmware uses 1,481,743 program bytes and 108,316 static RAM bytes, respectively
17,096 and 808 bytes above v1.18. All three flash regions passed digest checks;
RTC synchronization succeeded on the first attempt at 2026-10-03 17:50:34 UTC.

Live USB probes exercised the same handlers used by the physical side buttons
and PWR. Both games paused with identical snapshots across a delay, resumed
without catch-up, and required explicit Leave before returning to their menu.
The probe completed Pong to 7–0, entered TESTPONG using the hardware keyboard
route, paged the board and returned by its selectable Back. Tetris verified
rightward movement, rotation, repeated wrapping, frozen pause state, natural
gravity/lock/game-over, TESTBLOCK name entry, and music-only versus fully muted
sessions. The completed Tetris run rendered 1,687,040 audio samples after its
last resume (over 105 seconds); all 43 sampled audio states reported zero
underruns, overflows, or I2S write errors. Acoustic output and physical finger
presses were not independently observed; the existing speaker path, synthesized
PCM tests, production handlers and live I2S counters were used for validation.

A real hardware reset restored one score in each table and distinct per-game
music/effects switches. Pet name SLOTH, NYC scene, Eastern time, hidden subtitle,
shake sensitivity 2, paddle speeds 3/3, and joke seen-count 24 were retained.
The pre-test 64 KB pet-preferences partition was then restored and verified
byte-for-byte to remove the temporary scores and switches. Its saved UTC
continues normal elapsed-care accounting; firmware and Wi-Fi pairing partitions
were unchanged. Final validation confirmed empty boards, music off/effects on,
and preserved pet settings/history. The device was left at the new main menu. The companion's existing signed v1.6 bundle
passed signature verification and was relaunched; no signing or permission identity changed.

Evidence: `artifacts/tests-v1.19.log`, `build-v1.19.log`, `flash-v1.19.log`,
`games-live-v1.19.log`, `games-reset-v1.19.log`, `games-restored-v1.19.log`,
`games-cleanup-v1.19.log`, `games-clean-v1.19.log`, and the reproducible
`probe-games-v1.19.py`. Three firmware binaries and the unchanged signed
companion are packaged with verified SHA-256 hashes in `artifacts/releases/v1.19/`.

## v1.20 — Leaf Sweep, three-game saves, and menu icons

- Added Leaf Sweep, a bounded touch game with continuous swept collision tests,
  growing/respawning leaves, warning cans, cooling energy, and a score based on
  whole-run collection efficiency. Pause excludes elapsed time and clears the
  old stroke. A fatal swipe cannot also dismiss the final screen: a release and
  fresh tap are required before optional name entry. Main-loop audio ownership
  extends to a third original tune and separate leaf-collection/can-hit cues;
  PCM golden checks confirm the existing Pong/Tetris tunes are unchanged.
- Game records now use a canonical 526-byte v2 record containing three named
  top-ten tables and six independent audio switches. The decoder also accepts
  the released 352-byte v1 format under the same NVS key, preserving both older
  boards and their switches while initializing Leaf Sweep with an empty board,
  music off, and effects on. Tests cover full-table migration, all 16 legacy and
  64 current preference combinations, ranking/ties, every-byte corruption,
  truncation, reserved fields, and unchanged output on rejection.
- The full portable ASan/UBSan suite passed. Leaf Sweep randomized play covered
  51 completed games, 6,277 collection events, and 307 can-hit events, alongside
  exact collision ordering, growth/warnings/expiry, cooling, safe spawning,
  scoring, pause/clock wrap, stalled frames, frozen endings, and drawing bounds.
  Three-game menu/overlay tests cover name entry, Save/Skip, paged scoreboards,
  independent preferences, and guarded rendering. Icon tests verify nonempty
  foreground-only glyphs, 16-pixel bounds, and edge clipping. Updated menu,
  keyboard, pause, and score previews were visually inspected.
- Bounded USB touch diagnostics route through the same Leaf Sweep handler as
  the touchscreen. Malformed, expired, or cancelled screen-context lines remain
  in discard mode until newline, so delayed bytes cannot become unrelated
  control shortcuts. Parser tests cover coordinate bounds, malformed tails,
  explicit discard, and timeout rollover. Existing pet, humor, display, Wi-Fi,
  audio-driver, clocks, sensors, Pong, and Tetris checks also passed.

Firmware uses 1,492,339 program bytes and 108,812 static RAM bytes. All three
flashed regions passed digest verification. RTC synchronization succeeded at
2026-10-03 18:19:53 UTC.

A live USB probe exercised the production touch and button handlers. A tap
collected a mature leaf and exposed the paw coordinates; a continuous stroke
collected two more along its segment. Touching Pause stopped audio and held the
complete game snapshot and object list constant. PWR resumed without counting
the pause. Can contacts raised energy, a released contact allowed cooling, and
idle time reduced the pace score. Six naturally spawned cans ended the run at
100 energy, with three leaves and a frozen score of 506 after 15.527 active
seconds. The final gesture did not skip the result screen. The probe entered
TESTLEAF through the hardware keyboard path, saved the score, paged the board,
and returned with selectable Back. Fully muted play and explicit quit also
passed; independent Leaf switches did not change Pong/Tetris preferences.

After the last resume, 239,616 audio samples were rendered (about 15 seconds).
All sampled audio states reported zero underruns, overflows, or I2S write errors.
Physical finger motion and acoustic output were not independently observed;
touch-handler diagnostics, the production sensor tests, synthesized PCM tests,
and live I2S counters provide the validation evidence.

A hardware reset restored the Leaf score table entry and both Leaf sound
preferences. A fresh pre-test 64 KB pet-preferences backup preserved changes
made since installation, including joke seen-count 30 and Leaf music enabled.
That exact backup was restored and verified by digest after the test, removing
the temporary score while retaining the user's latest pet settings, joke history,
and audio preferences. Saved UTC continues normal elapsed-care accounting;
firmware and Wi-Fi pairing partitions were untouched. Final checks confirmed
three empty scoreboards, music flags 0/0/1, effects 1/1/1, and the preserved
SLOTH/NYC/Eastern/hidden-subtitle/shake-2 settings and paddle speeds 3/3.
The device was left on the illustrated Games menu. The unchanged signed v1.6
companion passed signature verification and was relaunched.

Evidence: `artifacts/tests-v1.20.log`, `touch-command-v1.20.log`,
`build-v1.20.log`, `flash-v1.20.log`, `leaf-live-v1.20.log`,
`leaf-reset-v1.20.log`, `leaf-restored-v1.20.log`,
`leaf-cleanup-write-v1.20.log`, `leaf-cleanup-verify-v1.20.log`,
`leaf-clean-v1.20.log`, and the reproducible `probe-leaf-v1.20.py`.
Three firmware binaries and the unchanged signed companion are archived with
verified SHA-256 hashes in `artifacts/releases/v1.20/`.

## v1.21 — hold Move to drop faster in Sloth Tetris

- A physical left BOOT press still moves right once. Holding for 300 ms after
  its debounced edge enables 50 ms-per-row gravity; release restores normal
  level-based gravity. The top rail displays DROP while active. Rotation works
  during a hold, and holding across a piece spawn keeps fast fall active.
  Scoring, collision rules, the 350 ms landing delay, and its reset cap are unchanged.
- Only accepted physical gameplay presses arm the hold gate. Entry, pause,
  wake, and exit reset it and require release before another hold. Menu presses,
  simultaneous side-button edges, and USB tap shortcuts do not arm it. Speed
  changes settle prior elapsed time before clearing the gravity accumulator;
  partial simulation steps and grounded lock time remain intact. Resume rebases
  time and clears fast fall without simulating the pause.
- Targeted strict C++11 ASan/UBSan engine and gate tests passed. Coverage includes
  the 299/300 ms threshold, release/repress, stale DMA edges, pause/wake gating,
  clock wrap, one horizontal move per press, rotation, ordinary/fast gravity,
  mode switching, stalls, held drop across spawn, both game-over paths, and
  renderer bounds. One-millisecond mode toggling cannot evade the lock timer.
  Randomized engine play completed 165 games with 2,323 settled pieces.
- Firmware compilation passed: 1,492,739 program bytes and 108,820 static RAM
  bytes, increases of 400 and 8 bytes over v1.20. All three flash regions passed
  digest verification; RTC synchronization succeeded at 2026-10-03 18:39:52 UTC.
  Saved pet options, three audio preferences, and scoreboards were retained.
- A live USB probe checked one-step movement without a latched drop, normal
  gravity, rotation, frozen pause state, resume, and explicit quit. It saved no
  test score. Audio counters remained free of underruns, overflows, and write
  errors. Physical holding was validated with the production hold gate and
  engine together in host tests, not by an independently observed physical
  button hold. The device was left at the Sloth Tetris submenu, ready to play.

The existing signed companion v1.6 was relaunched unchanged. A pre-update pet
preferences backup is retained, and the firmware/companion release is archived
with verified SHA-256 hashes in `artifacts/releases/v1.21/`. Evidence:
`artifacts/tetris-hold-v1.21.log`, `tetris-game-v1.21.log`, `build-v1.21.log`,
`flash-v1.21.log`, `tetris-live-v1.21.log`, and `probe-tetris-v1.21.py`.

## v1.22 — score keyboard, colorful option icons, and saying dismissal

- Score names now use a five-column scrolling keyboard with 36x28 logical
  targets (72x56 panel pixels), doubled letter size, and fixed Space/Delete/
  Clear/Save/Skip controls. Hardware Next reveals the selected key. A first
  release commits a tap; modest drift into a gap remains valid, while larger
  movement scrolls without typing. The keyboard no longer uses the generic
  recognizer's repeated-release delay or resets all input after each letter.
- Cancelled touches and the application's wake gate now acknowledge the same
  release. In-place hardware edits/navigation do not demand an extra release
  from an already lifted finger. Regression tests cover the previously lost
  first tap after interrupting a held contact, as well as typing immediately
  after hardware selection and scrolling.
- All 43 option/action icons have two to four part-specific colors, with
  separate selected-card palettes. Tests enforce at least 3:1 contrast against
  their intended card backgrounds, unchanged transparent silhouettes, bounded
  drawing and clipping. The pet's hamburger button remains one color, as
  requested. Gameplay, menus and scoreboards consistently use 3-TOED TETRIS;
  save identifiers and existing scores are unchanged.
- Either side button dismisses visible or pending speech on pet home before
  selection/care actions. That first press only closes the saying; subsequent
  presses retain normal controls. Clearing a pending reveal does not cancel
  queued joke-history persistence.

The full portable ASan/UBSan suite passed, including all game/record/audio,
touch, sensor, pet, display, clock and Wi-Fi tests. New overlay cases cover
rapid first-release taps, holds, drift, all keys/digits, swipes and clipping,
fixed controls during partial scrolling, hardware auto-reveal, cancellation,
and all three boards. Top/middle/bottom keyboards, both icon palettes, menu,
settings, gameplay and scoreboard previews were visually inspected.

Final firmware uses 1,495,593 program bytes and 108,844 static RAM bytes.
Bootloader, partition table and app digests matched after flashing; RTC sync
succeeded at 2026-10-03 18:59:17 UTC. The user's pet settings, music/effects
switches, and one Tetris scoreboard entry survived the update. A pre-update
preferences backup remains available; it was not restored over newer activity.

On-device USB probes use the production button/touch handlers. Both side-button
routes dismissed sayings while selection, sleep, food and joy stayed unchanged;
the next ordinary press advanced selection normally. A naturally completed
Leaf Sweep run reached the shared name keyboard. Live checks entered ABCDE in
one rapid down/up sequence, scrolled and typed K09, exercised fixed edits and
hardware auto-reveal, and successfully entered B immediately after interrupted
touch and screen-off/wake sequences. Skip, scoreboard paging and Back worked.
No validation score was saved; the original board counts remained 0/1/0.
Physical finger presses were not independently observed. A diagnostic held-touch
check was adapted to read status before the real sensor's release sample;
portable tests cover sustained contact timing. The final keyboard probe passed.

The device was left on Games and the unchanged signed companion was relaunched.
Evidence: `artifacts/tests-v1.22.log`, `build-v1.22.log`, `flash-v1.22.log`,
`ui-live-v1.22.log`, `keyboard-live-v1.22.log`, and `probe-ui-v1.22.py`.
Firmware and companion binaries are archived with verified SHA-256 hashes in
`artifacts/releases/v1.22/`.

## v1.23 — Forest Fidget

Games now has a fourth, direct-launch Forest Fidget option with twelve distinct
toys: Dew Pond, Acorn Roll, Mushroom Pop, Fern Brush, Firefly Jar, Pinecone Spin,
Pebble Stack, Moss Squish, Leaf Globe, Vine Swing, Rainstick, and Zen Rake. The
left/right hardware buttons select previous/next with wraparound. The matching
touch rail also navigates, while a short PWR tap returns directly to the Games
row without an exit confirmation. There are no scores, game-over states, sound
preferences, or persistent records for this mode.

Each toy supports touch without an IMU. Motion drives rolling, swarming, spinning,
tumbling, drifting, swinging, and bouncing, and can bend ferns or soften sand.
State uses bounded arrays, fixed 20ms simulation steps and at most 200ms catch-up.
Invalid motion is rejected, stale gravity expires, and input is cleared across
toy changes, exit and wake. Existing elapsed pet care continues independently.
The mode stays bright while open and does not invoke pet shake rewards.

Portable sanitizer cases exercise all twelve touch-only toys and 120,000
randomized simulation steps, plus inertial flicks, deliberate stationary holds,
slow pond strokes, spring recovery, lure forces, pendulum geometry, peg collisions,
jar/globe containment, sand interpolation and held-rake preservation. They also
check finite bounds, deterministic seeds, invalid input, pause/rebase, long
stalls, clock wrap, cycling, and cancelled contacts. Renderer cases cover twelve
distinct scenes, visible interaction changes, fixed header/footer clipping,
unchanged snapshots and malformed input bounds. Menu tests cover all four rows,
direct launch/return focus, hardware Back, and the unchanged three score/audio
slots.

The renderer fills rotated ellipses with scanline spans, uses a fixed-point
48-sample outline table, and caches rotations. Particle and peg loops reject
noncollisions by squared distance before taking square roots. Direct old/new primitive
comparisons across radii 1–102 and five rotation angles stayed within one pixel.
All twelve idle and interaction previews were inspected; leaves and fireflies
remain inside their drawn enclosures.

An initial live probe found that the generic tap recognizer required a second
release packet to complete a quick navigation-strip tap. Forest Fidget now has
a dedicated first-release recognizer: each gesture emits once, field-to-header
drags stay cancelled, and one release clears both navigation and wake gates.

The full portable ASan/UBSan regression suite passed. The changed controls,
engine and renderer tests were rerun after the final fixes and optimizations.
Final firmware compiles without warnings, using 1,530,149 program bytes and
110,972 static RAM bytes. All three flashed regions passed digest verification.
The post-flash application initially did not answer USB queries; an additional
USB reset restored communication, after which RTC sync/readback succeeded at
2026-10-03 19:37:18 UTC.

Final on-device probes exercised the production input handlers for every toy,
continuous contact/release, animation progression, both wrap directions, a held
contact across a toy change, single-release and rapid consecutive navigation
taps, field-to-header drag rejection, panel sleep/wake rebasing, touch Back, and
direct PWR return to the selected Games row. All passed. Actual fresh IMU samples
reached the toys; physical finger/button presses and directional tilting were
not independently observed. Those input trajectories and physics are covered by
portable tests, while USB diagnostics exercise the installed handler paths.

A short per-toy device timing sample measured about 10–11.5 rendered frames/sec
with 42–46 fresh motion samples/sec. Mushroom Pop improved from about 7.4 to 11.3
frames/sec and Pinecone Spin from 8.3 to 11.5 after scanline drawing. These are
short local measurements, not a guaranteed frame rate for every gesture. Free
heap remained around 134.7KB through the final pass. The original scoreboard
counts (0/1/0), three music/effect preferences, pet options and joke history were
retained; no test score was saved.

The device was left at Dew Pond, and the existing signed companion v1.6 was
relaunched unchanged. A pre-update preferences backup is retained. The three
firmware binaries and companion archive have verified SHA-256 hashes in
`artifacts/releases/v1.23/`. Evidence: `tests-v1.23.log`,
`fidget-controls-v1.23.log`, `fidget-engine-v1.23.log`, `fidget-render-v1.23.log`,
`build-v1.23.log`, `flash-v1.23.log`, `reset-v1.23.log`, `clock-v1.23.log`,
`fidget-live-v1.23.log`, and `fidget-performance-v1.23.log` under `artifacts/`.

## v1.24 — dedicated Tetris touch controls

Removed the hardware Move long-press speed change. BOOT still moves one column
right per press and wraps at the right wall; KEY still rotates clockwise. A held
hardware button produces only its initial action. Four touch buttons provide
bounded Left/Right, Rotate, and immediate Drop to the ghost landing position.
Drop locks one piece with existing line scoring and no additional drop points.

The full twenty-row board uses 8-pixel cells at (24,56). Score, lines, level,
next-piece preview and a compact sloth occupy the upper right. The four buttons
are 48×42 logical pixels (96×84 panel pixels), separated by 12 logical pixels.
They act on release; gaps, field touches, cross-button drags and out-and-back
contacts do nothing. Hardware actions, pause and wake cancel pending contacts.
A generation token prevents a held contact from acting on a newly spawned piece.
A completed touch also satisfies the destination release gate when opening the
pause dialog or game-over score entry.

The full portable ASan/UBSan regression suite passed. Engine cases cover all 28
piece orientations, bounded movement, retained hardware wrapping, collision and
lock-reset rules, exact ghost-position drops, one-to-four-line scoring, natural
lock races, both game-over paths, pause and clock/generation wrap. Randomized play
settled 1,170 pieces across 77 complete games. Touch cases cover dead zones,
first-release activation, rapid taps, holds, drag cancellation and reset gating.
Renderer cases check all seven next shapes, maximum score/line/level values,
pressed feedback, game-over controls and framebuffer bounds. Falling, stacked,
maximum-metric, pressed, next-piece and game-over previews were rendered; the main
layouts were visually inspected, and the README preview was updated.

Firmware compiled without warnings: 1,532,291 program bytes and 110,988 static RAM
bytes. All three flashed regions passed digest verification, normal boot
succeeded, and RTC synchronization completed at 2026-10-03 19:57:36 UTC.

The live USB probe exercised the installed production input handlers: hardware
Move/Rotate, all four touch actions, no Drop on contact-down, one Drop on release,
duplicate releases, dead gaps, field taps, cross-button and out-and-back drags,
hardware cancellation of held Drop, rapid taps, both touch wall limits and
hardware wrap. It also checked PWR pause and frozen time, hardware and touch
Continue, touch Pause, panel off/wake gating, game over, immediate next-tap Skip,
and a fresh game. All passed. These diagnostics inject logical contacts and
button edges; physical finger accuracy and held GPIO trajectories were not
independently observed. The recognizer and debounced-edge code cover those paths.

Original score counts (0/1/0), all three music/effect preferences, pet settings,
and joke history were retained; no test score was saved. The device was left with
a fresh Tetris game paused and Continue selected. The signed companion v1.6 was
relaunched unchanged. A pre-update preferences backup is retained. Firmware and
companion archives have verified SHA-256 hashes in `artifacts/releases/v1.24/`.
Evidence under `artifacts/`: `tests-v1.24.log`, `build-v1.24.log`,
`flash-v1.24.log`, `backup-v1.24.log`, `before-v1.24.log`, and
`tetris-touch-live-v1.24.log` with its `probe-tetris-touch-v1.24.py` driver.

## v1.25 — consistent pet/menu hardware buttons

Pet home now uses the menu button order: front-left BOOT advances through
Feed, Play, Nap and Menu; front-right KEY activates the highlighted action.
The routing predicate now includes pet home in menu navigation. Game controls,
live-display volume, menu mappings, saying dismissal and wake-only handling
retain their previous paths.

Existing button-policy tests passed with ASan/UBSan, including simultaneous
presses, Pong, screen power and volume bounds. The firmware build completed
without warnings (1,532,247 program bytes, 110,988 static RAM bytes), all flashed
regions passed digest verification, and RTC sync completed at
2026-10-03 20:01:54 UTC. A live USB diagnostic probe exercised the installed
handlers for all four pet highlights and wrap, Right opening the highlighted
Menu, menu Next/Select, back to pet, off-screen action rejection and wake-only
behavior. All passed. Settings, score counts, music/effects and joke history
matched the pre-update state. Physical button presses were not independently
observed. The device was left on pet home with Feed selected, and the signed
companion v1.6 was relaunched unchanged.

Evidence: `artifacts/controls-v1.25.log`, `build-v1.25.log`, `flash-v1.25.log`,
`before-v1.25.log`, `pet-buttons-live-v1.25.log` and
`probe-pet-buttons-v1.25.py`. Firmware and companion binaries are archived with
verified SHA-256 hashes in `artifacts/releases/v1.25/`.

## v1.26 — Storage settings and read-only card diagnostics

Settings has a scrolling Storage option with a multicolor icon. The page shows
physical flash capacity, current firmware image size against its allocation,
and saved-data slot usage. MicroSD shows filesystem capacity/used/free when
readable, or a clear unavailable/error state. Known physical card capacity is
retained if filesystem usage cannot be read. Refresh runs another check; Back,
PWR and the top navigation strip return to the Storage row with the settings
draft unchanged. The page is diagnostic and has no persistent preferences.

The backend uses the existing IDF SPI2 bus with SD CS6. At boot it attempts to
put an inserted card into SPI mode after attaching the LCD IO device and before
sending any LCD command. For subsequent scans, the main loop pauses LCD traffic
through card initialization; a worker callback releases that gate before FAT
counting. A result queue publishes a complete snapshot without sharing mutable
UI state. Remote display entry waits for a scan to finish. Checks have separate
four-second initialization and thirty-second recount budgets, bounded SD
commands and periodic idle yields; native SPI bus acquisition still depends on
normal display-driver arbitration.

The private disk adapter rejects writes and trim, and never opens files,
formats, repairs NVS, or repartitions. Cleanup unregisters the private filesystem,
removes only its SD device, and restores CS6 high without remapping shared pins.
FAT free space is recounted rather than trusting the cached FSInfo count. The
installed library supports FAT12/16/32 and the first supported volume, not
exFAT. No wired card-detect GPIO exists, so a nonresponse is Unavailable rather
than an unsupported assertion that the slot is empty.

The full ASan/UBSan portable regression suite passed. New backend cases cover
read-only disk enforcement, >4GiB capacity arithmetic, malformed counts, partial
failures, deadlines, callback ordering, recursion rejection and cleanup. Board
mocks verify card preparation occurs between bus/LCD IO attachment and the first
panel command. Settings tests cover hardware and touch entry/Refresh/Back,
repeated release, drag rejection, draft preservation, every status, malformed
snapshots, large units and framebuffer bounds. Ready, checking, unavailable,
error-with-capacity and scrolled-menu layouts were rendered and inspected.

Final firmware compiles without warnings: 1,566,617 program bytes and 111,188
static RAM bytes. All three installed regions passed digest verification. The
initial post-flash clock handshake and first reset retry timed out; a separate
serial read confirmed normal application operation and continued rendering.
A subsequent clock sync/readback succeeded at 2026-10-03 20:23:54 UTC.

The installed firmware reported physical flash 16,777,216 bytes; firmware image
1,566,720 bytes within 3,145,728 reserved bytes; and 163 occupied entries out of
4,032 across the two 64KiB NVS partitions. This corresponds to 5,216 occupied
entry bytes out of 129,024 entry bytes (metadata included). Six on-device scans
returned microSD Unavailable with ESP_ERR_TIMEOUT (263), with no card capacity
established. A mounted physical card was not available for a live filesystem
usage test; that path is covered by SDK compilation and mocked card/FAT cases.

The live probe exercised hardware navigation, repeated Refresh, continued frame
progression, Back to the selected Storage row, reentry, and closing without Save.
Heap stayed stable after the first check. Settings, music/effects, score counts
(0/1/0) and joke history matched the pre-update state. Physical finger presses
were not independently observed; touch behavior is covered by the portable UI
tests. The device was left on Storage and the signed companion v1.6 relaunched
unchanged. Source and validation are committed; firmware/companion binaries have
verified SHA-256 hashes in `artifacts/releases/v1.26/`.

Evidence under `artifacts/`: `tests-v1.26.log`, `build-v1.26.log`,
`flash-v1.26.log`, `reset-v1.26.log`, `clock-v1.26.log`,
`clock-retry-v1.26.log`, `boot-debug-v1.26.log`, `before-v1.26.log`,
`storage-live-v1.26.log` and `probe-storage-v1.26.py`.


## v1.27 — fitted speech and full-size pet reactions

Pet sayings now use a centered bubble sized to the wrapped text in both width
and height, anchored above the care meters. The full-size animal is always
rendered underneath; the miniature speech portrait is removed. The persistent
status line is at the top, below the name/optional animal subtitle. Meter labels
and bars sit at y173/y184; the care buttons and their touch targets retain their
positions. Speech hit testing follows the drawn rounded bubble and pointer,
plus the original pet ellipse, instead of owning an invisible fixed rectangle.

Each successfully saved/displayed saying starts a four-second reaction: shrug,
wave, facepalm, head tilt, nod, stretch, peek, giggle, sway, point, heart hands,
or slow clap. An independent twelve-entry shuffle bag guarantees full coverage
and prevents an immediate repeat at bag boundaries. It resets at startup without
changing the persistent joke selector/history. Articulated arms and transformed
full-size heads move with easing, then settle to idle while the text remains.
Sleeping animals retain their resting pose. Cancellation uses the existing
speech clear path for buttons, care, menu entry and screen-off. Active gestures
request an 80ms frame interval; ordinary idle and dimmed pacing stay unchanged.
No extra framebuffer or dynamic allocation is introduced.

The complete portable ASan/UBSan regression suite passed. New checks cover
shuffle coverage across 100 cycles for four seeds, no adjacent repeats, expiry,
clock wrap and cancellation. Renderer checks exercise all twelve reactions for
all three animals across eight times, distinct anatomy changes, framebuffer and
HUD/status/meter bounds, settling, invalid inputs and sleeping suppression.
Speech tests compare eighteen animal/sleep/text combinations pixel-for-pixel
outside the overlay and match the hit area against the rendered bubble. All
2,048 corpus entries remain untruncated: 1 uses one row, 565 use two, 1,397 use
three and 85 use four. Their bubbles leave the eyes visible. Short/long/napping
and NYC layouts, plus a labeled twelve-gesture contact sheet, were rendered and
visually inspected. Physical panel pixels/finger taps were not independently
observed; the previews use the firmware renderer and touch geometry is covered
by portable tests.

Firmware compiled without warnings: 1,570,159 program bytes and 111,212 static
RAM bytes. All three flashed regions passed digest verification. Clock sync
succeeded at 2026-10-03 20:47:19 UTC. Live USB diagnostics exercised installed
joke/button/menu handlers: twelve unique gestures followed by a nonrepeating
boundary, four-second gesture expiry while speech stayed visible, both side
buttons dismissing without selection/care side effects, and menu cancellation.
Forty frames were presented during the approximately four-second expiry check.
Settings, animal/name/scene/timezone/subtitle, game scores (0/1/0), music/effects,
and nap state were retained. Fifteen test sayings intentionally advanced the
persisted joke history from 47 to 62 entries. The device was left on pet home.
The signed companion v1.6 was relaunched unchanged, preserving its identity.

Evidence in `artifacts/`: `tests-v1.27.log`, `build-v1.27.log`,
`flash-v1.27.log`, `before-v1.27.log`, `pet-reactions-live-v1.27.log`, and
`probe-pet-reactions-v1.27.py`. Firmware and the unchanged companion archive have
verified SHA-256 hashes in `artifacts/releases/v1.27/`.

## v1.28 — utilities, shared Wi-Fi setup and livelier pets

Tetris's left BOOT button now distinguishes a short release from a 650ms hold:
the release moves right with the existing hardware wrap, while a hold hard-drops
once without first moving sideways. Continued hold/release cannot affect the
next piece. Piece changes, pause and wake cancel the gesture. Right KEY still
rotates; PWR still pauses rather than dropping. Forest Fidget receives the
corrected panel Y acceleration sign. Pet art adds the Sun Conure and bounded
species-specific idle outings, grooming and scene movement; cosmetic dozing
does not change Nap or care levels. Game visuals were refreshed while retaining
their rules, controls and score records.

The factory app allocation is now 8,388,608 bytes at `0x10000`. Both saved-data
partitions retain their existing offsets and sizes: `nvs` at `0xFE0000` and
`pet_nvs` at `0xFF0000`, each 65,536 bytes. Enlarging this allocation does not
format saved data or turn the remaining flash into a filesystem.

Utilities contains Remote Display, Wi-Fi Explorer and Bluetooth Explorer.
Explorers retain at most 24 identities with scrollable lists/details and bounded
advertisement data. Discovery uses passive Wi-Fi scanning or BLE advertisements;
it does not connect, pair, advertise or send active scan requests. Bluetooth
Classic is unsupported. Detail guidance projects synchronized gyro rotation
onto measured gravity and correlates sweep angle with recent RSSI. It requires
adequate samples, bidirectional movement and correlation before showing a
stronger-left/right hint; otherwise it reports uncertainty. This is experimental
signal-strength guidance, not angle-of-arrival, distance or a reliable bearing.
Gyro sampling stops outside details, and scanner ownership is released on exit
or display sleep before another radio mode can start.

The `codex/lhp-browser` shared-network changes add a separate Wi-Fi Networks
menu and reusable SSID/password editor. Its bounded active scan is for network
selection and connection, distinct from passive Explorer discovery. Wi-Fi Networks
and Remote Display use one station service with exclusive ownership. A separate
CRC-protected network record migrates legacy credentials only when absent;
forgetting writes a durable marker and retains the Mac's TLS identity. The
merged LHP work remains an isolated offline experiment: real host fixture
rendering and a separate C6 build are documented in
`experiments/lhp_browser/VERIFICATION.md`. It is not an installed public-web
browser, and its isolated memory figures are not combined-app measurements.

Live development exposed two SDK defects. Arduino's unconditional scan-done
handler consumed raw scan results and allocated space for every AP, including
when the scan was started directly. Both scanners now keep Arduino Wi-Fi off
while owning the raw IDF scanner and copy only their bounded results. BLE exit
initially failed heap poisoning in controller teardown. The pinned C6 library
allocates a three-byte block, then writes a four-byte pointer at offset three;
ELF disassembly matches the live failure. A linker wrapper expands only that
allocation shape to seven bytes, preserving vendor ownership/free behavior.
The workaround requires C6, Arduino 3.3.0 and IDF 5.5.0, and fails compilation
on an SDK change pending re-audit. Heap checking remains enabled.

To recover RAM consumed by linked Bluetooth code/data, LZ4 decoding now borrows
the existing idle pet framebuffer instead of reserving another 15,360 bytes.
The explorer copies directly into its UI, removing a redundant 2,900-byte
snapshot. Startup USB RX is 16,384 rather than 32,768 bytes: the largest framed
packet is 15,459 bytes and USB remains stop-and-wait, with audio/batching confined
to Wi-Fi. TLS settings and pixel limits are unchanged. Streaming checks the
shared connection state without copying the entire network list each iteration.

The full portable ASan/UBSan suite passed, covering hold/release cancellation,
four-species rendering and fixed HUD bounds, radio metadata and gesture bounds,
uncertain/noisy motion guidance, network lifecycle and credential migration,
LZ4 external-workspace validation, and existing games/display/audio regressions.
Radio driver mocks also passed ThreadSanitizer. The exact BLE offset-three write
has an allocator regression test, and linked disassembly confirms the wrapper.
Updated pet, game and utility previews were rendered and visually inspected.

The first merged firmware compiled at **1,932,249 program bytes and 109,744 static
RAM bytes**. All three flashed regions passed digest verification; clock sync
succeeded at `2026-10-03T22:08:40+00:00`. Earlier pre-merge live probes verified
24 Wi-Fi entries, 24 BLE entries, detail gyro telemetry, three BLE exit/reentry
cycles, rapid scanner handoffs, screen-off/wake and clean heap checkpoints after
the SDK fixes. Those runs also exercised Tetris input and Fidget progression.
Final merged and shared-scratch measurements follow below.

Evidence: `artifacts/tests-v1.28.log`, `build-v1.28.log`, `flash-v1.28.log`,
`live-v1.28.log`, `ble-v1.28.log`, `handoff-v1.28.log`,
`ble-reset-v1.28.log`, and `ble-allocation-link-v1.28.txt`.


### Final shared-scratch build and replacement device

The final decoder/DMA change reuses one aligned allocation for JPEGDEC and the
panel stripe, recovering another approximately 15 KiB without changing decoder
output, stripe geometry, TLS, or transport limits. The object is constructed and
destroyed within each borrow; the board rejects use during presentation, its
input callbacks, or panel sleep. ASan/UBSan tests cover canaries, bad ranges,
alignment, input/output aliasing, arbitrary scratch overwrites, malformed entropy,
and repeated successful/failed decode followed by normal panel presentation.
The real companion transport test also passes with caller-owned JPEG scratch.
The full portable suite was rerun successfully after this change.

Final build: **1,932,825 program bytes; 91,856 static RAM bytes**. The full build
is only about 23% of the 8 MiB app allocation. The Mac companion is the existing
signed v1.6 bundle; it was not rebuilt or re-signed for this release.

The user substituted a second ESP32-C6 revision 0.2 / 16 MiB board. Its different
base MAC was checked before any write. All 16,777,216 original flash bytes were
saved to the root-level `feedforward-firmware`, verified against device flash,
and only then installed as the canonical backup. SHA-256 is
`d357302b41c18108338c7b6dd88d1170be5cdb9da2fb999d1e4176652e724282`;
the checksum file is `feedforward-firmware.sha256`. The raw backup is mode 0600
and Git-ignored. This is a complete image, including the original partition
table and persistent data, not merely the original app binary. The original
first device's backup under `backups/` remains separate.

All three Moss regions on the replacement passed digest verification, with
clock sync at `2026-10-03T22:25:50+00:00`. Fresh pet/settings/game records were
created on the replacement; the first device's pet history was not cloned.
The user explicitly approved copying the previously used Wi-Fi network only.
Credentials were entered through the shared network editor without printing
or writing them to a credential file; joining/saving succeeded, and Moss
created and paired a new device-specific identity. The latest build includes
all committed shared-network and isolated browser-experiment branch changes.

The replacement passed release-only Tetris movement, one-shot hold drop, pause
and resume, Wi-Fi/BLE discovery/details, gyro telemetry, three BLE lifecycles,
and heap-integrity checks. Its settings, score counts and joke history remained
unchanged by those probes. Earlier merged-board probes additionally verified
network-selector reconnect, scan sleep/wake, scanner/network ownership handoffs,
Fidget vertical sign and animation progression, and the 8 MiB storage report.

### Measured display performance

Before the final scratch change, the merged firmware connected with about
22,040 free heap bytes, then encountered a packet assembly timeout during a
large initial refresh. The final replacement stream completed all three video
workloads without NACKs or disconnects, generally reporting about 38,500 bytes (37.6 KiB) free.
The hardware was changed between these runs at the user's request; they are
not a controlled same-board A/B benchmark. The original RAM exhaustion and
USB fallback diagnosis also came from earlier live connection failures.

All workloads use the existing companion's 800×800 virtual desktop, 240×240
Fast pixels, adaptive compression, and the same 30 Hz synthetic drawing helper.
The first two seconds are excluded. Rates count completed changed updates,
not capture callbacks, unchanged frames, or the panel's nominal refresh rate.

| Workload | Duration | Changed updates/s | Median transfer time |
| --- | ---: | ---: | ---: |
| Scrolling, audio off | 25 s | 7.44 | 127.8 ms |
| Small moving marker, audio off | 20 s | 23.85 | 25.0 ms |
| Detailed image motion, audio off | 20 s | 6.28 | 150.4 ms |
| Scrolling, audio on | 25 s | 5.77 | 145.1 ms |

The latest historical audio-off baseline was 7.41/s scrolling and 5.83/s
image motion; an older small-motion run reached 25.68/s. Different times,
hardware and signal conditions prevent attributing small differences to code.
The final run's RSSI was about -52 dBm, versus about -33 dBm in the older
v1.4 benchmark. Audio cost roughly 22% of scrolling cadence in this run.

The audio benchmark uses a silent source and verifies real PCM counter growth
rather than assuming an enabled checkbox means audio traffic. The stream had
zero I2S write errors, but audio was not uninterrupted: underruns, overflow and
dropped-sample counters increased during load. Across the observed session,
current free heap samples stayed roughly 19–26 KiB. The reported lifetime low
of 436 bytes first appeared during startup and stayed unchanged; ESP-IDF sums
independent per-region historical minima, so this is not a measurement of
simultaneously available heap or evidence of a steady leak. Future memory
telemetry should sample simultaneous free bytes and largest allocatable block
at connection/audio/first-image boundaries before tuning driver buffers.
Audio preference was restored to off after measurement.

Evidence: `artifacts/backup-feedforward.log`,
`replacement-device-identification.log`, `flash-replacement-v1.28.log`,
`replacement-before-tests-v1.28.log`, `replacement-live-v1.28.log`,
`replacement-network-setup-v1.28.log`, `wifi-before-scratch-v1.28.log`,
`wifi-video-v1.28.log`, `wifi-summary-v1.28.txt`, `wifi-audio-v1.28.log`,
`wifi-audio-summary-v1.28.txt`, and the workload logs named in those summaries.


Maximum-payload USB tests on the final replacement firmware acknowledged every
packet and returned cleanly via RELEASE. Fast 240-pixel raw/noise updates stayed
about 1.82–1.85 full frames/s; compact flat RLE updates reached about 9.87–10.86/s.
Native 480-pixel noise timing was variable (0.22–0.32/s in the final two runs),
so no universal native-USB speed improvement is claimed. Evidence is
`artifacts/usb-final-v1.28.csv` and `usb-final-repeat-v1.28.csv`; the earlier
first-board comparison is `usb-benchmark-v1.28.csv`.

Final replacement Storage checks reported 16,777,216 flash bytes,
1,932,928 image bytes in the 8,388,608-byte app slot, and 122/4,032 occupied NVS
entries across 131,072 partition bytes. No microSD card was available for a live
mounted-filesystem check. Repeated refresh/back/reentry passed, then returned to
pet home (`artifacts/replacement-storage-v1.28.log`). Firmware and unchanged
companion archives are in `artifacts/releases/v1.28/` with verified SHA-256
checksums. The synthetic helper now supports silent audio and exits nonzero if
no virtual display exists; both its successful timed workload and failure path
were exercised without changing the signed companion.


## v1.29 — standalone browser — 2026-10-03

The browser build includes the v1.28 shared JPEG/DMA memory changes. It was
flashed only at app offset `0x10000` and verified by device-side digest; the
bootloader and partition table were unchanged. Pet state and the existing
saved Wi-Fi network restored successfully. A private pre-browser NVS backup
was retained locally. No network credentials were printed by the test helper.

- Direct verified HTTPS to `example.com`: HTTP 200, 577 HTML bytes, rendered by
  LHP on the ESP32-C6 without a companion. A 1,441-byte HTTP fixture rendered a
  1,339-pixel document; scrolling, relative links, Back, Reload, editor cancel
  and repaint, and chunked responses passed through actual device input paths.
- Oversized responses, non-HTML content, an untrusted TLS certificate, and failed
  DNS lookups were rejected with the expected error codes. The public expired-
  certificate endpoint timed out on the device and also failed from the Mac; a
  local self-signed HTTPS fixture supplied a reachable negative TLS test. The
  successful checks were retained and the suite continued without a reset or
  firmware change. Stop completed in 0.83 seconds; screen-off cancellation and
  wake passed.
- Ten HTTPS open/close cycles reclaimed the pet canvas each time. Browser →
  shared Wi-Fi picker → Browser → pet transitions passed without changing the
  saved network. The live suite and continuation completed 43 state checks with
  intact heap.
- The original exit failure was reproduced using genuine 32-bit Espressif TLSF
  code: a free 115,212-byte raw block advertised only 114,676 allocatable user
  bytes, causing a repeated 115,200-byte allocation to fail. Claiming the prefix
  and growing it with `realloc` restored the original canvas, with allocator
  integrity preserved. The production fix uses public heap APIs and never
  exposes a short allocation to rendering.
- Network initialization runs while the pet canvas remains protected. The
  renderer-only LHP build skips unused global GPIO ISR installation and UDP
  event-pipe sockets. The latter caused a persistent 104-byte main-task lwIP
  allocation inside the released canvas during testing. CSS/glyph arena chunks
  now count toward the renderer cap; 105 layout and 49 paint allocation-failure
  budgets pass under sanitizers. Parser logging uses a private sink. Device
  snapshots contain numeric state
  rather than URLs, page content, or passwords.
- Full host tests and the real-LHP engine suite passed under sanitizers. All
  114 staged firmware source files matched the worktree. During browser tests,
  ESP-IDF reported a lifetime heap minimum of 69,792 bytes; the lowest sampled
  free heap was 71,960 bytes. Smallest worker stack margin: 7,080/12,288 bytes.
- After browsing, Remote Display acknowledged every USB test packet across
  native/fast, raw/RLE, and partial-update cases, released cleanly, and returned
  to pet home. Browser then rendered HTTPS again and exited cleanly. Final pet
  free heap was 104,508 bytes; the reported lifetime minimum after USB was
  56,028 bytes. This was a functional regression check, not a speed comparison.

Build: 2,406,763/8,388,608 program bytes, 101,024/327,680 static RAM bytes;
image length 2,406,864 bytes. SHA-256:
`3342026acdb7911edc44c36b12e598ef93af2b06f13bccd16e8b953bcb73f189`.

Remaining limit: roughly 0.2 KiB of free heap is retained per shared Wi-Fi
connection cycle. A six-cycle Settings-only scan/connect/back test reproduced
it without browser, HTTP, TLS or worker creation; successful join/stop calls
match the pre-browser implementation. TCP state counts were zero after browser
requests, and tracked renderer allocations returned to zero. Free heap after
the ten browser exits fell from 107,088 to 104,980 bytes. A 142-second idle
check recovered some timer allocations but not the full difference. No bound
or plateau is claimed; this remains an unresolved Wi-Fi lifecycle issue.

Evidence is retained in Git-ignored `artifacts/browser-device/`: final flash and
verify logs, host suite log, `live-results.json`, `live-suite.log`,
`post-browser.log`, `usb-after-browser.csv`, and the exact allocator reproduction.
The v1.28 rollback image remains preserved. Actual on-panel typography and
physical touch feel have not received human confirmation. Modern JavaScript
sites, images, forms, and YouTube playback remain unsupported; see
[browser limits](browser.md).


## v1.30 — scrollable main menu — 2026-10-03

The main menu uses 40-pixel cards on a 47-pixel pitch, replacing the cramped
25-pixel cards. Three full entries fit at once; swiping reveals Utilities and
Settings. The navigation rail and Back to Pet remain fixed, with a scrollbar
and a swipe hint. Hardware Next keeps the complete focused row visible and
wraps to the top. Touch selection occurs only on release; dragging never also
launches an item. Existing submenu layouts remain unchanged.

- Full host regression suite passed under AddressSanitizer/UndefinedBehaviorSanitizer.
  Menu tests cover both scroll limits, every intermediate rendering offset,
  masked header/footer pixels, release-only taps, horizontal and return-to-origin
  drag rejection, stale-contact cancellation, focus reveal/wrap, and return state.
- Host-rendered top and bottom menu previews were visually inspected and updated.
  This is not a claim of human confirmation of physical touch feel.
- All 114 staged firmware files matched the worktree. The app alone was flashed
  at `0x10000`, followed by successful device digest verification. Pet state,
  saved Wi-Fi, bootloader and partitions were preserved.
- All 28 live device state checks passed with intact heap: swipe/clamp in both
  directions, drag suppression, rail Next, hardware focus scrolling/wrap,
  Browser launch only on release and verified HTTPS rendering, return from
  Browser/Settings/Wi-Fi, scrolled Utilities access, Games navigation, and the
  fixed Back to Pet control. The device was left on the main menu at the top.

Program storage: 2,408,393/8,388,608 bytes; static RAM: 101,056/327,680 bytes.
This adds 1,630 program bytes and 32 static RAM bytes over v1.29, with no new
framebuffer or dynamic menu allocation. App image length: 2,408,496 bytes;
SHA-256: `b968f9493cd4ab011deb29e262b78eab84466249493722c321a9d2a9f320e2ea`.

Evidence is retained in Git-ignored `artifacts/menu-scroll/`; the tested release
is in `artifacts/releases/v1.30/`, with v1.29 retained for rollback. The browser
compatibility and shared Wi-Fi heap-retention limitations documented under
v1.29 remain unchanged.

## v1.31 — touchscreen release handling — 2026-10-04

CST9220 release packets can omit their coordinates. The sensor driver now
returns the last valid contact position on release, including when the caller
starts each poll with zeroed outputs. This lets existing touch handlers accept
keyboard/button taps, address-bar editing and page dragging. Error/unavailable
readings remain distinct from release, and initialization clears the cache.

- Full host regression suite passed under AddressSanitizer/UndefinedBehaviorSanitizer.
  A native packet integration test exercises the production sensor driver and
  all 95 browser keys, both shared Wi-Fi keyboards, name/score keyboards,
  address-bar/toolbar taps, menu controls and page/keyboard drags. Both zero-count
  and stale-coordinate release formats are covered. The same test with the old
  driver fails at the address-bar tap.
- Live candidate testing exposed a separate canvas restoration failure. Temporary
  queue/lock tracing attributed a persistent 104-byte canvas-hole allocation to
  the SDK RSA/MPI mutex on the first TLS hardware operation. The production
  implementation initializes SHA/AES, MPI and ECC mutexes through their public SDK
  APIs while the canvas is protected. Temporary tracing was removed.
- Synthetic device swipes now arrive as a complete contact in one serial write;
  otherwise a physical sensor poll can release the injected contact mid-gesture.
- All 114 staged firmware files matched the final source. Program storage:
  2,408,341/8,388,608 bytes; static RAM: 101,064/327,680 bytes. Image length:
  2,408,448 bytes; SHA-256:
  `ca0528d1a7383f63dcef03896d2d35ff92fb3715b0986d522fc30776c2184705`.

The final production image was flashed at the existing `0x10000` app offset and
its device-side digest matched on 2026-10-04. All 19 live state checks passed:
address-bar taps, both touch-entered HTTP/HTTPS URLs, keyboard scrolling and
editing, page drags in both directions, links, Back, Reload, keyboard Cancel,
Wi-Fi picker entry/return, reload after Wi-Fi, and Exit with the full pet canvas
restored. These use USB-injected touch events through the real firmware handlers;
the host packet integration separately covers sensor decoding. Physical touch
feel has not received human confirmation. Saved network, pet state, bootloader
and partition layout were preserved.

Every live heap-integrity check passed. The lifetime heap minimum was 79,044
bytes; final free heap was 111,756 bytes. The fetch worker's smallest remaining
stack was 7,076 of 12,288 bytes, and the long-page fixture peaked at 91,761 tracked
LHP bytes after scrolling. These are fixture measurements, not a certification
of arbitrary websites or prolonged reconnect stability. Existing browser
compatibility and shared Wi-Fi retention limitations remain documented above.

Evidence is in Git-ignored `artifacts/touch-fix/`; the verified release is in
`artifacts/releases/v1.31/`, with v1.30 retained for rollback. The device was left
on the main menu with Browser visible.

## v1.32 — reusable touch keyboard — 2026-10-04

URL entry, Wi-Fi credentials (including Remote Display), pet names, and score
names now share one allocation-free keyboard controller and renderer. The final
layout has six columns and three visible rows: 18 character keys, each 34 × 28
logical pixels (68 × 56 on the panel). Editing and submit controls stay fixed.
Hardware Next/Select reveals the focused key; swipe gestures never type a key.
The 95-character keyboard scroll range is 390 logical pixels.

- Full host suite passed under AddressSanitizer/UndefinedBehaviorSanitizer before
  the final geometry adjustment. All affected suites passed again afterward:
  shared keyboard, settings, scores, Wi-Fi, Remote Display, browser UI/controller,
  and native CST9220 packet integration across all keyboard families. Coverage
  includes every printable ASCII character, capacity limits, masks, cancellation,
  hardware focus, mode switching, render bounds, and identical row/frame output.
- Firmware previews were inspected. The browser streams the same keyboard one
  row at a time; no framebuffer or heap allocation was added by the module.
- All 116 staged firmware files matched commit `e7892e6`. Program storage:
  2,408,989/8,388,608 bytes; static RAM: 101,144/327,680 bytes. This adds 648
  program bytes and 80 static RAM bytes over v1.31. Image length: 2,409,088
  bytes; SHA-256: `d300eb4800a2690942922dab01f4305a48d5ac6ab2ab7af740e39f88a6a5c6de`.
- The application at `0x10000` was flashed and its device digest matched. All
  19 browser checks passed, including complete touch-entered HTTP/HTTPS URLs,
  keyboard scrolling/editing, page drags, links, Back/Reload, Wi-Fi return, and
  Exit with the pet canvas restored. Another 11 editor checks passed for pet-name
  hardware entry, alphabet/numeric switching, Wi-Fi SSID/password touch and
  hardware entry, Clear, and cancellation. Existing names and networks were
  retained; test drafts were discarded.

Live tests use USB-injected touch/button events through production handlers.
Settings has no serial touch-injection route, so its touchscreen behavior is
covered by native sensor-packet integration; device Settings checks used the
hardware-button route. Physical touch feel still needs human confirmation.
All heap-integrity checks passed. Lifetime minimum: 76,804 bytes; final
free heap: 112,660 bytes; fetch stack remaining: 7,076 bytes; LHP peak:
91,761 bytes for the long-page fixture. Existing browser compatibility and
shared Wi-Fi reconnect-retention limitations remain unchanged.

Evidence: Git-ignored `artifacts/shared-keyboard/`. Verified release:
`artifacts/releases/v1.32/`; v1.31 remains available for rollback.

## v1.33 — more keyboard space and Reddit default — 2026-10-04

The shared keyboard now shows five rows of six keys (30 visible), preserving
the 34 × 28 logical-pixel key size. The large heading was removed; input and
count share a compact line. Delete/Clear/Done use one footer row. PWR and the
top Back control cancel edits, and Next skips the removed duplicate Cancel.
Score entry retains its distinct Skip action in the same single-row footer.
Pet-name alphabet entry fits without scrolling; the full ASCII range scrolls
330 logical pixels. The browser default is `https://reddit.com`.

- Full host suite passed with strict warnings, AddressSanitizer and
  UndefinedBehaviorSanitizer, including native sensor-packet integration, all
  keyboard profiles, footer bounds, cancellation, hardware focus, and explicit
  default-address/normalized-fetch assertions. Firmware previews were inspected.
- All 116 staged source files matched commit `4e18628`. Program storage:
  2,409,039/8,388,608 bytes; static RAM: 101,144/327,680 bytes. Image length:
  2,409,136 bytes; SHA-256:
  `4219cff5e76be62a7dc91704e0deeafc72ce91db12ee91be90ba5281772b56df`.
- Flashed the existing application partition at `0x10000`; device digest matched.
  19 browser and 11 editor checks passed through USB-injected touch/button events.
  Coverage includes URL typing, the new controls, top Back cancellation, HTTP
  and HTTPS, page drags/links, Wi-Fi handoff, canvas restoration, name numeric
  mode, and credential entry. Test drafts were discarded; saved settings and
  networks were retained. Settings touch uses native-packet host coverage because
  its device serial diagnostic route supports only hardware-button input.
- The default Reddit request returned HTTP 200, 8,397 bytes, and rendered
  a 441-pixel response. This verifies the returned document, not Reddit's
  complete interactive functionality; existing JavaScript/assets/forms limits apply.
- All heap-integrity checks passed. Lifetime minimum: 76,644 bytes; final
  free heap: 112,512 bytes; minimum fetch stack remaining: 4,988 bytes.
  Existing browser compatibility and reconnect-retention limitations remain.

Evidence: Git-ignored `artifacts/keyboard-space/`; verified release:
`artifacts/releases/v1.33/`, with v1.32 retained for rollback.

## v1.34 — standalone text browsing and reserved canvas — 2026-10-04

The reported closing hang was reproduced before updating: the browser had fully
closed (`active=0`, `engine_live=0`) while canvas reclamation remained pending.
The heap had 227,236 bytes free but its largest block was 108,532 bytes, too
small for the 115,200-byte pet canvas. The canvas now remains allocated and is
loaned to a private aligned arena. TLS, reader and LHP allocations are released
before returning that exact allocation to the menu; exit does not allocate it
again from the fragmented system heap.

Text is the default browser mode. HTML/Atom sources stream up to 512 KiB into a
46,736-byte retained document: 512 lines, 40 columns, 320 links and a 16 KiB URL
table. Parser scratch and TLS disappear before rendering. Text uses 20 native
pixels per line with blue underlined links; touch scrolling, link selection,
URL editing and hardware navigation remain available. TEXT/HTML toggles the
retained graphical experiment, whose separate 32 KiB limit still applies.

- Reddit's normal 8,397-byte response was a JavaScript loader with no readable
  body. Text mode now uses public Atom feeds. Live device checks loaded the front
  page (23,907 bytes, 132 lines, 48 links), r/AskReddit (14,630 bytes, 108 lines),
  and a tapped discussion (3,488 bytes, 33 lines, 5 links). A rapid first post
  request received HTTP 429; a later discussion succeeded after a quiet minute.
  The UI reports rate limits/refusals explicitly and does not retry around them.
- Hacker News loaded 34,745 bytes into 186 lines/196 links; a 61,105-byte
  discussion produced 365 lines/188 links. Both exceed the old graphical cap.
  The exact release image was subsequently smoke-tested against both front pages,
  including a Reddit drag and successful menu return.
- The full host suite passed under ASan/UBSan and strict warnings, followed by
  focused final reader/fetch/controller rechecks. New tests cover chunk boundaries,
  Atom ordering/escaping, malformed input, script/style removal, UTF-8 punctuation,
  link/URL limits, clipping, bounded row rendering, mode transitions and cleanup.
  The memory test runs 100 loans with reordered/fallback/reallocated/concurrent
  allocations and verifies return of the identical canvas. Real LHP engine and
  allocator tests passed; the graphical preview's missing shared-keyboard link
  dependency was also corrected.
- Final target build: 2,421,625 / 8,388,608 program bytes; 101,168 / 327,680 static
  RAM bytes (12,586 program bytes and 24 static bytes over v1.33). All 120 staged
  firmware files match `da6a140`. Application image: 2,421,728 bytes;
  SHA-256: `6e5fd46885b654fbbdf096d312a6e16b5367449d78a78ada02d152c3756d71b2`. Flashed only the application at `0x10000`; device digest matched.
- Final-image local checks passed: a 43,224-byte streamed source, drag scrolling,
  chunked transport, TEXT/HTML switching, editor cancellation/repaint, HTTP 429,
  rejection above 512 KiB, and closing a stalled request in
  0.73 seconds. Five repeated hardware Back exits took
  0.41–0.42 seconds each and left zero arena allocations. Exits succeeded with a
  largest general-heap block of only 44,020 bytes. All sampled heap integrity
  checks passed. Final free heap: 110,728 bytes; final boot's lifetime minimum:
  38,472 bytes. Shared Wi-Fi reconnect retention remains around 0.2 KiB
  per cycle; this is not a multi-hour stability certification.
- A read-only storage probe detected 127,999,672,320 physical bytes on the card,
  but mount returned FatFS `FR_NO_FILESYSTEM` (-13). SDK `FF_FS_EXFAT` is zero,
  consistent with an unsupported exFAT card but not proof of its format. No
  contents were modified or formatting performed; text browsing does not need it.
  The user's permission to format remains available for a future cache if needed.

Device actions used USB-injected touch/button events through production handlers;
physical finger feel was not human-confirmed. Public page contents/addresses and
credentials were excluded from device logs. Screenshots/previews are host renders.
Evidence: Git-ignored `artifacts/browser-reader/`. Verified release:
`artifacts/releases/v1.34/`; v1.33 is preserved for rollback.

## v1.35 — Hacker News, text redraws and FAT32 card — 2026-10-04

Hacker News is now the initial browser address. Text viewport painting changed
from eight individual rows per main-loop turn to a complete viewport using 25
transfers of eight logical rows. The old 25 loop turns imposed at least 125 ms
of deliberate loop delay, in addition to drawing and 200 transfers. Repeated
drag samples no longer restart an incomplete page; empty contacts, non-link taps
and clamped offsets do not dirty the controls. The reader/UI share a 3,840-byte
scratch buffer without a full browser framebuffer. Graphical LHP keeps its
separate incremental renderer.

- Final hardware paint measurements across 20 injected swipes: 49.55–50.56 ms,
  median 50.54 ms. These measure
  viewport painting, not end-to-end touch latency or a guaranteed display FPS.
  Idle produced no extra paint; cancelling URL editing repainted the same offset.
- The default live HTTPS page returned HTTP 200, 34,870 bytes,
  188 lines and 196 links. Four hardware Back exit checks,
  including after scanning SD, took 0.311–0.318 seconds and left no arena allocations
  or pending canvas reclamation. All sampled heap integrity checks passed.
- The 127,999,672,320-byte microSD had an exFAT signature at partition LBA 32768.
  With the user's format authorization, an isolated temporary maintenance build
  created MBR/FAT32 with two FAT copies and 32 KiB clusters in 161.527 seconds.
  A create/write/close/reopen/read/close/delete test succeeded. Conservative
  single-sector writes needed a longer busy timeout than the read-only probe.
  A full FAT recount at the maintenance clock exceeded its old 30-second limit.
- A subsequent normal-firmware scan reproduced `spi_hal_setup_trans` asserting
  that a SPI command was still running while LCD DMA and SD polling overlapped.
  Storage now paints Checking first, keeps exclusive bus ownership through the
  entire filesystem scan, and resumes display writes only after unmount and SD
  device removal. The scan deadline is 60 seconds; the read-only disk adapter
  still rejects all writes and trim. Host regressions require the completion
  callback to remain uncalled during mount/recount and run after device removal.
- Two final production scans succeeded, before and after browser use, in
  32.807 and 32.902 seconds. Final card data capacity:
  127,968,215,040 bytes; free: 127,968,182,272; error: zero.
  No maintenance formatting code remains in the release. Internal partition
  layout and saved settings were preserved. The final screen is Storage.
- Focused browser controller, UI, display and storage tests passed under strict
  warnings and ASan/UBSan. Final build: 2,421,867 / 8,388,608 program bytes and
  101,168 / 327,680 static RAM bytes. All 120 staged firmware files match
  source commit `3bfde56`. Image: 2,421,968 bytes;
  SHA-256: `d423af8ddc9d674b62b783e40a84a50146d04681deffcdc94331f79edc345239`. Only app offset `0x10000` was flashed; device digest matched.
  Final heap: 113,204 free, 32,256 lifetime minimum,
  53,236 largest block. Prior v1.34 release remains available.

Input checks used USB-injected button/touch events through production handlers;
physical finger feel was not human-confirmed. `docs/browser.md` records the
source-checked image/CSS feasibility assessment and staged experiment. Images,
external CSS and general modern-site support were not enabled by this change.
Evidence is in Git-ignored `artifacts/browser-v1.35/` and the verified release in
`artifacts/releases/v1.35/` in both the browser worktree and primary checkout.


## v1.36 — SD-backed images/CSS and browser confirmation — 2026-10-04

Source `dd4f200` adds two bounded external stylesheets and up to six JPEG/PNG
image attempts in graphical HTML mode. Downloads use the existing verified
transport, close TLS before decoding, and cache fitted RGBA rows in a private
32-slot FAT32 directory. A 64 KiB decoder budget and small row buffers avoid a
browser framebuffer. Text remains the default. The existing embedded CSS subset
continues to render; external styles are inserted in document order. Image links
use LHP's flow/hit-testing and PNG alpha is composited against the page.

Exit, hardware Back with no history, and manual Wi-Fi handoff now show Stay/Exit,
with Stay selected. Back dismisses the dialog. A load can finish beneath it;
Stay restores the page and resets the renderer watchdog after a user-held modal.
The loading screen displays 38 characters per line across up to three lines,
with an ellipsis for longer URLs. Host previews of the actual UI were inspected.

- Full `scripts/test.sh` passed, followed by focused final worker, UI, controller
  and shared-bus tests under ASan/UBSan. The real pinned LHP engine/arena/assets
  suite also passed under ASan/UBSan. Asset tests check JPEG and PNG pixels,
  alpha blending, linked images, 512×256 → 320×160 scaling, CSS insertion/escaping,
  cache hits, invalid/truncated cache entries, failed writes, absent SD,
  mid-decode cancellation, raw-text/comment filtering and cleanup. The recursive
  bus guard also passed 200,000 updates across two threads; existing display
  callback/reentry checks passed.
- A controlled device fixture loaded one external stylesheet and two images.
  Reload and a fresh browser session each reused all three entries. Eight swipe
  checks on the initial candidate and four on the final binary passed with
  stable retained page allocations after glyph warming. The final loaded page
  was 1072 native pixels high, with a 42,107-byte initial engine footprint;
  scrolling warmed glyphs to 64,339 bytes. This is not a claim of menu-speed
  graphical scrolling: LHP still paints incrementally.
- A second device fixture loaded public JPEG and PNG resources from verified
  HTTPS endpoints successfully (HTTP 200 page, two images, zero skipped). The
  fetch worker now has a 16 KiB stack and heap-allocated asset job storage;
  its measured minimum unused stack was 6,968 bytes on that HTTPS image load.
  The original 12 KiB candidate had only 1,824 bytes in another nested path,
  so it was replaced before this release.
- A deliberately truncated PNG was skipped; the same page retained its JPEG,
  stylesheet, links and normal Ready state. Cancellation during a delayed image
  response drained successfully. That exit check, including opening and choosing
  the confirmation and host diagnostic overhead, took 1.142 seconds.
- Both touch and hardware Next/Select/Back confirmation paths passed. Stay
  restored the rendered page after the dialog remained open for 17 seconds.
  Three final confirmed exits took 0.511–0.521 seconds from selecting Exit to
  observing completion, including host polling. All had zero browser arena and
  engine allocations and no pending canvas reclamation. All sampled heap
  integrity checks passed. Final browser-test heap: 111,636 free, 19,412 lifetime
  minimum, largest block 52,212 bytes. Small SDK/general-heap retention across
  Wi-Fi rejoins remains observable (200–232 bytes on the last cycles); these
  checks establish browser ownership cleanup, not a system-wide zero-leak claim.
- Live default Hacker News HTTPS text loads remained successful (about 35 KiB,
  188 lines and 196 links), including after graphical assets and cancellation.
- Final build: 2,448,541 / 8,388,608 program bytes; 101,224 / 327,680 static RAM
  bytes. Image: 2,448,640 bytes, 26,672 bytes larger than v1.35. SHA-256:
  `ae627358b655e622be27a1241831f30bb42d523baf461e7d96f3a1f0a7ffcc3b`.
  All 125 staged firmware files matched source `dd4f200`. Only application offset
  `0x10000` was flashed; the device digest matched. Settings, partition layout
  and the existing FAT32 card were preserved; no formatting was performed.

These device checks used USB-injected touch/button packets through production
handlers, not a human assessment of physical finger feel. Physical card removal
and power loss during writes were not induced; corresponding host failures and
truncated cache entries were checked. Unsupported images are skipped; JavaScript,
full modern CSS, and arbitrary modern-site compatibility remain outside this
experiment. Limits and lifecycle are documented in `docs/browser.md`.

Evidence is retained in Git-ignored `artifacts/browser-v1.36/`, and firmware,
checksums and validation metadata in `artifacts/releases/v1.36/`, in both the
browser worktree and primary checkout. v1.35 remains available for rollback.

A final read-only Storage scan after cache writes succeeded in 34.289 seconds: 127,968,215,040 data bytes, 393,216 used, 127,967,821,824 free; error zero. The browser had released the card before the diagnostic mounted it. The device was returned to the menu.


## v1.37 — Rounded-screen insets and reader images — 2026-10-04

Source `07ceb6b` moves browser controls clear of the rounded corners, leaves a
three-logical-pixel gutter on both sides, and shares the 480 × 352 native viewport
across rendering, scrolling and touch hit tests. Default **READ** mode now displays
bounded inline JPEG/PNG images through the existing SD cache; **HTML** retains
its experimental CSS layout. Responsive `srcset` selection avoids oversized
fallbacks. Indexed PNG1/2/4/8 with palette alpha is supported, and progressive
JPEG DC previews retain a useful, bounded display size.

Reader text and URL buffers grow with actual content instead of reserving about
46 KiB per page. A single open image file and a shared 4 KiB read-ahead window
replace retained per-image handles; this matters because the SDK embeds another
4 KiB buffer in every FatFS `FIL`. Reader cache entries store 160 × 160 logical
samples at most, while HTML retains up to 320 × 320 native samples. This removes
three quarters of reader image bytes without reducing its visible resolution.

- The final `scripts/test.sh` suite passed under ASan/UBSan. The actual pinned
  LHP engine, arena and image/CSS suite also passed under ASan/UBSan. New checks
  cover responsive and long image tags, palette bit packing/alpha/CRC rejection,
  small-page RAM use, image link coordinates, shared read-ahead, and at most two
  open files (only the download target remains open during asset fetch).
- Live standalone, verified HTTPS returned HTTP 200 for all three target pages:
  Hacker News (34,471 bytes, 181 lines, 194 links), portfolio test site A (8,565 bytes,
  12 images, zero skipped attempts), and portfolio test site B (468,656 bytes,
  8 images, 3 skipped attempts). The latter has 12 recorded blocks including a
  repeated source. Its full source fits the streaming reader but exceeds HTML
  mode's 32 KiB limit. These sites' redirects were followed normally.
- Production host previews from the same public source snapshots decoded 12 and
  9 images respectively. The device's eight-image result is the verified target
  result; some images remain unavailable on the constrained target. Neither the
  12-block limit nor missing JavaScript, video and full modern CSS is hidden.
- Nineteen final device checkpoints covered initial loads, eight image-page
  swipes plus Hacker News, Reload, confirmation/Stay and three confirmed exits.
  All 203 sampled heap-integrity checks passed. Measured reader paints were
  44.00–44.50 ms for Hacker News, 114.01–214.53 ms for the first personal site,
  and 140.51–199.53 ms for the second. These are viewport paint times, not total
  finger-to-display latency. Image pages remain slower than menus. Earlier
  full-resolution reader cache entries measured up to 662 ms on the first site.
- Initial personal-site loads took about 19.57 and 26.39 seconds after GO,
  including host polling. Reload again decoded all 12 images on the first site.
  These public responses did not qualify for persistent cache reuse (zero hits);
  current-page rendering still reads decoded pixels from SD. Eligible cache-hit
  reuse is covered by the production host suite and previous v1.36 device test.
- Confirmed exits took 1.144–1.153 seconds including opening the confirmation and
  host polling. Each left zero browser arena/engine allocations and no pending
  reclamation. The smallest measured unused fetch stack was 7,712 bytes of
  16 KiB. Final heap was 112,980 bytes free with a 53,236-byte largest block.
  The lowest sampled free heap was 24,564 bytes; the SDK aggregate per-region
  low-watermark was 2,340 bytes. The SDK explicitly warns that regional minima
  need not occur simultaneously, so this aggregate is not a measured global
  minimum. These checks do not establish arbitrary-site or system-wide
  leak-free operation; earlier shared Wi-Fi retention remains a known limit.
- Final build: 2,466,243 / 8,388,608 program bytes; 101,232 / 327,680 static RAM
  bytes. Binary: 2,466,352 bytes (17,712 more than v1.36). All 127 staged firmware
  files matched source `07ceb6b`. SHA-256:
  `9400246c0b3c621ea2f4173ed16c0f5223124bac04f004a06f624e29970db8c5`.
  Only application offset `0x10000` was flashed; the device digest matched.
  The existing FAT32 card, saved settings and partition layout were preserved.

Device input used USB-injected touch/button events through production handlers.
Physical corner fit and finger feel still need human confirmation; inspected
PNG previews are host renders. The device was left with the browser closed.
Evidence is retained in ignored `artifacts/browser-v1.37/`; the verified firmware
and metadata are in `artifacts/releases/v1.37/`. v1.36 remains available for rollback.


## v1.38 — Foreground memory, fast storage and display pacing — 2026-10-04

Source `9ce8d41` allocates browser painter state, Remote Display packet/queue
buffers, and radio discovery UI state only while needed. Main-loop reclamation
waits for worker completion. An exclusive foreground lease protects the shared
115,200-byte canvas at a stable address; it is reused rather than freed into a
fragmented heap. Read-only LHP CSS tables move to flash. Nonurgent saves and HUD
work wait during browser/display sessions. The pet catches up from its elapsed
timeline on return or action, rather than advancing every hidden frame.

- The complete `scripts/test.sh` suite passed, including ASan/UBSan lifecycle,
  buffer-credit, storage, Wi-Fi configuration and two-hour awake/sleeping pet
  catch-up tests across timer rollover. The pinned real LHP engine/arena/assets
  sanitizer suite passed all three tests. The signed macOS build and its complete
  transport suite passed, including authenticated audio-buffer feedback, bounded
  deferral, wrong-nonce rejection, JPEG decode quality and legacy behavior.
- Final target: 2,467,549 / 8,388,608 program bytes and 66,256 / 327,680 static
  RAM bytes. Static RAM is 34,976 bytes lower than v1.37. The production map
  confirms that Arduino station joins and both local Wi-Fi drivers use the
  eight-packet dynamic RX/TX cap. The 10 KiB Wi-Fi worker stack had 3,420 bytes
  unused at its observed minimum after the real TLS/streaming session.
- Storage reported 16,777,216 bytes of onboard flash and completed the 128 GB
  card check in **99 ms**, versus the preceding 34.289-second full FAT recount.
  Capacity was 127,999,672,320 physical bytes / 127,968,215,040 filesystem bytes.
  Used/free numbers are explicitly labeled cached estimates; this card's FSInfo
  reported 98,304 used bytes, less than the prior measured scan. Normal settings
  never scan the FAT; a missing count shows NOT COUNTED. Existing card contents,
  partitions and saved credentials were preserved. No reformat was necessary.
- With the browser closed before streaming, the final Wi-Fi display session
  remained connected through a 30-second photographic workload and 60-second
  scrolling workload. The Mac desktop was 800 × 800, transferred at 240 × 240
  with adaptive compression and audio enabled. Acknowledged changed-frame
  cadence was **6.11/s photo** and **5.30/s scrolling**; median sampled transfer
  times were 150.7 and 155.3 ms. These are completed-frame measurements, not
  capture FPS or end-to-end input latency. Host regression compilation also ran
  during this workload. v1.37's comparison session disconnected after about four
  seconds, so there is no meaningful sustained baseline FPS to compare.
- Audio uses a session-only 12 KiB ring, 240 ms prefill, capacity-based packet
  credit, smaller image batches/JPEGs, and optional queue feedback. Numeric audio
  counters showed zero overflows, dropped samples and I2S write errors. There
  were **four underruns over the whole session**: two before the photo workload,
  no additional underruns during its sampled window, and two during scrolling.
  Synthetic silent PCM exercised capture, transport and playback counters; this
  was not a human listening test. Playback is improved but is **not glitch-free**.
  Longer or less favorable Wi-Fi sessions remain unverified.
- After streaming, heap returned to 148,696 free bytes and all tracked app buffers
  were zero. Opening radio allocated 3,988 bytes of UI state; closing released it
  and restored the same free heap. Standalone verified HTTPS then rendered all
  12 images at portfolio test site A, followed by two successful Hacker News cycles.
  All 15 final checkpoints had intact heaps. Every checked exit left zero browser
  arena/engine allocations, display receive bytes, Wi-Fi queues and radio UI
  bytes, with the canvas returned to UI ownership. Final home had 147,604 free
  bytes and a 36,852-byte largest block. Small shared Wi-Fi/SDK retention remains
  (roughly 232–240 bytes per subsequent HN join); this is not a system-wide
  leak-free claim. Lowest sampled audio heap during the workload was 11,264
  bytes. The SDK aggregate regional low-watermark was 908 bytes; regional minima
  need not be simultaneous and do not measure a global instantaneous minimum.
- Moss Display was installed and launched at the established primary-checkout
  `build/macos/Moss Display.app` path, not from the worktree. Its strict signature
  verification passed with Developer ID Application: the configured signing identity
  and the same Apple/Team-based designated requirement. Capture worked there.
  No TCC reset or privacy database modification was performed. Future worktree
  builds must be copied to that stable launch location after testing.
- All 129 staged firmware source files matched this commit. Application binary:
  2,467,648 bytes, SHA-256
  `40a37d308f5f52359ecf0f55f88123be93494aaf7ff5e4865049f967e85c6aeb`.
  Only application offset `0x10000` was flashed; on-device digest verification
  passed. Device input used USB-injected events through production handlers.
  The device was left at home with the browser and Remote Display closed.

Numeric evidence and test logs are retained in ignored `artifacts/runtime-v1.38/`;
firmware, checksums and validation metadata are in `artifacts/releases/v1.38/`,
in the browser worktree and primary checkout. v1.37 remains available for rollback.
