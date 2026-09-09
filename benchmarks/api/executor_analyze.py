# SPDX-License-Identifier: Apache-2.0
"""Summarize repeated executor measurements without combining their quantiles."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics


def spread(values):
    if not values or any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("invalid measurement")
    return {"min": min(values), "median": statistics.median(values), "max": max(values)}


def summarize(report):
    if report["format"] != "wirelink-executor-performance-v1" or not report["runs"]:
        raise ValueError("missing executor measurements")
    groups = defaultdict(list)
    for row in report["runs"]:
        if row["errors"] or row["calls"] <= 0 or row["profile"] != report["profile"]:
            raise ValueError("failed or mixed-profile measurement")
        groups[(row["mode"], row["producers"], row["bytes"], row["period_us"])].append(row)
    result = []
    for key, rows in sorted(groups.items()):
        if len({row["repetition"] for row in rows}) != len(rows):
            raise ValueError("duplicate repetition")
        if len({(row["abi"], row["calls"]) for row in rows}) != 1:
            raise ValueError("mixed workload")
        stage_names = set(rows[0]["stages"])
        if any(set(row["stages"]) != stage_names for row in rows):
            raise ValueError("missing profiling stages")
        item = dict(zip(["mode", "producers", "bytes", "period_us"], key))
        item.update(repetitions=len(rows), abi=rows[0]["abi"], calls=rows[0]["calls"])
        item["metrics"] = {metric: spread([row[metric] for row in rows]) for metric in [
            "wall_ns", "cpu_ns_per_call", "calls_per_second", "p50_ns", "p95_ns", "p99_ns",
            "max_ns", "dispatched", "coalesced"]}
        item["stages"] = {}
        for name in sorted(stage_names):
            stages = [row["stages"][name] for row in rows]
            active = [stage for stage in stages if stage["count"]]
            if not active:
                continue
            item["stages"][name] = {
                "count": spread([stage["count"] for stage in stages]),
                "mean_ns": spread([stage["wall_ns"] / stage["count"] for stage in active]),
                "ns_per_call": spread([stage["wall_ns"] / row["calls"] for stage, row in zip(stages, rows)]),
                "max_ns": max(stage["max_ns"] for stage in stages),
            }
        result.append(item)
    return {"format": "wirelink-executor-summary-v1", "profile": report["profile"],
            "host": report["host"], "system": report["system"],
            "binary_sha256": report["binary_sha256"], "groups": result}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(json.loads(args.report.read_text(encoding="utf-8"))),
                     indent=2, allow_nan=False))
