"""Exercise the host RTC protocol without pyserial, hardware, or real sleeping."""
import collections
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import Mock, patch


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "sync_clock.py"
EPOCH = 1790899200


class PortUnavailable(Exception):
    pass


class FakeClock:
    def __init__(self, epoch):
        self.epoch = epoch
        self.elapsed = 0.0

    def monotonic(self):
        return self.elapsed

    def time(self):
        return self.epoch + self.elapsed

    def sleep(self, seconds):
        self.elapsed += seconds


class FakeDevice:
    def __init__(self, clock, lines):
        self.clock = clock
        self.lines = collections.deque(lines)
        self.writes = []
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.closed = True

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def readline(self):
        self.clock.sleep(0.1)
        return self.lines.popleft() if self.lines else b""


def line(text):
    return (text + "\n").encode("ascii")


def acknowledgment(utc):
    return line(f"CLOCK set ok utc={utc}")


def readback(utc, status=0, synced=True):
    return line(f"CLOCK status={status} utc={utc} accounted_utc={utc} "
                f"sync_needed={0 if synced else 1}")


class SyncClockTests(unittest.TestCase):
    def prepare(self, lines, epoch=EPOCH, port_failures=0):
        self.clock = FakeClock(epoch)
        self.device = FakeDevice(self.clock, lines)
        self.serial_constructor = Mock(side_effect=[
            *[PortUnavailable("USB is re-enumerating") for _ in range(port_failures)],
            self.device,
        ])
        serial_stub = types.ModuleType("serial")
        serial_stub.Serial = self.serial_constructor
        serial_stub.SerialException = PortUnavailable
        spec = importlib.util.spec_from_file_location("sync_clock_under_test", SCRIPT)
        self.module = importlib.util.module_from_spec(spec)
        # No installed serial package can be imported or access a real device.
        with patch.dict(sys.modules, {"serial": serial_stub}):
            spec.loader.exec_module(self.module)
        self.module.time = self.clock

    def invoke(self, timeout=2):
        self.output = io.StringIO()
        with contextlib.redirect_stdout(self.output):
            return self.module.sync_clock("mock-port", timeout=timeout)

    def assert_successful_commands(self, utc):
        self.assertEqual(self.device.writes, [b"t\n", line(f"T{utc}"), b"t\n"])
        self.assertTrue(self.device.closed)
        self.serial_constructor.assert_called_with("mock-port", 115200, timeout=0.2)

    def test_nine_and_ten_digit_utc_boundaries(self):
        # Firmware accepts both lengths and the same inclusive year bounds.
        for utc in (946684800, 999999999, 1000000000, EPOCH, 4102444799):
            with self.subTest(utc=utc):
                self.prepare([readback(0, status=1, synced=False),
                              acknowledgment(utc), readback(utc)], epoch=utc)
                self.assertEqual(self.invoke(), utc)
                self.assert_successful_commands(utc)
                self.assertIn("Moss clock synchronized:", self.output.getvalue())

    def test_boot_banner_and_noisy_output_establish_readiness(self):
        self.prepare([b"\xff boot output\n", line("DISPLAY ready"),
                      line("READY: KEY=select"), line("SAVE ok"),
                      acknowledgment(EPOCH), readback(EPOCH)])
        self.assertEqual(self.invoke(), EPOCH)
        self.assert_successful_commands(EPOCH)

    def test_usb_reenumeration_is_retried(self):
        self.prepare([readback(0, status=2, synced=False),
                      acknowledgment(EPOCH), readback(EPOCH)], port_failures=2)
        self.assertEqual(self.invoke(), EPOCH)
        self.assertEqual(self.serial_constructor.call_count, 3)
        self.assert_successful_commands(EPOCH)

    def test_missing_readiness_never_sets_clock(self):
        self.prepare([line("old firmware"), b"\xff\n"])
        with self.assertRaisesRegex(RuntimeError, "did not respond to the clock query"):
            self.invoke(timeout=0.5)
        self.assertTrue(self.device.writes)
        self.assertTrue(all(command == b"t\n" for command in self.device.writes))
        self.assertTrue(self.device.closed)

    def test_set_and_persistence_errors_are_reported(self):
        for error in ("setting RTC failed", "saving clock anchor failed"):
            with self.subTest(error=error):
                self.prepare([line("READY:"), line(f"CLOCK error: {error}")])
                with self.assertRaisesRegex(RuntimeError, error):
                    self.invoke()
                self.assertEqual(self.device.writes, [b"t\n", line(f"T{EPOCH}")])
                self.assertTrue(self.device.closed)

    def test_old_acknowledgment_cannot_confirm_new_sync(self):
        self.prepare([line("READY:"), acknowledgment(EPOCH - 1), readback(EPOCH)])
        with self.assertRaisesRegex(RuntimeError, "readback timed out"):
            self.invoke(timeout=0.5)
        self.assertEqual(self.device.writes, [b"t\n", line(f"T{EPOCH}")])
        self.assertTrue(self.device.closed)

    def test_readback_requires_valid_rtc_and_saved_anchor(self):
        for status, synced in ((1, True), (2, True), (0, False)):
            with self.subTest(status=status, synced=synced):
                self.prepare([line("READY:"), acknowledgment(EPOCH),
                              readback(EPOCH, status=status, synced=synced)])
                with self.assertRaisesRegex(RuntimeError, "readback timed out"):
                    self.invoke(timeout=0.5)
                self.assertTrue(self.device.closed)

    def test_readback_skew_tolerance_in_both_directions(self):
        for delta in (-4, -3, 3, 4):
            with self.subTest(delta=delta):
                self.prepare([line("READY:"), acknowledgment(EPOCH), readback(EPOCH + delta)])
                if abs(delta) <= 3:
                    self.assertEqual(self.invoke(), EPOCH + delta)
                else:
                    with self.assertRaisesRegex(RuntimeError, "does not match"):
                        self.invoke()
                self.assertTrue(self.device.closed)

    def test_host_clock_change_is_checked_again_at_readback(self):
        self.prepare([line("READY:"), acknowledgment(EPOCH), readback(EPOCH)])
        self.clock.time = Mock(side_effect=[EPOCH, EPOCH + 60])
        with self.assertRaisesRegex(RuntimeError, "does not match"):
            self.invoke()
        self.assertTrue(self.device.closed)

    def test_invalid_host_year_never_sends_time_command(self):
        for utc in (0, 946684799, 4102444800):
            with self.subTest(utc=utc):
                self.prepare([line("READY:")], epoch=utc)
                with self.assertRaisesRegex(RuntimeError, "within 2000"):
                    self.invoke()
                self.assertEqual(self.device.writes, [b"t\n"])
                self.assertTrue(self.device.closed)


if __name__ == "__main__":
    unittest.main()
