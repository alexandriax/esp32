"""Synthetic browser control tests; never import pyserial or open hardware."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
import contextlib
import io

script = Path(__file__).resolve().parents[1] / "scripts" / "browser-device-check.py"
spec = importlib.util.spec_from_file_location("browser_device_check", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BrowserDeviceCheckTest(unittest.TestCase):
    def test_numeric_allowlist(self):
        self.assertEqual(module.parse_numeric_line("BROWSER phase=6 focus=-1 url=https://private.invalid"),
                         ("BROWSER", {"phase": 6, "focus": -1}))
        self.assertEqual(module.parse_numeric_line("TCP fin_wait_1=0 time_wait=0 peer=private.invalid"),
                         ("TCP", {"fin_wait_1": 0, "time_wait": 0}))
        for line in ("DISPLAY PAIRING synthetic-secret", "STATE food=20", "arbitrary page text", "BROWSER url=secret"):
            self.assertIsNone(module.parse_numeric_line(line))

    def test_redaction_before_parsing(self):
        redactor = module._monitor.LineRedactor(discard_initial_line=True)
        text = redactor.feed(b"BROWSER phase=6\nBROWSER phase=4 DISPLAY PAIRING synthetic-secret\nBROWSER phase=0\n")
        parsed = [module.parse_numeric_line(line) for line in text.splitlines()]
        self.assertEqual([value for value in parsed if value], [("BROWSER", {"phase": 0})])

    def test_keyboard_disabled_empty_draft_and_punctuation(self):
        self.assertEqual(module.keyboard_steps(96, 0, False), 1)  # a; Back is a separate navigation action.
        self.assertEqual(module.keyboard_steps(94, 0, False), 1)  # Skips Delete/Clear/Cancel/GO.
        self.assertEqual(module.keyboard_steps(94, 0, True), 4)
        self.assertEqual(module.keyboard_steps(0, 98, True), 97)
        self.assertEqual(module.keyboard_steps(0, 98, True, 511), 3)
        self.assertEqual(module.keyboard_steps(94, 98, True, 511), 3)
        for target in (95, 96, 97, 98):
            with self.assertRaises(module.CheckError):
                module.keyboard_steps(0, target, False)
        self.assertEqual(module.KEYS, 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~')

    def test_start_load_real_keyboard_sequence(self):
        class Simulated(module.Device):
            def __init__(self):
                self.page, self.focus, self.draft, self.phase = 0, 0, "https://example.com", 6
                self.navigated = None
                self.waited = False
            def state(self):
                return {"BROWSER": {"active": 1, "page": self.page,
                                    "focus": self.focus, "phase": self.phase}}
            def open(self, timeout=100): return self.state()
            def status(self, timeout=4): return self.state()
            def wait(self, predicate, timeout=100):
                state = self.state()
                assert predicate(state)
                return state
            def tap(self, x, y):
                if y == 15:
                    self.page, self.focus = 1, 0
                else:
                    assert (x, y) == (120, 218)
                    if self.draft:
                        self.draft, self.focus = "", 96
            def send(self, commands):
                for command in commands:
                    if command == "[":
                        self.focus = (self.focus+1) % 99
                        while (self.focus == 97 or (not self.draft and self.focus in (95, 96, 98)) or
                               (len(self.draft) >= 511 and self.focus < 95)):
                            self.focus = (self.focus+1) % 99
                    else:
                        assert command == "]"
                        if self.focus == 98:
                            self.navigated, self.draft = self.draft, ""
                            self.page, self.focus, self.phase = 0, 0, 1
                        else:
                            assert self.focus < 95
                            self.draft += module.KEYS[self.focus]
            def wait_result(self, timeout=100):
                self.waited = True
                self.phase = 6
                return self.state()
        address = "http://192.0.2.1:8000/A?x=a_B&n=1#anchor"
        device = Simulated()
        self.assertEqual(device.start_load(address)["BROWSER"]["phase"], 1)
        self.assertEqual(device.navigated, address)
        self.assertFalse(device.waited)
        device = Simulated()
        self.assertEqual(device.load(address)["BROWSER"]["phase"], 6)
        self.assertTrue(device.waited)
        self.assertEqual(device.navigated, address)
        device = Simulated()
        device.start_load("a" * 511)
        self.assertEqual(device.navigated, "a" * 511)

    def test_cli_exit_default_timeout(self):
        for arguments, expected in (([], 15.0), (["--timeout", "7"], 7.0)):
            with patch.object(module, "Device") as factory, contextlib.redirect_stdout(io.StringIO()):
                device = factory.return_value.__enter__.return_value
                device.exit.return_value = {"BROWSER": {"active": 0, "phase": 7}}
                self.assertEqual(module.main(["--port", "synthetic", "--action", "exit"] + arguments), 0)
                device.exit.assert_called_once_with(expected)

    def test_synthetic_status_and_tap(self):
        class Serial:
            buffer = b"initial unknown fragment\n"
            writes = []
            def read(self, count):
                data, self.buffer = self.buffer[:count], self.buffer[count:]
                return data
            def write(self, data):
                self.writes.append(data)
                if self.writes == [b"sI"]:
                    return  # A lost diagnostic reply must not repeat UI input.
                if data == b"sI":
                    self.buffer += (b"APP menu=1 page=0 focus=1\nBROWSER active=1 phase=6 page=0\n"
                                    b"DISPLAY WIFI_READY synthetic-secret\nWIFI_NETWORKS open=0 saved=1\n"
                                    b"HEAP integrity=1 free=1000 largest=900\n")
            def flush(self): pass
            def close(self): pass
        serial = Serial()
        with module.Device("synthetic", connection=serial, startup_wait=0) as device:
            state = device.status()
            self.assertEqual(state["BROWSER"]["phase"], 6)
            self.assertEqual(state["HEAP"]["integrity"], 1)
            self.assertEqual(set(state), module.GROUPS)
            self.assertEqual(serial.writes, [b"sI", b"sI"])
            device.tap(90, 230)
            self.assertEqual(serial.writes[-1], b"L0,90,230\nL1,90,230\nL0,90,230\n")
        with self.assertRaises(module.CheckError):
            module.Device("", connection=serial)


if __name__ == "__main__":
    unittest.main()
