#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
port=${1:?Usage: scripts/flash.sh /dev/cu.usbmodem101}
esptool=.tools/venv/bin/esptool.py
backup=backups/original-esp32c6-16mb.bin
for file in sloth_pet.ino.bootloader.bin sloth_pet.ino.partitions.bin sloth_pet.ino.bin; do
  test -f "build/$file" || { echo 'Build firmware first: scripts/build.sh' >&2; exit 1; }
done
mkdir -p backups artifacts
"$esptool" --chip esp32c6 --port "$port" --after no_reset flash_id
if [ ! -f "$backup" ]; then
  # A failed/incomplete read never becomes the canonical restore image.
  "$esptool" --chip esp32c6 --port "$port" --baud 921600 --before no_reset --after no_reset \
    read_flash 0 0x1000000 "$backup.partial"
  "$esptool" --chip esp32c6 --port "$port" --baud 921600 --before no_reset --after no_reset \
    verify_flash 0 "$backup.partial"
  mv "$backup.partial" "$backup"
  shasum -a 256 "$backup" > backups/SHA256SUMS
fi
test "$(wc -c < "$backup" | tr -d ' ')" = 16777216
shasum -a 256 -c backups/SHA256SUMS
# No erase_flash: write only bootloader, partition table, and app sectors.
# Keep the ROM loader active until readback verification completes. Resetting
# between these steps makes native USB temporarily disappear on macOS.
"$esptool" --chip esp32c6 --port "$port" --baud 921600 --before no_reset --after no_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/sloth_pet.ino.bootloader.bin \
  0x8000 build/sloth_pet.ino.partitions.bin \
  0x10000 build/sloth_pet.ino.bin
# On macOS, opening a fresh serial connection asserts DTR. A no_reset verify
# would leave that boot strap asserted when RTS resets the chip, parking it in
# the ROM downloader (the AMOLED retains its last frame). usb_reset explicitly
# releases DTR/RTS so the final hard reset starts the application normally.
"$esptool" --chip esp32c6 --port "$port" --baud 921600 --before usb_reset --after hard_reset verify_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/sloth_pet.ino.bootloader.bin \
  0x8000 build/sloth_pet.ino.partitions.bin \
  0x10000 build/sloth_pet.ino.bin
# Each build leaves the battery-backed RTC running. Synchronize from the host
# after the normal boot/catch-up so installs never substitute compile time.
.tools/venv/bin/python scripts/sync_clock.py "$port"
