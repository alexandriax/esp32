#!/usr/bin/env python3
"""Install the separate Doom app and WHD without rewriting Moss or saved NVS.

Usage: scripts/flash-doom.py /dev/cu.usbmodem101 --app doom.bin --whd doom1.whd
Run scripts/build.sh first so build/sloth_pet.ino.partitions.bin matches the
checked-in flash layout. A fresh, verified full-device backup precedes writes.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
LAYOUT = ROOT / "firmware/sloth_pet/partitions.csv"
TABLE_IMAGE = ROOT / "build/sloth_pet.ino.partitions.bin"
PART_TOOL = ROOT / ".tools/arduino-data/packages/esp32/hardware/esp32/3.3.0/tools/gen_esp32part.py"
ESPTOOL = ROOT / ".tools/venv/bin/esptool.py"
FLASH_BYTES = 0x1000000
TABLE_OFFSET = 0x8000
TABLE_BYTES = 0xC00
FACTORY_OFFSET, FACTORY_BYTES = 0x10000, 0x600000
DOOM_OFFSET, DOOM_BYTES = 0x610000, 0x400000
WAD_OFFSET, WAD_BYTES = 0xA10000, 0x400000
OTADATA_OFFSET, OTADATA_BYTES = 0xE30000, 0x2000

# The original 8 MiB factory map and intermediate 8+2 map are accepted for
# migration. The new map is accepted so a later Doom build can be installed without
# clearing savegames. Each install resets OTA selection to Moss; the new image
# receives a fresh rollback-checked launch from the game menu.
ORIGINAL_PARTITIONS = (
    ("factory", 0x00, 0x00, 0x10000, 0x800000),
    ("nvs", 0x01, 0x02, 0xFE0000, 0x10000),
    ("pet_nvs", 0x01, 0x02, 0xFF0000, 0x10000),
)
INTERMEDIATE_PARTITIONS = (
    ("factory", 0x00, 0x00, 0x10000, 0x800000),
    ("doom", 0x00, 0x10, 0x810000, 0x200000),
    ("wad", 0x01, 0x40, WAD_OFFSET, WAD_BYTES),
    ("saves", 0x01, 0x41, 0xE10000, 0x20000),
    ("otadata", 0x01, 0x00, OTADATA_OFFSET, OTADATA_BYTES),
    ("nvs", 0x01, 0x02, 0xFE0000, 0x10000),
    ("pet_nvs", 0x01, 0x02, 0xFF0000, 0x10000),
)
NEW_PARTITIONS = (
    ("factory", 0x00, 0x00, FACTORY_OFFSET, FACTORY_BYTES),
    ("doom", 0x00, 0x10, DOOM_OFFSET, DOOM_BYTES),
    ("wad", 0x01, 0x40, WAD_OFFSET, WAD_BYTES),
    ("saves", 0x01, 0x41, 0xE10000, 0x20000),
    ("otadata", 0x01, 0x00, OTADATA_OFFSET, OTADATA_BYTES),
    ("nvs", 0x01, 0x02, 0xFE0000, 0x10000),
    ("pet_nvs", 0x01, 0x02, 0xFF0000, 0x10000),
)


def fail(message: str) -> None:
    raise ValueError(message)


def partitions(data: bytes) -> tuple[tuple[str, int, int, int, int], ...]:
    if len(data) != TABLE_BYTES:
        fail("partition table must be exactly 0xC00 bytes")
    found = []
    for index in range(0, len(data), 32):
        magic, kind, subtype, offset, size, raw_name, flags = struct.unpack_from(
            "<HBBII16sI", data, index
        )
        if magic == 0xEBEB:  # IDF partition-table MD5 record.
            break
        if magic == 0xFFFF:
            break
        if magic != 0x50AA or flags:
            fail("unrecognized or encrypted partition table entry")
        try:
            name = raw_name.split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as exc:
            raise ValueError("non-ASCII partition label") from exc
        found.append((name, kind, subtype, offset, size))
    return tuple(found)


def validate_files(app: Path, whd: Path, table: Path) -> tuple[int, int]:
    for path in (app, whd, table, LAYOUT, PART_TOOL, ESPTOOL):
        if not path.is_file():
            fail(f"missing {path}")
    app_size = app.stat().st_size
    if not 0x70 <= app_size <= DOOM_BYTES:
        fail(f"Doom app size {app_size} exceeds its 4 MiB slot or is empty")
    with app.open("rb") as source:
        header = source.read(0x70)
    if header[0] != 0xE9 or struct.unpack_from("<H", header, 12)[0] != 13:
        fail("Doom app is not an ESP32-C6 image")
    # esp_app_desc_t starts after the 24-byte image header and 8-byte first
    # segment header. Its project_name prevents installing a different C6 app
    # (including Moss itself) into the Doom slot by accident.
    if struct.unpack_from("<I", header, 0x20)[0] != 0xABCD5432 or \
            header[0x50:0x70].split(b"\0", 1)[0] != b"diablito":
        fail("app image is not the diablito Doom project")
    subprocess.run([str(ESPTOOL), "--chip", "esp32c6", "image_info", str(app)],
                   check=True, stdout=subprocess.DEVNULL)

    whd_size = whd.stat().st_size
    if not 16 < whd_size <= WAD_BYTES:
        fail(f"WHD size {whd_size} exceeds its 4 MiB slot or is empty")
    with whd.open("rb") as source:
        magic, lumps, directory, declared_size = struct.unpack("<4sIII", source.read(16))
    if magic != b"IWHD" or not lumps or directory != 36 or \
            directory + (lumps + 1) * 4 > whd_size:
        fail("WHD header is invalid (expected the Doom IWHD format)")
    if declared_size != whd_size:
        fail(f"WHD declares {declared_size} bytes, but file has {whd_size}")

    with tempfile.TemporaryDirectory(prefix="ffhware-partitions-") as directory:
        generated = Path(directory) / "partitions.bin"
        subprocess.run([sys.executable, str(PART_TOOL), "--flash-size", "16MB",
                        str(LAYOUT), str(generated)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        expected = generated.read_bytes()
    if partitions(expected) != NEW_PARTITIONS:
        fail("checked-in Doom partition layout does not match the expected addresses")
    if table.read_bytes() != expected:
        fail("partition image is stale; run scripts/build.sh before installing Doom")
    return app_size, whd_size


def validate_factory_image(backup: Path) -> int:
    """Verify the installed Moss image fits its smaller 6 MiB reservation."""
    with backup.open("rb") as source:
        source.seek(FACTORY_OFFSET)
        header = source.read(24)
        if len(header) != 24 or header[0] != 0xE9 or not 0 < header[1] <= 16 or \
                struct.unpack_from("<H", header, 12)[0] != 13:
            fail("existing factory app is not a valid ESP32-C6 image")
        length = 24
        for _ in range(header[1]):
            source.seek(FACTORY_OFFSET + length)
            segment = source.read(8)
            if len(segment) != 8:
                fail("existing factory image has a truncated segment header")
            _, segment_bytes = struct.unpack("<II", segment)
            length += 8 + segment_bytes
            if length > FACTORY_BYTES:
                fail("existing factory image extends into the new Doom partition")
        length += (15 - length % 16) + 1  # checksum byte ends a 16-byte block
        if header[23]:
            length += 32  # appended SHA-256 digest
        if length > FACTORY_BYTES:
            fail("existing factory image exceeds its new 6 MiB partition")
        source.seek(FACTORY_OFFSET)
        with tempfile.TemporaryDirectory(prefix="ffhware-factory-check-") as directory:
            image = Path(directory) / "factory.bin"
            with image.open("wb") as target:
                remaining = length
                while remaining:
                    chunk = source.read(min(65536, remaining))
                    if not chunk:
                        fail("full-flash backup ends inside the factory image")
                    target.write(chunk)
                    remaining -= len(chunk)
            subprocess.run([str(ESPTOOL), "--chip", "esp32c6", "image_info", str(image)],
                           check=True, stdout=subprocess.DEVNULL)
    return length


def command(port: str, *args: str, before: str = "no_reset", after: str = "no_reset") -> None:
    subprocess.run([str(ESPTOOL), "--chip", "esp32c6", "--port", port,
                    "--baud", "921600", "--before", before, "--after", after,
                    *args], check=True, cwd=ROOT)


def make_backup(port: str) -> Path:
    backup_dir = ROOT / "backups"
    backup_dir.mkdir(exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    destination = backup_dir / f"pre-doom-{timestamp}-esp32c6-16mb.bin"
    if destination.exists():
        fail(f"backup already exists: {destination}; wait a second and retry")
    partial = destination.with_suffix(".partial")
    if partial.exists():
        fail(f"incomplete backup exists: {partial}; inspect it before retrying")
    try:
        command(port, "read_flash", "0", hex(FLASH_BYTES), str(partial))
        if partial.stat().st_size != FLASH_BYTES:
            fail("full-flash backup has the wrong length")
        command(port, "verify_flash", "0", str(partial))
        with partial.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        partial.rename(destination)
        destination.with_suffix(".sha256").write_text(
            f"{digest}  {destination.relative_to(ROOT)}\n", encoding="ascii"
        )
    except Exception:
        partial.unlink(missing_ok=True)
        raise
    print(f"Verified 16 MiB backup: {destination}", flush=True)
    return destination


def install(port: str, app: Path, whd: Path, table: Path) -> None:
    # Confirm the attached target and its current factory/NVS positions before
    # touching any flash sector. Read only the partition table at this stage.
    command(port, "flash_id", before="default_reset")
    with tempfile.TemporaryDirectory(prefix="ffhware-current-table-") as directory:
        current_path = Path(directory) / "partitions.bin"
        command(port, "read_flash", hex(TABLE_OFFSET), hex(TABLE_BYTES), str(current_path))
        current = partitions(current_path.read_bytes())
    if current not in (ORIGINAL_PARTITIONS, INTERMEDIATE_PARTITIONS, NEW_PARTITIONS):
        fail("device partition table is not a known Moss/Doom layout")
    backup = make_backup(port)
    factory_length = validate_factory_image(backup)
    print(f"Existing factory app verified at {factory_length} bytes; it fits 6 MiB")

    # Clear an older VALID selection before replacing any part of its image.
    # An interrupted update must boot the untouched factory app, never a
    # partially written replacement Doom image. This sector is unallocated in
    # the old map. Savegames and both NVS regions remain untouched.
    command(port, "erase_region", hex(OTADATA_OFFSET), hex(OTADATA_BYTES))

    # Write data first, app second, partition table last. A failure before the
    # final step leaves the existing factory app bootable.
    for offset, path in ((WAD_OFFSET, whd), (DOOM_OFFSET, app)):
        command(port, "write_flash", hex(offset), str(path))
        command(port, "verify_flash", hex(offset), str(path))
    command(port, "write_flash", hex(TABLE_OFFSET), str(table))
    command(port, "verify_flash", hex(TABLE_OFFSET), str(table),
            before="usb_reset", after="hard_reset")
    print("Doom image, WHD, and partition table verified; next boot is Moss. Factory app, savegames, and both NVS partitions were not written.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="ESP32-C6 serial port; omit with --check-only")
    parser.add_argument("--app", type=Path, required=True, help="built ESP32-C6 Doom app image")
    parser.add_argument("--whd", type=Path, required=True, help="Doom WHD game-data image")
    parser.add_argument("--partition-image", type=Path, default=TABLE_IMAGE)
    parser.add_argument("--check-only", action="store_true", help="validate images without opening a device")
    args = parser.parse_args()
    try:
        app, whd, table = (path.resolve() for path in (args.app, args.whd, args.partition_image))
        app_size, whd_size = validate_files(app, whd, table)
        print(f"Validated ESP32-C6 Doom app ({app_size} bytes), WHD ({whd_size} bytes), and partition image")
        if args.check_only:
            return 0
        if not args.port:
            parser.error("port is required unless --check-only is used")
        install(args.port, app, whd, table)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Doom install aborted: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
