#!/usr/bin/env python3
"""Summarize numeric display logs within a synthetic workload's time window."""
import argparse
import datetime
import math
import re
import statistics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log")
parser.add_argument("workload", help="Output from benchmark-display-host.sh")
parser.add_argument("--warmup", type=float, default=2.0,
                    help="Exclude this many initial workload seconds (default: 2)")
args = parser.parse_args()
if not math.isfinite(args.warmup) or args.warmup < 0:
    parser.error("--warmup must be a finite, nonnegative number of seconds")
with open(args.workload, encoding="utf-8") as source:
    workload = source.read()
begin = re.search(r"begin_epoch=([0-9.]+)", workload)
end = re.search(r"end_epoch=([0-9.]+)", workload)
if not begin or not end:
    parser.error("Workload log needs completed BENCH begin_epoch/end_epoch markers")
workload_start, stop = float(begin[1]), float(end[1])
start = workload_start + args.warmup
if stop <= start:
    parser.error("Workload must last longer than --warmup")
frames = []
with open(args.log, encoding="utf-8") as source:
    for line in source:
        if "DISPLAY FRAME COMPLETE" not in line:
            continue
        try:
            # NSLog uses local time; run analysis in the same timezone as capture.
            timestamp = datetime.datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S.%f").timestamp()
        except ValueError:
            continue
        if not start <= timestamp <= stop:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if all(k in fields for k in ("session", "frames", "rectangles", "wire_bytes", "elapsed_ms")):
            frames.append((timestamp, fields))
print(f"Workload: {stop-workload_start:.1f} s; excluded warmup: {args.warmup:.1f} s; analysis window: {stop-start:.1f} s")
if not frames:
    print("No periodic acknowledged-frame samples inside the workload window.")
    raise SystemExit(0)
moving = [f for _, f in frames if int(f["rectangles"]) > 0]
print(f"Periodic frame samples: {len(frames)}; changed samples: {len(moving)}")
if moving:
    timings = [float(f["elapsed_ms"]) for f in moving]
    sizes = [int(f["wire_bytes"]) for f in moving]
    print(f"Changed-frame transfer time: median {statistics.median(timings):.1f} ms, range {min(timings):.1f}–{max(timings):.1f} ms")
    print(f"Changed-frame wire bytes: median {statistics.median(sizes):.0f}, range {min(sizes)}–{max(sizes)}")
first_time, first = frames[0]
last_time, last = frames[-1]
if len(frames) > 1 and first["session"] == last["session"] and last_time > first_time:
    changed_counter = all("changed_frames" in fields for _, fields in frames)
    counter = "changed_frames" if changed_counter else "frames"
    count = int(last[counter]) - int(first[counter])
    if count < 0:
        print("Cadence unavailable: frame counter reset within the sample interval.")
    else:
        label = "Completed changed-frame cadence" if changed_counter else "Legacy processed-frame cadence (includes unchanged captures)"
        print(f"{label}: {count/(last_time-first_time):.2f}/s over {last_time-first_time:.2f} s between periodic samples")
        if not changed_counter:
            print("Legacy logs lack changed_frames; this cadence is not physical display refresh FPS.")
