#!/usr/bin/env python3
"""Collect low-overhead CPU frequency and temperature telemetry for Y1-G3."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import time

_STOP = False

def _stop(_signum: int, _frame: object) -> None:
    global _STOP
    _STOP = True


def read_int(path: Path) -> int | None:
    try:
        return int(path.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


def read_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return None


def thermal_values() -> dict[str, int]:
    values: dict[str, int] = {}
    for hwmon in sorted(Path("/sys/class/hwmon").glob("hwmon*")):
        device = read_text(hwmon / "name") or hwmon.name
        for temp_path in sorted(hwmon.glob("temp*_input")):
            stem = temp_path.stem.removesuffix("_input")
            label = read_text(hwmon / f"{stem}_label") or stem
            value = read_int(temp_path)
            if value is not None:
                values[f"{device}:{label}"] = value
    for zone in sorted(Path("/sys/class/thermal").glob("thermal_zone*")):
        kind = read_text(zone / "type") or zone.name
        value = read_int(zone / "temp")
        if value is not None:
            values[f"thermal_zone:{kind}"] = value
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("cpu", type=int)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()
    if args.cpu < 0 or args.interval < 0.05:
        parser.error("invalid CPU or interval")

    signal.signal(signal.SIGINT, _stop)
    signal.signal(signal.SIGTERM, _stop)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    freq_root = Path(f"/sys/devices/system/cpu/cpu{args.cpu}/cpufreq")
    with args.output.open("w", encoding="utf-8", buffering=1) as stream:
        while not _STOP:
            record = {
                "utc_ns": time.time_ns(),
                "monotonic_ns": time.monotonic_ns(),
                "cpu": args.cpu,
                "pid": os.getpid(),
                "scaling_cur_freq_khz": read_int(freq_root / "scaling_cur_freq"),
                "cpuinfo_cur_freq_khz": read_int(freq_root / "cpuinfo_cur_freq"),
                "scaling_min_freq_khz": read_int(freq_root / "scaling_min_freq"),
                "scaling_max_freq_khz": read_int(freq_root / "scaling_max_freq"),
                "temperatures_millic": thermal_values(),
            }
            stream.write(json.dumps(record, sort_keys=True, ensure_ascii=False) + "\n")
            time.sleep(args.interval)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
