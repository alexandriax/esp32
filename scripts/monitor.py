#!/usr/bin/env python3
"""USB monitor with bounded duration, commands, and pairing-secret redaction."""
import argparse
import time


class LineRedactor:
    """Never release a serial line until its complete contents are classified."""

    MARKERS = (b"DISPLAY PAIRING", b"DISPLAY WIFI_READY")
    OMITTED = "[USB pairing data omitted]\n"
    MAX_LINE_BYTES = 4096
    _TAIL_BYTES = max(map(len, MARKERS)) - 1

    def __init__(self, discard_initial_line=False):
        self._initial = discard_initial_line
        self._reset()

    def _reset(self):
        self._pending = bytearray()
        self._tail = b""
        self._sensitive = False
        self._oversized = False
        self._has_bytes = False

    def _append(self, fragment):
        if not fragment:
            return
        self._has_bytes = True
        scan = self._tail + fragment
        self._sensitive |= any(marker in scan for marker in self.MARKERS)
        self._tail = scan[-self._TAIL_BYTES:]
        remaining = self.MAX_LINE_BYTES - len(self._pending)
        self._pending.extend(fragment[:remaining])
        self._oversized |= len(fragment) > remaining

    def _finish_line(self, complete):
        if self._sensitive:
            result = self.OMITTED
        elif self._initial:
            # A newly opened USB stream can begin in the middle of a token.
            result = "[initial serial line omitted]\n"
        elif self._oversized:
            result = "[oversized serial line omitted]\n"
        elif not complete:
            # Do not print a prefix which could become a sensitive line after
            # the deadline, a disconnect, or a KeyboardInterrupt.
            result = "[incomplete serial line omitted]\n"
        else:
            result = self._pending.decode("utf-8", errors="replace") + "\n"
        self._initial = False
        self._reset()
        return result

    def feed(self, data):
        output = []
        parts = data.split(b"\n")
        for index, fragment in enumerate(parts):
            self._append(fragment)
            if index < len(parts) - 1:
                output.append(self._finish_line(complete=True))
        return "".join(output)

    def finish(self):
        """Safely finish a partial final line; repeated calls produce nothing."""
        return self._finish_line(complete=False) if self._has_bytes else ""


def main():
    # Unit tests can import the redactor without pyserial or a USB connection.
    import serial

    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--send", default="s", help="s=status w=save t=clock f=feed p=play n=nap h=dance q=joke u=menu j=next k=select [=BOOT ]=KEY x=cancel o=screen-off v=screen-wake b=power-tap d=diagnostics")
    args = parser.parse_args()
    # Native USB can take a few seconds to re-enumerate after a successful flash.
    deadline = time.monotonic() + 10
    while True:
        try:
            connection = serial.Serial(args.port, 115200, timeout=0.2)
            break
        except serial.SerialException:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.25)
    with connection as device:
        redactor = LineRedactor(discard_initial_line=True)
        # Allow setup/display initialization to finish after USB re-enumerates.
        time.sleep(2)
        device.write(args.send.encode("ascii"))
        until = time.monotonic() + args.seconds
        try:
            while time.monotonic() < until:
                data = device.read(4096)
                if data:
                    print(redactor.feed(data), end="", flush=True)
        finally:
            print(redactor.finish(), end="", flush=True)


if __name__ == "__main__":
    main()
