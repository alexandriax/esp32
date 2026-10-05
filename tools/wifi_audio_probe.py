#!/usr/bin/env python3
"""Play a bounded synthetic tone on Moss only; never opens Mac audio or a microphone.

Quit the companion and select Wi-Fi Display on a previously provisioned device
attached by USB. Pairing data is read only from USB and retained only in memory.
"""
import argparse
import hashlib
import math
import socket
import ssl
import struct
import time

import serial
from benchmark_display import packet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--seconds", type=int, choices=range(5, 61), default=15)
    parser.add_argument("--volume", type=int, choices=range(0, 61), default=35)
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=.1) as device:
        device.write(b"?")
        deadline = time.monotonic() + 25
        fields = None
        while time.monotonic() < deadline:
            line = device.readline().decode("ascii", "replace").strip().split()
            if len(line) == 7 and line[:2] == ["DISPLAY", "WIFI_READY"]:
                fields = line
                break
        if not fields:
            raise RuntimeError("No listening Wi-Fi display; choose Wi-Fi Display on Moss first")
        nonce, address, port, pin, token = fields[2:]
        session = int(nonce, 16)
        if len(nonce) != 16 or len(pin) != 64 or len(token) != 64 or not session:
            raise RuntimeError("Invalid USB pairing metadata")
        expected_pin = bytes.fromhex(pin)
        bytes.fromhex(token)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.check_hostname = False
        # Self-signed leaf is authenticated by the exact USB-provided SHA-256 pin.
        context.verify_mode = ssl.CERT_NONE
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.maximum_version = ssl.TLSVersion.TLSv1_2
        with socket.create_connection((address, int(port)), timeout=5) as tcp:
            with context.wrap_socket(tcp, server_hostname=None) as net:
                if hashlib.sha256(net.getpeercert(binary_form=True)).digest() != expected_pin:
                    raise RuntimeError("Device certificate does not match trusted USB pairing")
                net.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                pending = bytearray()

                def wait(prefix, limit=3):
                    until = time.monotonic() + limit
                    while time.monotonic() < until:
                        while b"\n" in pending:
                            raw, _, rest = pending.partition(b"\n")
                            pending[:] = rest
                            if len(raw) > 512:
                                raise RuntimeError("Oversized device reply")
                            line = raw.decode("ascii", "strict")
                            if line.startswith("DISPLAY AUDIO_PERF "):
                                values = line.split()[3:]
                                if len(values) == 9 and all(x.isdecimal() for x in values):
                                    print("audio queued/under/over/dropped/errors/heap/min/received/rendered=" + "/".join(values), flush=True)
                            if line.startswith(prefix):
                                return line
                            if line.startswith("DISPLAY NACK") or line.startswith("DISPLAY STOP"):
                                raise RuntimeError("Device rejected or ended the audio probe")
                        net.settimeout(max(.01, until - time.monotonic()))
                        chunk = net.recv(1024)
                        if not chunk:
                            raise RuntimeError("Device closed the probe connection")
                        pending.extend(chunk)
                        if len(pending) > 2048:
                            raise RuntimeError("Oversized device reply buffer")
                    raise RuntimeError("Timed out waiting for device acknowledgment")

                net.sendall(b"MOSS AUTH " + token.encode("ascii") + b"\n")
                token = None
                wait("MOSS AUTH OK")
                wait(f"DISPLAY WIFI_REQUEST {nonce} ")
                sequence = 0
                try:
                    net.sendall(packet(1, session, payload=struct.pack("<HHHBB", 480, 480, 15360, 1, 0)))
                    wait(f"DISPLAY READY {nonce} 0")
                    net.sendall(packet(14, session, payload=bytes((1, 1, args.volume, 0))))
                    response = wait(f"DISPLAY AUDIO {nonce} ")
                    if response.split()[3:] != ["1", str(args.volume), "0"]:
                        raise RuntimeError("Device audio did not start")
                    print(f"Playing {args.seconds}s of synthetic pulses on Moss only, volume={args.volume}", flush=True)
                    began, last_ping = time.monotonic(), 0
                    samples = 0
                    for _ in range(args.seconds * 10):
                        now = time.monotonic()
                        if now - began >= args.seconds:
                            break
                        if now - last_ping >= .5:
                            sequence += 1
                            net.sendall(packet(3, session, sequence))
                            wait(f"DISPLAY ACK {nonce} {sequence}")
                            last_ping = now
                        pcm = []
                        for i in range(1600):
                            t = (samples + i) / 16000
                            phase = t % 1
                            envelope = max(0, min(1, phase / .02, (.3 - phase) / .02))
                            pcm.append(round(32767 * .25 * envelope * math.sin(2 * math.pi * 440 * t)))
                        net.sendall(packet(15, session, payload=struct.pack("<1600h", *pcm)))
                        wait(f"DISPLAY AUDIO_ACK {nonce}")
                        samples += 1600
                        time.sleep(max(0, began + samples / 16000 - time.monotonic()))
                    print(f"Probe completed: {samples} source samples acknowledged", flush=True)
                finally:
                    try:
                        net.sendall(packet(14, session, payload=bytes((1, 0, args.volume, 0))))
                        net.sendall(packet(5, session))
                    except OSError:
                        pass


if __name__ == "__main__":
    main()
