# SPDX-License-Identifier: Apache-2.0
"""Compare repeated measurements. Timing gates are opt-in, not universal SLAs."""
import argparse
import json
import math
from pathlib import Path
import statistics


def measurements(report):
    if report.get("format") != "wirelink-performance-v1":
        raise ValueError("unsupported performance report format")
    rows = {}
    for variant, data in report["micro"].items():
        groups = {}
        for row in data["benchmarks"]:
            if row.get("error_occurred"):
                raise ValueError(f"failed benchmark: {variant}/{row['name']}")
            if row.get("run_type", "iteration") != "iteration":
                continue
            scale = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}[row["time_unit"]]
            name = row.get("run_name", row["name"])
            for metric in ["cpu_time", "real_time", "endpoint_bytes", "runtime_arena_bytes"]:
                value = row[metric] * (scale if metric.endswith("time") else 1)
                if not math.isfinite(value) or value <= 0:
                    raise ValueError(f"invalid measurement: {variant}/{name}/{metric}")
                groups.setdefault((f"{variant}/{name}", metric), []).append(value)
        rows.update({key: statistics.median(values) for key, values in groups.items()})
    groups = {}
    for row in report["udp"]:
        for metric, value in [(key, row[key]) for key in
                             ["p50_ns", "p95_ns", "p99_ns", "max_ns", "cpu_ns_per_call"]] + [
                                 ("server_cpu_ns_per_call", row["server"]["cpu_ns_per_call"])]:
            if not math.isfinite(value) or value <= 0:
                raise ValueError(f"invalid UDP measurement: {metric}")
            groups.setdefault((f"udp/{row['payload_bytes']}B", metric), []).append(value)
    rows.update({key: statistics.median(values) for key, values in groups.items()})
    if not rows:
        raise ValueError("empty performance report")
    return rows


def compare(before, after, allow_context_mismatch=False):
    if not allow_context_mismatch:
        for key in ["system", "machine", "host", "samples", "warmup"]:
            if before["context"][key] != after["context"][key]:
                raise ValueError(f"context mismatch: {key}")
        for key in ["CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
                    "CMAKE_C_FLAGS", "CMAKE_C_FLAGS_RELEASE", "CMAKE_CXX_FLAGS",
                    "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_INTERPROCEDURAL_OPTIMIZATION"]:
            if before["context"].get("build", {}).get(key) != after["context"].get("build", {}).get(key):
                raise ValueError(f"build context mismatch: {key}")
        if before["micro"].keys() != after["micro"].keys():
            raise ValueError("benchmark variants changed")
        for variant in before["micro"]:
            for key in ["workload", "variant", "compiler", "build_type", "num_cpus", "caches"]:
                if before["micro"][variant]["context"].get(key) != after["micro"][variant]["context"].get(key):
                    raise ValueError(f"context mismatch: {variant}/{key}")
    left, right = measurements(before), measurements(after)
    if left.keys() != right.keys():
        raise ValueError(f"measurement set changed: {left.keys() ^ right.keys()}")
    return [(name, metric, old, right[(name, metric)],
             100 * (right[(name, metric)] / old - 1)) for (name, metric), old in sorted(left.items())]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--allow-context-mismatch", action="store_true")
    parser.add_argument("--max-regression-percent", type=float,
                        help="opt-in gate for micro CPU time only (same dedicated runner)")
    args = parser.parse_args()
    if args.max_regression_percent is not None and not 0 <= args.max_regression_percent < math.inf:
        parser.error("regression threshold must be finite and nonnegative")
    try:
        rows = compare(json.loads(args.before.read_text(encoding="utf-8")),
                       json.loads(args.after.read_text(encoding="utf-8")), args.allow_context_mismatch)
    except (ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print("Median of repetitions; lower is better. Time units: ns; storage units: bytes.")
    failed = False
    for name, metric, before, after, percent in rows:
        print(f"{name:42s} {metric:25s} {before:12.2f} -> {after:12.2f} {percent:+7.2f}%")
        if metric == "cpu_time" and args.max_regression_percent is not None:
            failed |= percent > args.max_regression_percent
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
