#!/usr/bin/env python3
"""Reproducible host-only LHP probe. Fetching is an explicit separate command."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
PIN = json.loads((HERE / "source-pin.json").read_text())
DEFAULT_WORK = Path(tempfile.gettempdir()) / f"moss-lhp-browser-{os.getuid()}"


def checked_archive(path):
    if hashlib.sha256(path.read_bytes()).hexdigest() != PIN["sha256"]:
        raise ValueError("Upstream archive checksum mismatch; source was not extracted")
    return path


def source_dir(work, archive):
    """Recreate from a verified archive, never trusting a mutable cached checkout."""
    checked_archive(archive)
    destination = work / "source"
    stage = Path(tempfile.mkdtemp(prefix="source-", dir=work))
    prefix = f"libwebsockets-{PIN['revision']}"
    try:
        with tarfile.open(archive, "r:gz") as bundle:
            for member in bundle.getmembers():
                name = Path(member.name)
                if name.is_absolute() or ".." in name.parts or name.parts[0] != prefix:
                    raise ValueError("Unsafe path in upstream archive")
                relative = Path(*name.parts[1:])
                target = stage / relative
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with bundle.extractfile(member) as src, target.open("wb") as dst:
                        shutil.copyfileobj(src, dst)
                    target.chmod(0o755 if member.mode & 0o111 else 0o644)
                # Symlinks and special files are intentionally not extracted.
        (stage / ".moss-lhp-source").write_text(PIN["revision"] + "\n")
        if destination.exists():
            marker = destination / ".moss-lhp-source"
            if not marker.is_file() or marker.read_text().strip() != PIN["revision"]:
                raise ValueError("Refusing to replace an unrecognized source directory; choose a clean --work path")
            shutil.rmtree(destination)
        stage.rename(destination)
    except BaseException:
        shutil.rmtree(stage, ignore_errors=True)
        raise
    return destination


def fetch(args):
    args.work.mkdir(parents=True, exist_ok=True)
    archive = args.work / "upstream.tar.gz"
    if archive.exists():
        checked_archive(archive)
    else:
        temporary = archive.with_suffix(".download")
        try:
            subprocess.run(["curl", "--fail", "--location", "--proto", "=https",
                            "--tlsv1.2", PIN["url"], "--output", str(temporary)], check=True)
            checked_archive(temporary)
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    print(archive)


def build(args):
    args.work.mkdir(parents=True, exist_ok=True)
    archive = (args.archive or args.work / "upstream.tar.gz").resolve()
    source = source_dir(args.work, archive)
    subprocess.run(["cmake", "--fresh", "-S", str(HERE), "-B", str(args.work / "build"),
                    f"-DLHP_UPSTREAM_SOURCE={source}"], check=True)
    subprocess.run(["cmake", "--build", str(args.work / "build"), "--target",
                    "moss-lhp-probe", "moss-lhp-target-test", "--parallel", str(args.jobs)], check=True)
    print(args.work / "build" / "moss-lhp-probe")


def invoke(binary, fixture, output, limit=200 * 1024):
    output.mkdir(parents=True, exist_ok=True)
    name = fixture.stem
    bmp, layout, metrics = [output / f"{name}.{ext}" for ext in ("bmp", "layout.txt", "json")]
    completed = subprocess.run([str(binary), str(fixture.resolve()), str(bmp), str(layout),
                                str(metrics), str(limit)], capture_output=True,
                               text=True, timeout=10)
    (output / f"{name}.log").write_text(completed.stdout + completed.stderr)
    report = json.loads(metrics.read_text()) if metrics.exists() else {}
    return completed, report, bmp, layout


def run(args):
    binary = args.work / "build" / "moss-lhp-probe"
    output = (args.output or args.work / "results").resolve()
    output.mkdir(parents=True, exist_ok=True)
    lifecycle = subprocess.run([str(args.work / "build" / "moss-lhp-target-test"),
                                str(output / "target-fixture.ppm")],
                               capture_output=True, text=True, timeout=10)
    (output / "target-lifecycle.log").write_text(lifecycle.stdout + lifecycle.stderr)
    if lifecycle.returncode:
        raise RuntimeError(f"Target engine lifecycle failed; inspect {output / 'target-lifecycle.log'}")
    summaries = []
    for fixture in sorted((HERE / "fixtures").glob("*.html")):
        completed, report, bmp, layout = invoke(binary, fixture, output)
        if completed.returncode or not report.get("success"):
            raise RuntimeError(f"{fixture.name} failed; inspect {output / (fixture.stem + '.log')}")
        pixels = bmp.read_bytes()
        assert pixels[:2] == b"BM" and len(pixels) == 54 + 480 * 480 * 3
        assert struct.unpack_from("<ii", pixels, 18) == (480, -480)
        assert len(set(pixels[54:])) > 2, "Expected actual rendered text/color content"
        assert report["rendered_rows"] == 480 and report["text_nodes"] > 0
        assert report["allocation_failures"] == 0
        assert report["network_attempts_blocked"] == 0
        assert report["link_regions"] >= 1 and "link x=" in layout.read_text()
        if fixture.name == "image.html":
            assert "png x=" in layout.read_text(), "Expected an actual decoded PNG DLO"
        if fixture.name == "remote-blocked.html":
            assert "png x=" not in layout.read_text(), "Remote PNG must be filtered"
        if fixture.name == "tall.html":
            assert report["content_bottom_px"] > 480, "Fixture must exceed the viewport"
        assert "text x=" in layout.read_text()
        assert report["lws_requested_heap_after_cleanup_bytes"] == 0, "LWS retained allocations"
        summaries.append({"fixture": fixture.name, **report,
                          "bmp_sha256": hashlib.sha256(pixels).hexdigest()})
    # The entrypoint must reject URLs rather than silently enabling networking.
    rejected = subprocess.run([str(binary), "https://example.invalid/", "/dev/null",
                               "/dev/null", "/dev/null"], capture_output=True, timeout=2)
    assert rejected.returncode == 2, "A remote document URL was accepted"
    # Quota failure is a deliberate negative test: it must not hang or crash.
    completed, report, _, _ = invoke(binary, HERE / "fixtures" / "reader.html",
                                    output / "heap-limit", 32768)
    assert completed.returncode == 1 and not report.get("success")
    assert report.get("allocation_failures", 0) > 0
    assert report.get("lws_requested_heap_after_cleanup_bytes") == 0
    # Input bounds must reject before the parser or renderer is started.
    oversized = output / "oversized.html"
    oversized.write_bytes(b"x" * (65536 + 1))
    for rejected_path in (oversized, output / "missing.html"):
        invalid = subprocess.run([str(binary), str(rejected_path), "/dev/null",
                                  "/dev/null", "/dev/null"], capture_output=True, timeout=2)
        assert invalid.returncode == 2
    corrupt = output / "corrupt-archive.tar.gz"
    corrupt.write_bytes(b"invalid archive")
    try:
        checked_archive(corrupt)
    except ValueError:
        pass
    else:
        raise AssertionError("Corrupt archive accepted")
    summary = {"upstream_revision": PIN["revision"],
               "network": "local documents only; http(s) assets filtered; socket and DNS test guards deny outbound requests",
               "memory_note": "Native requested LWS heap plus scanline only; excludes allocator headers, libc, stacks, Wi-Fi and TLS. Not an ESP32 footprint.",
               "target_engine_lifecycle": "passed: rerender, cancel, restart, RGB565 stripes and cleanup",
               "fixtures": summaries, "negative_tests": ["remote URL rejected", "32 KiB heap cap rejected safely", "oversized and missing inputs rejected", "corrupt source archive rejected"]}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    print(f"Artifacts: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=DEFAULT_WORK,
                        help="Build/source/results directory (default: temporary directory)")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("fetch", help="Explicitly download and verify the pinned upstream archive")
    builder = commands.add_parser("build", help="Build from an already downloaded verified archive")
    builder.add_argument("--archive", type=Path)
    builder.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 2))
    runner = commands.add_parser("run", help="Render bounded local fixtures and run negative checks")
    runner.add_argument("--output", type=Path)
    args = parser.parse_args()
    args.work = args.work.resolve()
    try:
        {"fetch": fetch, "build": build, "run": run}[args.command](args)
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"LHP probe failed: {exc}\n")


if __name__ == "__main__":
    main()
