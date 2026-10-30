# SPDX-License-Identifier: Apache-2.0
"""Compare current fixed-contract kernel API diagnostics; CPU gates are opt-in."""
import argparse
import json
import math
from pathlib import Path
import statistics
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from report import add_output_arguments, emit, gate_settings, gate_rows, incomparable_rows, manifest
UNITS = {"cpu_ns": "ns", "wall_ns": "ns", "endpoint_bytes": "bytes", "fixture_bytes": "bytes"}
CASES = {"kernel_idle_pair": (0, 0), "kernel_rpc_4x2": (8, 0), "kernel_rejected_4x2": (8, 0),
         **{f"kernel_sample_pair/{size}": (0, 2) for size in (32, 120, 512, 1024)}}


def positive(value):
    if type(value) not in (int, float) or not math.isfinite(value) or value <= 0:
        raise ValueError("invalid positive measurement")
    return value


def host(report):
    context = report["context"]
    if context.get("workload") != "wirelink-kernel-api-v1" or context.get("clock") != "caller_manual_tick":
        raise ValueError("not a current kernel API workload; old API captures are not comparable")
    groups, layout = {}, None
    for row in report["benchmarks"]:
        if row.get("error_occurred"):
            raise ValueError("failed benchmark")
        if row.get("run_type", "iteration") != "iteration":
            continue
        name = row.get("run_name", row["name"])
        if name not in CASES:
            raise ValueError("unknown kernel API case")
        counters = (row["calls_per_op"], row["samples_per_op"])
        if any(type(value) not in (int, float) for value in counters) or counters != CASES[name]:
            raise ValueError("changed call/sample count")
        current_layout = (positive(row["endpoint_bytes"]), positive(row["fixture_bytes"]))
        if current_layout[1] < 2 * current_layout[0] or (layout is not None and layout != current_layout):
            raise ValueError("inconsistent fixture layout")
        layout = current_layout
        scale = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}[row["time_unit"]]
        repeat = row.get("repetition_index", 0)
        samples = groups.setdefault(name, {})
        if type(repeat) is not int or repeat < 0 or repeat in samples:
            raise ValueError("duplicate/invalid repetition")
        samples[repeat] = {"cpu_ns": positive(row["cpu_time"]) * scale,
                           "wall_ns": positive(row["real_time"]) * scale,
                           "endpoint_bytes": current_layout[0], "fixture_bytes": current_layout[1]}
    counts = {tuple(sorted(samples)) for samples in groups.values()}
    if not groups or len(counts) != 1 or next(iter(counts)) != tuple(range(len(next(iter(counts))))):
        raise ValueError("incomplete repetitions")
    keys = ("workload", "clock", "host_name", "num_cpus", "caches", "library_version",
            "library_build_type", "benchmark_source_hash")
    return ({key: context.get(key) for key in keys},
            {name: {metric: [row[metric] for row in samples.values()] for metric in UNITS}
             for name, samples in groups.items()})


def load(path):
    return host(json.loads(Path(path).read_text()))


def compare(before, after):
    left, right = dict(before[0]), dict(after[0])
    reason = manifest.source_hash_reason(left, right)
    left.pop("benchmark_source_hash", None)
    right.pop("benchmark_source_hash", None)
    if reason or left != right or before[1].keys() != after[1].keys():
        raise ValueError(reason or "workload/machine/measurement-set mismatch")
    rows = []
    for name, metrics in before[1].items():
        for metric, values in metrics.items():
            other = after[1][name][metric]
            if len(values) != len(other):
                raise ValueError("repetition count mismatch")
            rows.append((name, metric, statistics.median(values), statistics.median(other)))
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    add_output_arguments(parser, absolute_option="--max-regression-ns",
                         absolute_help="optional absolute CPU-time gate in nanoseconds")
    args = parser.parse_args(argv)
    rel, absolute, severity = gate_settings(args)
    try:
        before, after = load(args.before), load(args.after)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))
    try:
        pairs = compare(before, after)
        rows = gate_rows(pairs, suite="api", units=UNITS, gated={"cpu_ns"},
                         rel=rel, absolute=absolute, severity=severity)
        reasons = []
        for name, metric, left, right in pairs:
            print(f"{name:28s} {metric:16s} {left:.2f} -> {right:.2f}")
    except ValueError as error:
        reasons = [str(error)]
        rows = incomparable_rows(((name, metric) for name, metrics in before[1].items() for metric in metrics),
                                 reasons[0], suite="api", units=UNITS, severity=severity)
    return emit(args, rows, reasons=reasons, suite="api", title="Fixed-contract kernel API comparison",
                before=str(args.before), after=str(args.after))


if __name__ == "__main__":
    raise SystemExit(main())
