# Wi-Fi Networks

Wi-Fi Networks appears in the main menu immediately before Utilities. It is the
shared connection setup screen for the device and Remote Display. Remote
Display's network button opens this same screen; after a successful connection
it resumes Remote Display using the saved network.

## Device flow

- Open Wi-Fi Networks to scan nearby 2.4 GHz networks. Select an open network to
  connect, or enter the password for a supported personal network. Unsupported
  enterprise/legacy authentication is shown but cannot be selected.
- The saved network appears as a reconnect choice, even when absent from a scan.
  Only one network is remembered, matching the existing display-mode behavior.
  A new network replaces it only after a successful connection and storage write.
- Use Manual for a hidden SSID or to change the saved network's password. The
  existing case-sensitive ASCII keyboard is shared with the old display UI;
  password characters stay masked and temporary field drafts are erased on exit.
- Scan Again refreshes the list. Forget asks for confirmation, then removes the
  remembered network without deleting the Mac display pairing identity.
- PWR/Back returns to the previous screen. Leaving setup or putting the screen
  to sleep releases its radio; remembered credentials remain available for the
  next feature that needs them. There is no automatic network connection at boot.

Connected means association and an IP address. It does not establish public
Internet access, captive-portal completion, DNS/HTTPS reachability, or browser
compatibility. Enterprise EAP setup and captive-portal login are not implemented.
The isolated LHP renderer remains offline until its hardware memory gate passes.

## Shared implementation

`wifi_networks` owns station connection, deadlines, scan results, connection
status, cancellation and radio cleanup. Its pure controller uses a platform
driver and credential store, making failure cases testable without hardware.
There is no extra networking worker stack: the main loop ticks setup; the existing
Remote Display worker ticks its own connection while it owns the service.

`WifiNetworksUi` provides the network list, status, manual setup and forget
confirmation. `WifiNetworkEditor` owns the keyboard/field validation/masking used
by both the shared selector and the backward-compatible RemoteUi wrapper. The
firmware routes Remote Display setup through the selector instead of maintaining
another setup flow.

The application serializes setup, Remote Display and radio-explorer ownership.
It waits for the previous worker to finish before starting another radio user.
The radio explorers remain observational utilities; selecting an AP for a device
connection belongs to Wi-Fi Networks.

## Persistence and migration

The versioned, CRC-checked `moss-wifi/network` NVS blob is the canonical network
record. A valid legacy `moss-wifi/profile` supplies credentials only when no
canonical record exists. Successful connection writes the canonical record;
subsequent connections use it. Forget writes a durable empty marker so the legacy
profile cannot restore a forgotten network after reboot.

The legacy profile retains the display certificate, token and device identity.
Network setup does not generate TLS keys or expose a display server. First Remote
Display use after network-only setup creates its own pairing identity through
the existing provisioning path. Existing paired devices keep their identity.
NVS replacement is one blob transaction; errors never fall back from a damaged
canonical record to stale credentials. As before, this firmware does not enable
flash encryption; the checksum detects corruption, not tampering.

## Bounds and verification

The selector retains at most twelve AP entries and deduplicates matching network
names/security. Scan/connection deadlines and teardown cover cancel, errors and
sleep. The driver releases the SDK scan list and unregisters its handler before
another mode owns Wi-Fi. The SDK itself may allocate more scan memory than the
retained twelve-entry application list; hardware measurements remain necessary.

Host checks cover controller deadlines/replacement/cleanup, save-after-success,
legacy migration and forget behavior, menu order, keyboard reuse, password
masking and UI bounds. Run `scripts/test.sh` for the repository suite. Firmware
compilation checks the real ESP32 APIs. Physical scanning, AP interoperability,
radio handoffs and device heap/latency need a later hardware pass; this change is
prepared without flashing the device.

## Prepared build (2026-10-03)

The complete host suite passed, including ASan/UBSan UI, storage, controller and
actual ESP32 adapter tests with mocked SDK calls. The final worktree C6 compile
passed with all warnings enabled: 1,601,307 program bytes and 116,116 static RAM
bytes, against committed base `bd71422`. These are linker figures, not runtime
heap measurements or the footprint of the other thread's unfinished application.
Local evidence is retained under ignored `build/wifi-networks-review/`.

This branch is deliberately separate from the primary checkout's unfinished
radio/application integration. Before integrating and eventually flashing, merge
that completed work and recheck the application handoffs: opening setup must
close the radio-explorer UI, and starting a radio explorer must close setup and
wait for both the display worker and shared station service to release ownership.
The prepared setup itself already stops/waits for a previous raw scanner and
display worker. Rebuild the merged application and measure its peak/largest-block
heap across scan, join, TLS setup, cancel, sleep/wake and return to the pet.
