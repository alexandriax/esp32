#!/usr/bin/env python3
"""Opt-in browser checks through real USB button/touch input; never flashes.

CLI: browser-device-check.py --port /dev/cu.usbmodem101 --url https://example.com
Import Device(port) to compose load(), tap(), swipe(), reload(), back(), exit(),
start_load(), status() and wait() checks. Device construction opens only the explicit port.
All printed results are allowlisted numeric diagnostics, never serial text,
addresses, page content, network names or passwords. A Wi-Fi connection must
already be saved using the device's own Wi-Fi settings.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import string
import time

KEYS = string.ascii_lowercase + string.ascii_uppercase + string.digits + " " + string.punctuation
assert len(KEYS) == 95 and len(set(KEYS)) == 95
GROUPS = frozenset(("BROWSER", "HEAP", "APP", "WIFI_NETWORKS"))
NUMERIC_GROUPS = GROUPS | {"TCP"}  # Optional until the network stack is initialized.
NUMBER = re.compile(r"([a-z_][a-z_0-9]*)=(-?[0-9]+)\Z")
PHASE_IDLE, PHASE_READY, PAGE_URL = 0, 6, 1

_spec = importlib.util.spec_from_file_location("moss_browser_monitor", Path(__file__).with_name("monitor.py"))
_monitor = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_monitor)


class CheckError(RuntimeError):
    """Messages are fixed diagnostics; never interpolate untrusted input."""


def parse_numeric_line(line):
    """Discard unknown fields and lines, including every nonnumeric value."""
    fields = line.split()
    if not fields or fields[0] not in NUMERIC_GROUPS:
        return None
    values = {}
    for field in fields[1:]:
        match = NUMBER.fullmatch(field)
        if match:
            values[match[1]] = int(match[2])
    return (fields[0], values) if values else None


def keyboard_steps(focus, target, has_draft, draft_length=None):
    """Match BrowserUi::next, including disabled keys when the draft is empty."""
    if not 0 <= focus <= 98 or not 0 <= target <= 98:
        raise CheckError("Invalid keyboard focus")
    if draft_length is not None:
        has_draft = draft_length > 0
    disabled = {97} if has_draft else {95, 96, 97, 98}
    if draft_length is not None and draft_length >= 511:
        disabled.update(range(95))
    if target in disabled:
        raise CheckError("Requested keyboard key is disabled")
    steps = 0
    while focus != target:
        focus = (focus + 1) % 99
        if focus not in disabled:
            steps += 1
        if steps > 99:
            raise CheckError("Keyboard focus did not converge")
    return steps


class Device:
    def __init__(self, port, *, connection=None, startup_wait=2.0):
        if not port:
            raise CheckError("An explicit serial port is required")
        if connection is None:
            import serial
            connection = serial.Serial(port, 115200, timeout=0.1)
        self.connection = connection
        self.redactor = _monitor.LineRedactor(discard_initial_line=True)
        self.latest = {}
        self._versions = {group: 0 for group in GROUPS}
        self.pump(startup_wait)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        self.redactor.finish()  # Drop any incomplete sensitive line.
        self.connection.close()

    def pump(self, seconds=0.1):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            for line in self.redactor.feed(self.connection.read(4096)).splitlines():
                parsed = parse_numeric_line(line)
                if parsed:
                    group, values = parsed
                    self.latest[group] = values
                    self._versions[group] = self._versions.get(group, 0) + 1
        return self.latest

    def send(self, commands):
        # Callers send control packets, never a URL or Wi-Fi credential command.
        self.connection.write(commands.encode("ascii"))
        self.connection.flush()

    def status(self, timeout=4.0):
        # Drain previous queries before defining this response's generation.
        self.pump(0.12)
        versions = self._versions.copy()
        self.send("sI")
        until = time.monotonic() + timeout
        next_query = time.monotonic() + 0.75
        while time.monotonic() < until:
            self.pump(0.05)
            if all(self._versions[group] > versions[group] for group in GROUPS):
                return {group: dict(self.latest[group]) for group in sorted(self.latest) if group in NUMERIC_GROUPS}
            if time.monotonic() >= next_query:
                # CDC can drop a diagnostic reply while a full keyboard redraw
                # is draining. Retry only this read, never an input action.
                self.send("sI")
                next_query = time.monotonic() + 0.75
        raise CheckError("Timed out waiting for numeric browser diagnostics")

    def wait(self, predicate, timeout=100.0, interval=0.2):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            state = self.status(timeout=min(4.0, max(0.1, until-time.monotonic())))
            if predicate(state):
                return state
            self.pump(min(interval, max(0.0, until-time.monotonic())))
        raise CheckError("Timed out waiting for requested browser state")

    def tap(self, x, y):
        if not (0 <= x <= 239 and 0 <= y <= 239):
            raise CheckError("Touch coordinate is outside the logical screen")
        # Mode transitions and side buttons require a release before a new tap.
        self.send(f"L0,{x},{y}\nL1,{x},{y}\nL0,{x},{y}\n")
        self.pump(0.15)

    def swipe(self, x=120, start_y=180, end_y=60, steps=6):
        if steps < 1 or not all(0 <= v <= 239 for v in (x, start_y, end_y)):
            raise CheckError("Invalid swipe geometry")
        # Deliver a complete synthetic contact before the real sensor's next
        # poll can release it. Physical fingers supply continuous down packets.
        commands = [f"L0,{x},{start_y}\nL1,{x},{start_y}\n"]
        for index in range(1, steps+1):
            y = round(start_y + (end_y-start_y)*index/steps)
            commands.append(f"L1,{x},{y}\n")
        commands.append(f"L0,{x},{end_y}\n")
        self.send("".join(commands))
        return self.wait_result()

    def open(self, timeout=100.0):
        self.send("v")
        self.pump(0.3)  # Wake transition/guard must finish before button actions.
        state = self.status()
        if state["BROWSER"].get("active"):
            return state
        if state["APP"].get("remote"):
            raise CheckError("Exit Remote Display before running browser checks")
        if state["WIFI_NETWORKS"].get("open"):
            self.send("x")
            self.pump(0.2)
        self.send("u")
        # A menu already open on a child page preserves that page; go back using
        # the actual handler until Main, then navigate using its reported focus.
        for _ in range(8):
            state = self.status()
            app = state["APP"]
            if app.get("menu") and app.get("page") == 0:
                focus = app.get("focus", -1)
                if not 0 <= focus < 6:
                    raise CheckError("Unexpected main-menu focus")
                self.send("[" * ((1-focus) % 6) + "]")
                return self.wait(lambda s: s["BROWSER"].get("active") or
                                 s["WIFI_NETWORKS"].get("open"), timeout)
            self.send("x")
            self.pump(0.2)
        raise CheckError("Could not reach the main menu")

    def wait_result(self, timeout=100.0):
        return self.wait(lambda s: (
            s["BROWSER"].get("active") and s["BROWSER"].get("page") == 0 and
            s["BROWSER"].get("phase") in (PHASE_IDLE, PHASE_READY)
        ) or s["WIFI_NETWORKS"].get("open") or not (
            s["BROWSER"].get("active") or s["BROWSER"].get("pending") or
            s["BROWSER"].get("reclaim")
        ), timeout)

    def start_load(self, url, timeout=100.0):
        """Enter an address and select GO, returning before network completion."""
        if not url or len(url) > 511 or any(ch not in KEYS for ch in url):
            raise CheckError("URL must contain 1 to 511 printable ASCII characters")
        state = self.open(timeout)
        if not state["BROWSER"].get("active"):
            raise CheckError("Save a working Wi-Fi network on the device first")
        if state["BROWSER"].get("page") != PAGE_URL:
            self.tap(120, 15)
        state = self.wait(lambda s: s["BROWSER"].get("page") == PAGE_URL, 10)
        # Newly opened editor has a nonempty address. Clear is a real UI key.
        # If an earlier operator left an empty editor, tapping Clear is harmless;
        # query the resulting focus and use empty-draft skip rules in either case.
        self.tap(120, 218)
        state = self.status()
        if state["BROWSER"].get("page") != PAGE_URL:
            raise CheckError("URL editor closed unexpectedly")
        focus = state["BROWSER"].get("focus", -1)
        has_draft = False
        for ch in url:
            target = KEYS.index(ch)
            self.send("[" * keyboard_steps(focus, target, has_draft) + "]")
            state = self.status()
            if state["BROWSER"].get("page") != PAGE_URL or state["BROWSER"].get("focus") != target:
                raise CheckError("Keyboard focus did not match the selected character")
            focus, has_draft = target, True
        self.send("[" * keyboard_steps(focus, 98, True, len(url)) + "]")
        return self.status()

    def load(self, url, timeout=100.0):
        self.start_load(url, timeout)
        return self.wait_result(timeout)

    def reload(self, timeout=100.0):
        self.tap(90, 220)
        return self.wait_result(timeout)

    def stop(self, timeout=10.0):
        if self.status()["BROWSER"].get("loading"):
            self.tap(90, 220)
        return self.wait_result(timeout)

    def back(self, timeout=100.0):
        self.send("x")
        return self.wait_result(timeout)

    def exit(self, timeout=15.0):
        # Exercise the user-facing confirmation, then the normal drain handler.
        state = self.status()
        browser = state["BROWSER"]
        if browser.get("active") or browser.get("pending") or browser.get("reclaim"):
            self.send("u")
            confirm = self.wait(lambda s: not s["BROWSER"].get("active") or s["BROWSER"].get("page") == 2, timeout)
            if confirm["BROWSER"].get("active"):
                self.tap(175, 142)
        return self.wait(lambda s: not any(s["BROWSER"].get(key) for key in
                         ("active", "pending", "reclaim")), timeout)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--action", choices=("status", "open", "url", "exit"), default=None)
    parser.add_argument("--url", help="Address entered via the device keyboard; never printed")
    parser.add_argument("--exit-after", action="store_true")
    parser.add_argument("--timeout", type=float, default=None,
                        help="Seconds to wait; default 15 for exit, 100 for other actions")
    args = parser.parse_args(argv)
    action = args.action or ("url" if args.url is not None else "status")
    if (action == "url") != (args.url is not None):
        parser.error("The url action requires --url; other actions do not accept it")
    if args.timeout is None:
        args.timeout = 15.0 if action == "exit" else 100.0
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        with Device(args.port) as device:
            if action == "url":
                state = device.load(args.url, args.timeout)
            elif action == "open":
                device.open(args.timeout)
                state = device.wait_result(args.timeout)
            elif action == "exit":
                state = device.exit(args.timeout)
            else:
                state = device.status()
            print(json.dumps(state, sort_keys=True), flush=True)
            if args.exit_after:
                print(json.dumps(device.exit(), sort_keys=True), flush=True)
            if action in ("open", "url"):
                return 0 if state["BROWSER"].get("phase") == PHASE_READY else 2
    except Exception as error:
        # Serial exceptions can contain arbitrary backend detail: keep CLI
        # output deliberately fixed. CheckError messages never include input.
        print(json.dumps({"check_error": str(error) if isinstance(error, CheckError)
                          else type(error).__name__}), flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
