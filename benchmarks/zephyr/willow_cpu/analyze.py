# SPDX-License-Identifier: Apache-2.0
"""Select only complete CPU intervals inside final HIL command windows."""
import argparse
import json
import re
import statistics
from pathlib import Path


def parse(text, min_commands=1000):
    windows, samples, locks = [], [], {}
    for line in re.sub(r"\x1b\[[0-9;]*m", "", text).splitlines():
        stamp = re.search(r"\[(\d+):(\d+):(\d+)\.(\d+),(\d+)\]", line)
        if not stamp:
            continue
        h, m, s, ms, us = map(int, stamp.groups())
        time_ms = ((h * 60 + m) * 60 + s) * 1000 + ms + us / 1000
        for marker in ["wl_h7_cpu_v1,", "wl_h7_lock_v1,", "willow_wirelink_hil_device_v2,"]:
            if marker not in line:
                continue
            data = dict(p.split("=", 1) for p in line.split(marker, 1)[1].split(",") if "=" in p)
            if marker == "wl_h7_cpu_v1,":
                row = {k: int(v) for k, v in data.items()}
                row["end_ms"] = time_ms
                samples.append(row)
            elif marker == "wl_h7_lock_v1,":
                locks[int(data["w"])] = {k: int(v) for k, v in data.items()}
            elif data.get("m") == "work" and data.get("f") == "1" and int(data["cmd"]) >= min_commands:
                begin = time_ms - int(data["ems"])
                windows.append({"w": int(data["w"]), "commands": int(data["cmd"]),
                                "begin_ms": begin, "end_ms": begin + int(data["cms"])})
    selected = []
    for row in samples:
        if not any(row["end_ms"] - row["ms"] >= w["begin_ms"] + 1 and
                   row["end_ms"] <= w["end_ms"] - 1 for w in windows):
            continue
        expected = row["hz"] * row["ms"] / 1000
        if expected <= 0 or row["w"] not in locks:
            raise ValueError("incomplete CPU/lock interval")
        lock = locks[row["w"]]
        row.update(owner_percent=100 * row["owner"] / expected,
                   busy_percent=100 * row["busy"] / expected,
                   accounted_percent=100 * row["total"] / expected, locks=lock)
        if lock["on"]:
            if not lock["n"]:
                raise ValueError("lock profiling claimed on without any samples")
            row.update(lock_wait_ns=lock["wait"] * 1e9 / row["hz"] / lock["n"],
                       lock_hold_ns=lock["hold"] * 1e9 / row["hz"] / lock["n"],
                       lock_hold_percent=100 * lock["hold"] / expected)
        selected.append(row)
    if not windows or not selected:
        raise ValueError("no complete CPU intervals inside measured HIL traffic windows")
    metrics = ["owner_percent", "busy_percent", "accounted_percent"]
    if selected[0]["locks"]["on"]:
        metrics += ["lock_wait_ns", "lock_hold_ns", "lock_hold_percent"]
    return {"format": "wirelink-h7-cpu-v1", "windows": windows, "samples": selected,
            "summary": {metric: {"min": min(r[metric] for r in selected),
                                 "median": statistics.median(r[metric] for r in selected),
                                 "max": max(r[metric] for r in selected)} for metric in metrics},
            # Maxima are since boot, NOT per interval maxima or p99.
            "boot_lock_wait_max_cycles": max((r["waitmax"] for r in locks.values()), default=0),
            "boot_lock_hold_max_cycles": max((r["holdmax"] for r in locks.values()), default=0)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path, nargs="+", help="Segments from the SAME boot, in order")
    parser.add_argument("--min-commands", type=int, default=1000)
    args = parser.parse_args()
    print(json.dumps(parse("\n".join(path.read_text(encoding="utf-8") for path in args.log), args.min_commands), indent=2, allow_nan=False))
