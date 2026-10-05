#!/usr/bin/env python3
"""Set Moss's battery-backed RTC from this computer's UTC clock over USB."""
import argparse
import datetime
import re
import time

import serial


def sync_clock(port_name: str, timeout: float = 20) -> int:
    deadline = time.monotonic() + timeout
    while True:
        try:
            device = serial.Serial(port_name, 115200, timeout=0.2)
            break
        except serial.SerialException:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.25)
    with device:
        # Wait for the app, including its display/touch initialization. Querying
        # the clock is read-only and works if the boot banner was already missed.
        next_query = 0.0
        while time.monotonic() < deadline:
            if time.monotonic() >= next_query:
                device.write(b"t\n")
                next_query = time.monotonic() + 1
            line = device.readline().decode("utf-8", errors="replace").strip()
            if line.startswith("CLOCK status=") or line.startswith("READY:"):
                break
        else:
            raise RuntimeError("Moss did not respond to the clock query; v1.2 or later is required")

        utc = int(time.time())
        if not 946684800 <= utc < 4102444800:
            raise RuntimeError("Computer clock must be within 2000–2099")
        device.write(f"T{utc}\n".encode("ascii"))
        acknowledged = False
        while time.monotonic() < deadline:
            line = device.readline().decode("utf-8", errors="replace").strip()
            if line.startswith("CLOCK error:"):
                raise RuntimeError(line)
            if line == f"CLOCK set ok utc={utc}":
                acknowledged = True
                device.write(b"t\n")
            if acknowledged:
                match = re.match(r"CLOCK status=0 utc=(\d+) accounted_utc=\d+ sync_needed=0$", line)
                if match:
                    actual = int(match.group(1))
                    if abs(actual - int(time.time())) > 3:
                        raise RuntimeError("RTC readback does not match the computer's UTC clock")
                    timestamp = datetime.datetime.fromtimestamp(actual, datetime.timezone.utc)
                    print(f"Moss clock synchronized: {timestamp.isoformat()}")
                    return actual
        raise RuntimeError("Clock synchronization or RTC readback timed out")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    args = parser.parse_args()
    sync_clock(args.port)
