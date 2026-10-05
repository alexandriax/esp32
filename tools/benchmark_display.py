#!/usr/bin/env python3
"""Synthetic USB benchmark; quit Moss Display and choose Remote Display → Use USB first.

Uses no desktop capture or network. Requires firmware v1.9. Each pattern
temporarily fills the device screen, then returns to the Remote Display connection page.
"""
import argparse
import random
import struct
import time
import zlib

import serial


def cobs(data):
    result = bytearray([0])
    anchor, code = 0, 1
    for value in data:
        if value == 0:
            result[anchor] = code
            anchor = len(result)
            result.append(0)
            code = 1
        else:
            result.append(value)
            code += 1
            if code == 255:
                result[anchor] = code
                anchor = len(result)
                result.append(0)
                code = 1
    result[anchor] = code
    return bytes(result)


def packet(kind, nonce, sequence=0, y=0, width=0, height=0, payload=b""):
    raw = struct.pack("<4sBBHQIHHHHHH", b"MOSD", 1, kind, 0, nonce, sequence,
                      0, y, width, height, len(payload), 0) + payload
    return b"\0" + cobs(raw + struct.pack("<I", zlib.crc32(raw))) + b"\0"


def rle(pixels):
    output = bytearray()
    values = struct.unpack("<" + "H" * (len(pixels) // 2), pixels)
    previous, count = values[0], 0
    for value in values:
        if value == previous and count < 65535:
            count += 1
        else:
            output += struct.pack("<HH", count, previous)
            previous, count = value, 1
    output += struct.pack("<HH", count, previous)
    return bytes(output)


def desktop(size):
    # Deliberately synthetic flat-color UI with a sidebar, title, and text bars.
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            color = 0x1082 if x < size // 5 else 0xEF7D
            if y < size // 10:
                color = 0x232A
            elif x > size // 4 and y % (size // 20) < size // 100 and x < size * 4 // 5:
                color = 0x4208
            pixels += struct.pack("<H", color)
    return bytes(pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    args = parser.parse_args()
    rng = random.Random(19)
    cases = []
    for size in (480, 240):
        flat = desktop(size)
        noise = bytes(rng.getrandbits(8) for _ in range(size * size * 2))
        cases.extend([(f"flat-{size}-raw", size, flat, False),
                      (f"flat-{size}-rle", size, flat, True),
                      (f"noise-{size}-adaptive", size, noise, True)])
    with serial.Serial(args.port, 115200, timeout=.1) as device:
        def wait(prefix, timeout=5):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                line = device.readline().decode("ascii", "replace").strip()
                if line.startswith("DISPLAY NACK"):
                    raise RuntimeError(line)
                if line.startswith(prefix):
                    return line
            raise RuntimeError(f"Timed out waiting for {prefix}; choose Remote Display → Use USB first")

        device.write(b"?")
        nonce = int(wait("DISPLAY REQUEST").split()[2], 16)
        caps = wait(f"DISPLAY CAPS {nonce:016x}")
        if int(caps.split()[3]) & 3 != 3:
            raise RuntimeError("Firmware v1.9 capabilities required")
        sequence = 0
        try:
            device.write(packet(1, nonce, payload=struct.pack("<HHHBB", 480, 480, 15360, 1, 0)))
            wait(f"DISPLAY READY {nonce:016x} 0")
            print("case,wire_bytes,seconds,equivalent_full_frames_per_second", flush=True)
            for name, size, pixels, compress in cases:
                height = 16 if size == 480 else 8
                packets = []
                for y in range(0, size, height):
                    raw = pixels[y * size * 2:(y + height) * size * 2]
                    encoded = rle(raw) if compress else raw
                    use_rle = compress and len(encoded) < len(raw)
                    kind = (7 if use_rle else 2) if size == 480 else (9 if use_rle else 8)
                    sequence += 1
                    packets.append((sequence, packet(kind, nonce, sequence, y, size, height,
                                                       encoded if use_rle else raw)))
                start = time.monotonic()
                for seq, data in packets:
                    device.write(data)
                    wait(f"DISPLAY ACK {nonce:016x} {seq}")
                elapsed = time.monotonic() - start
                print(f"{name},{sum(len(data) for _, data in packets)},{elapsed:.4f},{1/elapsed:.2f}", flush=True)
            for name, size, height in (("changed-two-native-rows", 480, 2), ("changed-one-fast-row", 240, 1)):
                sequence += 1
                data = packet(2 if size == 480 else 8, nonce, sequence, 100, size, height,
                              desktop(size)[:size * height * 2])
                start = time.monotonic()
                device.write(data)
                wait(f"DISPLAY ACK {nonce:016x} {sequence}")
                print(f"{name},{len(data)},{time.monotonic()-start:.4f},partial-update", flush=True)
        finally:
            # RELEASE is sequence-independent, so failures also return safely.
            device.write(packet(5, nonce, sequence + 2))
            wait(f"DISPLAY RELEASED {nonce:016x}")
        device.write(b"s")
        until = time.monotonic() + .5
        while time.monotonic() < until:
            line = device.readline().decode("ascii", "replace").strip()
            if line:
                print("# " + line, flush=True)


if __name__ == "__main__":
    main()
