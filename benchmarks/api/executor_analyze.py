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
    activity_fields = {"activity_enabled", "activity", "latest_budget", "endpoint_steps",
                       "sink_samples", "sink_p50_ns", "sink_p99_ns"}
    extended = any(activity_fields.intersection(row) for row in report["runs"])
    if extended:
        if any(not activity_fields.issubset(row) for row in report["runs"]):
            raise ValueError("missing activity or delivery fields")
        if len({(row["activity_enabled"], row["latest_budget"]) for row in report["runs"]}) != 1:
            raise ValueError("mixed activity instrumentation or dispatch budgets")
        counters = set(report["runs"][0]["activity"])
        for row in report["runs"]:
            if set(row["activity"]) != counters or not {"passes", "rpc_jobs", "rpc_batches"}.issubset(counters):
                raise ValueError("missing activity counters")
            if any(type(value) is not int or value < 0 for value in row["activity"].values()):
                raise ValueError("invalid activity count")
            if not row["activity_enabled"] and any(row["activity"].values()):
                raise ValueError("disabled counters contain activity")
            if row["latest_budget"] < 1 or row["sink_samples"] != row["dispatched"]:
                raise ValueError("invalid budget or incomplete sink validation")
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
        if extended:
            item.update(activity_enabled=rows[0]["activity_enabled"], latest_budget=rows[0]["latest_budget"])
            for metric in ["endpoint_steps", "sink_samples", "sink_p50_ns", "sink_p99_ns"]:
                item["metrics"][metric] = spread([row[metric] for row in rows])
            if rows[0]["activity_enabled"]:
                item["activity"] = {name: spread([row["activity"][name] for row in rows])
                                    for name in sorted(counters)}
                item["metrics"]["passes_per_call"] = spread([
                    row["activity"]["passes"] / row["calls"] for row in rows])
                if all(row["sink_samples"] for row in rows):
                    item["metrics"]["passes_per_delivery"] = spread([
                        row["activity"]["passes"] / row["sink_samples"] for row in rows])
                if all(row["activity"]["rpc_batches"] for row in rows):
                    item["metrics"]["rpc_jobs_per_batch"] = spread([
                        row["activity"]["rpc_jobs"] / row["activity"]["rpc_batches"] for row in rows])
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
