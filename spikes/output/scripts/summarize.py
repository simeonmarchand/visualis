#!/usr/bin/env python3
"""Summarizes a P1 run directory (frames.csv + events.log) into the numbers the README asks for.

Only seconds where the window was exposed count toward frames and drops: macOS doesn't
draw covered windows, and those seconds say nothing about the renderer.

Usage: scripts/summarize.py <log-dir>
"""
import csv
import re
import statistics
import sys
from pathlib import Path


def main(log_dir: Path) -> None:
    rows = list(csv.DictReader((log_dir / "frames.csv").open()))
    if not rows:
        sys.exit(f"no rows in {log_dir / 'frames.csv'}")

    print(f"run: {log_dir}")
    print(f"video mode: {rows[0]['video_mode']}   backend: {rows[0]['backend']}   seconds logged: {len(rows) // max(1, len({r['window'] for r in rows}))}")
    cpu = [float(r["cpu_percent"]) for r in rows]
    rss = [float(r["rss_mb"]) for r in rows]
    print(f"cpu: mean {statistics.mean(cpu):.1f}%  max {max(cpu):.1f}%   memory: peak {max(rss):.0f} MB")

    for window in sorted({r["window"] for r in rows}):
        mine = [r for r in rows if r["window"] == window]
        exposed = [r for r in mine if r["exposed"] == "1"]
        # The first exposed second after a covered period carries the stall from being covered.
        steady = [r for prev, r in zip(mine, mine[1:]) if r["exposed"] == "1" and prev["exposed"] == "1"]
        frames = sum(int(r["frames"]) for r in steady)
        late = sum(int(r["late"]) for r in steady)
        worst = max((float(r["max_interval_ms"]) for r in steady), default=0)
        fps = statistics.mean(int(r["fps"]) for r in steady) if steady else 0
        print(f"{window}: exposed {len(exposed)}/{len(mine)} s   steady: {frames} frames, {late} late, "
              f"mean {fps:.1f} fps, worst interval {worst:.1f} ms")

    events = (log_dir / "events.log").read_text().splitlines()
    latencies = [float(m.group(1)) for line in events if (m := re.search(r"command->swap ([\d.]+) ms", line))]
    if latencies:
        print(f"command->swap: median {statistics.median(latencies):.1f} ms  max {max(latencies):.1f} ms  (n={len(latencies)}, includes covered-window outliers)")
    gaps = [float(m.group(1)) for line in events if (m := re.search(r"gap (-?[\d.]+) ms", line))]
    if gaps:
        print(f"loop gaps (ms beyond one frame): {', '.join(f'{g:.1f}' for g in gaps)}")
    covered = sum("not exposed" in line for line in events)
    if covered:
        print(f"window covered {covered} time(s) during the run")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(Path(sys.argv[1]))
