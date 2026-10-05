#!/usr/bin/env python3
"""Disposable TLS peer; tests real Network.framework traffic only on loopback."""
import hashlib
import pathlib
import socket
import ssl
import subprocess
import sys
import threading
import time

root = pathlib.Path(sys.argv[1])
der = ssl.PEM_cert_to_DER_cert((root / "cert.pem").read_text())
fingerprint = hashlib.sha256(der).hexdigest()
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.minimum_version = ssl.TLSVersion.TLSv1_2
context.load_cert_chain(root / "cert.pem", root / "key.pem")
failures = []
validation = subprocess.run([str(root / "wifi_channel_test"), "9", fingerprint, "invalid-token"],
                            capture_output=True, text=True, timeout=3)
print(f"invalid-token: {validation.stdout.strip() or validation.stderr.strip()}")
if validation.returncode:
    failures.append("Invalid hexadecimal token was not rejected immediately")
for case in ("transfer-disconnect", "wrong-pin", "bad-auth", "oversized-auth", "overflow-after-prefix", "stream-lines", "stop-on-line", "line-overflow", "line-binary", "line-prefix"):
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    server.settimeout(10)
    errors = []

    def serve():
        try:
            raw, _ = server.accept()
            raw.settimeout(5)
            try:
                peer = context.wrap_socket(raw, server_side=True)
            except ssl.SSLError:
                raw.close()
                if case == "wrong-pin":
                    return
                raise
            with peer:
                if case == "wrong-pin":
                    try:
                        data = peer.recv(1)
                        assert not data, "Incorrect pin allowed application traffic"
                    except ssl.SSLError:
                        pass
                    return
                auth = b""
                while not auth.endswith(b"\n") and len(auth) < 100:
                    chunk = peer.recv(100 - len(auth))
                    assert chunk, "Peer closed before authentication"
                    auth += chunk
                assert auth == b"MOSS AUTH " + b"a" * 64 + b"\n", auth
                if case == "transfer-disconnect":
                    # Check accumulation across multiple receives as well as pin/auth.
                    peer.sendall(b"MOSS ")
                    time.sleep(0.05)
                    peer.sendall(b"AUTH OK\n")
                    packet = b""
                    while len(packet) < 65536:
                        chunk = peer.recv(65536 - len(packet))
                        assert chunk, "Peer closed before sending the test packet"
                        packet += chunk
                    assert packet == bytes(range(256)) * 256, "TLS batch bytes differ"
                    time.sleep(0.1)  # Ensure send completion precedes remote close.
                elif case in ("stream-lines", "stop-on-line"):
                    peer.sendall(b"MOSS AUTH ")
                    time.sleep(0.03)
                    peer.sendall(b"OK\nDISPLAY CAPS 0123456789abcdef 31\nDISPLAY ACK 0123")
                    time.sleep(0.03)
                    try:
                        peer.sendall(b"456789abcdef 1\n")
                    except (BrokenPipeError, ssl.SSLError):
                        pass
                elif case.startswith("line-"):
                    invalid = {"line-overflow": b"DISPLAY " + b"X" * 505,
                               "line-binary": b"DISPLAY ACK\x00\n",
                               "line-prefix": b"UNTRUSTED OTHER LINE\n"}[case]
                    peer.sendall(b"MOSS AUTH OK\n" + invalid)
                elif case == "bad-auth":
                    peer.sendall(b"MOSS AUTH NO\n")
                elif case == "oversized-auth":
                    peer.sendall(b"X" * 256)
                elif case == "overflow-after-prefix":
                    peer.sendall(b"MOSS AUTH OK")
                    time.sleep(0.1)
                    peer.sendall(b"X" * 256)
                    time.sleep(0.1)
                    try:
                        peer.sendall(b"\n")
                    except (BrokenPipeError, ssl.SSLError):
                        pass  # A strict parser may already have rejected the junk.
                if case != "transfer-disconnect":
                    time.sleep(0.3)
        except Exception as error:
            errors.append(repr(error))
        finally:
            server.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    pin = "00" * 32 if case == "wrong-pin" else fingerprint
    run = subprocess.run([str(root / "wifi_channel_test"), str(server.getsockname()[1]), pin, case],
                         capture_output=True, text=True, timeout=22)
    thread.join(timeout=11)
    print(f"{case}: {run.stdout.strip() or run.stderr.strip()}")
    if run.returncode or errors or thread.is_alive():
        failures.append(f"{case}: exit={run.returncode}, server={errors}, stderr={run.stderr.strip()}")
if failures:
    print("\n".join(failures), file=sys.stderr)
    sys.exit(1)
print("All loopback TLS channel tests passed")
