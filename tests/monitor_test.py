"""Serial redaction tests: synthetic bytes only, no pyserial or USB access."""
import importlib.util
import contextlib
import io
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "monitor.py"
spec = importlib.util.spec_from_file_location("moss_monitor", SCRIPT)
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)
LineRedactor = monitor.LineRedactor


class MonitorRedactionTest(unittest.TestCase):
    def test_every_pairing_line_split(self):
        for marker in LineRedactor.MARKERS:
            line = b"prefix " + marker + b" nonce device fingerprint synthetic-secret\r\n"
            for at in range(len(line) + 1):
                redactor = LineRedactor()
                output = redactor.feed(line[:at]) + redactor.feed(line[at:]) + redactor.finish()
                self.assertEqual(output, LineRedactor.OMITTED, (marker, at))

    def test_bytewise_and_multiple_lines(self):
        redactor = LineRedactor()
        stream = (b"STATE food=90\r\nDISPLAY PAIRING synthetic-token\n"
                  b"DISPLAY WIFI_READY other-token\nDISPLAY PERF 123\n")
        output = "".join(redactor.feed(bytes([byte])) for byte in stream)
        self.assertEqual(output, "STATE food=90\r\n" + LineRedactor.OMITTED * 2 + "DISPLAY PERF 123\n")
        self.assertEqual(redactor.finish(), "")

    def test_utf8_spanning_chunks_and_invalid_bytes(self):
        redactor = LineRedactor()
        self.assertEqual(redactor.feed(b"Moss \xf0\x9f"), "")
        self.assertEqual(redactor.feed(b"\xa6\xa5\ninvalid \xff\n"), "Moss 🦥\ninvalid �\n")

    def test_no_fragment_is_released_before_newline(self):
        for marker in LineRedactor.MARKERS:
            for length in range(1, len(marker) + 1):
                redactor = LineRedactor()
                self.assertEqual(redactor.feed(b"prefix " + marker[:length]), "")
                output = redactor.finish()
                self.assertIn("omitted", output)
                self.assertNotIn("prefix", output)
                self.assertNotIn("DISPLAY", output)
                self.assertEqual(redactor.finish(), "")
        redactor = LineRedactor()
        self.assertEqual(redactor.feed(b"DISPLAY PAIRING secret-with-no-newline"), "")
        self.assertEqual(redactor.finish(), LineRedactor.OMITTED)

    def test_buffer_bound_and_markers_beyond_limit(self):
        for marker_at in (0, 4088, 5000):
            redactor = LineRedactor()
            line = b"x" * marker_at + b"DISPLAY WIFI_READY" + b"synthetic-token" * 500
            for at in range(0, len(line), 7):
                self.assertEqual(redactor.feed(line[at:at + 7]), "")
                self.assertLessEqual(len(redactor._pending), LineRedactor.MAX_LINE_BYTES)
                self.assertLessEqual(len(redactor._tail), LineRedactor._TAIL_BYTES)
            self.assertEqual(redactor.feed(b"\nOK\n"), LineRedactor.OMITTED + "OK\n")
        redactor = LineRedactor()
        self.assertEqual(redactor.feed(b"x" * 4096 + b"\n"), "x" * 4096 + "\n")
        self.assertEqual(redactor.feed(b"y" * 4097 + b"\n"), "[oversized serial line omitted]\n")
        self.assertEqual(redactor.feed(b"z" * 100000), "")
        self.assertEqual(redactor.finish(), "[oversized serial line omitted]\n")

    def test_initial_unknown_fragment_is_never_printed(self):
        redactor = LineRedactor(discard_initial_line=True)
        self.assertEqual(redactor.feed(b"token-tail-with-missing-prefix"), "")
        self.assertEqual(redactor.feed(b"\nSTATE ok\n"), "[initial serial line omitted]\nSTATE ok\n")
        redactor = LineRedactor(discard_initial_line=True)
        self.assertEqual(redactor.feed(b"unknown-secret-tail"), "")
        self.assertEqual(redactor.finish(), "[initial serial line omitted]\n")
        redactor = LineRedactor(discard_initial_line=True)
        self.assertEqual(redactor.feed(b"DISPLAY PAIRING secret\n"), LineRedactor.OMITTED)

    def test_empty_input_and_blank_lines(self):
        redactor = LineRedactor()
        self.assertEqual(redactor.feed(b""), "")
        self.assertEqual(redactor.finish(), "")
        self.assertEqual(redactor.feed(b"\n\n"), "\n\n")
        self.assertEqual(redactor.finish(), "")

    def test_monitor_deadline_flush_uses_redactor_without_usb(self):
        clock = [0.0]
        chunks = iter((b"unknown-secret-tail\n", b"DISPLAY PAI", b"RING secret\nSTATE ok\n",
                       b"DISPLAY WIFI_", b"READY incomplete-secret"))

        class Device:
            closed = False
            sent = None

            def __enter__(self):
                return self

            def __exit__(self, *_):
                self.closed = True

            def write(self, data):
                self.sent = data

            def read(self, _):
                clock[0] += 0.1
                return next(chunks, b"")

        device = Device()
        fake_serial = types.SimpleNamespace(Serial=lambda *args, **kwargs: device,
                                            SerialException=OSError)
        output = io.StringIO()
        with patch.dict(sys.modules, serial=fake_serial), \
             patch.object(sys, "argv", ["monitor.py", "/dev/FAKE", "--seconds", "0.5"]), \
             patch.object(monitor.time, "monotonic", side_effect=lambda: clock[0]), \
             patch.object(monitor.time, "sleep", side_effect=lambda seconds: clock.__setitem__(0, clock[0] + seconds)), \
             contextlib.redirect_stdout(output):
            monitor.main()
        self.assertEqual(output.getvalue(), "[initial serial line omitted]\n" +
                         LineRedactor.OMITTED + "STATE ok\n" + LineRedactor.OMITTED)
        self.assertTrue(device.closed)
        self.assertEqual(device.sent, b"s")


if __name__ == "__main__":
    unittest.main()
