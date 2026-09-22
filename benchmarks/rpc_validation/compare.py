# SPDX-License-Identifier: Apache-2.0
"""Compare complete v2 host or FIRMWARE RPC validation workloads; reject partial runs."""
import argparse
import json
import math
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from report import (add_output_arguments, emit, gate_settings, gate_rows,
                    incomparable_rows, manifest)

STAGES = ("decode", "canonical", "convert", "pipeline", "owned_decode")
GROUPS = {(k, s, b) for k in range(3) for s in range(5)
          for b in ((0, 16, 32) if k == 0 else (0, 16, 32, 256, 512))}
UNITS = {"cpu_ns": "ns", "wall_ns": "ns", "cycles": "cycles"}
GATED = {"cpu_ns"}


def name(key):
    k, s, b = key
    return f"{STAGES[s]}/k{k}/b{b}"


def positive(value):
    if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f"invalid measurement: {value}")
    return value


def host(report):
    context = report["context"]
    if context.get("workload") != "wirelink-rpc-validation-v2":
        raise ValueError("unsupported workload")
    groups, sizes = {}, {}
    for row in report["benchmarks"]:
        if row.get("error_occurred"):
            raise ValueError("benchmark error")
        if row.get("run_type", "iteration") != "iteration":
            continue
        key = row.get("run_name", row["name"])
        samples = groups.setdefault(key, {})
        repeat = row.get("repetition_index", 0)
        if repeat in samples:
            raise ValueError("duplicate repetition")
        scale = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}[row["time_unit"]]
        samples[repeat] = (positive(row["cpu_time"]) * scale, positive(row["real_time"]) * scale)
        size = positive(row["encoded_bytes"])
        if sizes.setdefault(key, size) != size:
            raise ValueError("encoded length changed")
    if groups.keys() != {name(k) for k in GROUPS}:
        raise ValueError("incomplete host matrix")
    repeats = {tuple(sorted(v)) for v in groups.values()}
    if len(repeats) != 1 or next(iter(repeats)) != tuple(range(len(next(iter(repeats))))):
        raise ValueError("incomplete repetitions")
    keys = ("workload", "host_name", "num_cpus", "caches", "library_version",
            "library_build_type", "benchmark_source_hash")
    return ({"kind": "host", "encoded_bytes": sizes, **{k: context.get(k) for k in keys}},
            {k: {"cpu_ns": [v[0] for v in samples.values()],
                 "wall_ns": [v[1] for v in samples.values()]} for k, samples in groups.items()})


def firmware(text):
    begin, end, rows = None, None, {}
    for line in text.splitlines():
        at = line.find("rpc_validation_")
        if at < 0:
            continue
        record, *items = line[at:].split(",")
        fields = dict(item.split("=", 1) for item in items)
        if record == "rpc_validation_begin_v2":
            if begin is not None or rows or end is not None:
                raise ValueError("duplicate or misplaced begin")
            begin = {k: int(fields[k]) for k in ("hz", "batch")}
            positive(begin["hz"])
            if begin["batch"] != 32:
                raise ValueError("batch changed")
        elif record == "rpc_validation_end_v2":
            if begin is None or end is not None:
                raise ValueError("duplicate or misplaced end")
            end = fields
        elif record == "rpc_validation_v2":
            if begin is None or end is not None:
                raise ValueError("measurement outside run")
            key = tuple(int(fields[k]) for k in ("k", "s", "b", "r"))
            if key in rows or int(fields["n"]) != 32:
                raise ValueError("duplicate sample or changed batch")
            rows[key] = positive(int(fields["cycles"])) / 32
        else:
            raise ValueError("unsupported firmware record")
    if set(rows) != {(*k, r) for k in GROUPS for r in range(5)} or end != {
            "groups": "65", "samples": "325", "result": "pass"}:
        raise ValueError("incomplete firmware capture")
    groups = {}
    for (*key, _), cycles in rows.items():
        values = groups.setdefault(name(key), {"cpu_ns": [], "cycles": []})
        values["cycles"].append(cycles)
        values["cpu_ns"].append(cycles * 1e9 / begin["hz"])
    return ({"kind": "firmware", **begin}, groups)


def read(path):
    text = path.read_text(encoding="utf-8")
    return host(json.loads(text)) if path.suffix == ".json" else firmware(text)


def compare(left, right):
    left_context, right_context = dict(left[0]), dict(right[0])
    reason = manifest.source_hash_reason(left_context, right_context)
    left_context.pop("benchmark_source_hash", None)
    right_context.pop("benchmark_source_hash", None)
    if reason:
        raise ValueError(reason)
    if left_context != right_context or left[1].keys() != right[1].keys():
        raise ValueError("workload or machine changed")
    for key in sorted(left[1]):
        for metric, values in left[1][key].items():
            other = right[1][key][metric]
            if len(values) != len(other):
                raise ValueError("repetition count changed")
            before, after = statistics.median(values), statistics.median(other)
            yield key, metric, before, after, 100 * (after / before - 1)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    add_output_arguments(parser, absolute_option="--max-regression-ns",
                         absolute_help="absolute gate threshold in ns; a "
                                       "timing metric fails only when both "
                                       "thresholds are exceeded")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    rel, absolute, severity = gate_settings(args)
    try:
        left, right = read(args.before), read(args.after)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))  # invalid data: exit 2, no verdict
    try:
        pairs = list(compare(left, right))
        reasons = []
    except ValueError as error:
        pairs, reasons = [], [str(error)]
    if reasons:
        rows = incomparable_rows(((name, metric) for name, metrics in left[1].items()
                                  for metric in metrics),
                                 reasons[0], suite="rpc_validation",
                                 units=UNITS, severity=severity)
    else:
        rows = gate_rows(((name, metric, before, after)
                          for name, metric, before, after, _ in pairs),
                         suite="rpc_validation", units=UNITS,
                         gated=GATED, rel=rel, absolute=absolute,
                         severity=severity)
        print("Median per operation; negative is faster. Verify compiler/flags separately.")
        for key, metric, before, after, change in pairs:
            print(f"{key:26s} {metric:8s} {before:12.2f} -> {after:12.2f} {change:+7.2f}%")
    return emit(args, rows, reasons=reasons, suite="rpc_validation",
                title="Wirelink RPC validation benchmark comparison",
                before=str(args.before), after=str(args.after))


if __name__ == "__main__":
    raise SystemExit(main())
