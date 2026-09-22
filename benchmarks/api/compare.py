# SPDX-License-Identifier: Apache-2.0
"""Compare repeated measurements. Timing gates are opt-in, not universal SLAs."""
import argparse
import json
import math
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from report import (add_output_arguments, emit, gate_settings, gate_rows,
                    incomparable_rows, manifest)

# Micro CPU time is the only gated metric: the gate is opt-in, never a
# universal SLA. UDP percentiles and byte counts are reported, not gated.
GATED = {"cpu_time"}


def unit_of(metric):
    if metric.endswith("_bytes"):
        return "bytes"
    return "ns"


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
        # Reports stamped from different workload sources are incomparable,
        # never regressing (SPEC benchmark_source_hash rule).
        reason = manifest.source_hash_reason(before["context"], after["context"])
        if reason:
            raise ValueError(reason)
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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--allow-context-mismatch", action="store_true")
    add_output_arguments(parser, absolute_option="--max-regression-ns",
                         absolute_help="absolute gate threshold in ns; micro "
                                       "CPU time fails only when both "
                                       "thresholds are exceeded")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    rel, absolute, severity = gate_settings(args)
    try:
        before = json.loads(args.before.read_text(encoding="utf-8"))
        after = json.loads(args.after.read_text(encoding="utf-8"))
        left = measurements(before)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))  # invalid data: exit 2, no verdict
    try:
        pairs = compare(before, after, args.allow_context_mismatch)
        reasons = []
    except (ValueError, KeyError, TypeError) as error:
        # Both reports parsed, but their contexts differ: report
        # incomparable instead of a regression verdict.
        pairs, reasons = [], [str(error)]
    units = {metric: unit_of(metric) for _, metric in left}
    if reasons:
        rows = incomparable_rows(left, reasons[0], suite="api", units=units,
                                 severity=severity)
    else:
        rows = gate_rows(((name, metric, old, new)
                          for name, metric, old, new, _ in pairs),
                         suite="api", units=units, gated=GATED, rel=rel,
                         absolute=absolute, severity=severity)
        print("Median of repetitions; lower is better. Time units: ns; storage units: bytes.")
        for name, metric, before_value, after_value, percent in pairs:
            print(f"{name:42s} {metric:25s} {before_value:12.2f} -> "
                  f"{after_value:12.2f} {percent:+7.2f}%")
    return emit(args, rows, reasons=reasons, suite="api",
                title="Wirelink API benchmark comparison",
                before=str(args.before), after=str(args.after))


if __name__ == "__main__":
    raise SystemExit(main())
