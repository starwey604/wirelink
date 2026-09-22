# SPDX-License-Identifier: Apache-2.0
"""Validate and compare Google Benchmark JSON or complete firmware console captures."""
import argparse
import itertools
import json
import math
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from report import (add_output_arguments, emit, gate_settings, gate_rows,
                    incomparable_rows)

MODES = ("spare", "exact", "overlap", "send", "claim", "busy3", "retry3")
UNITS = {"cpu_ns": "ns", "wall_ns": "ns", "cycles": "cycles"}
GATED = {"cpu_ns"}


def positive(value):
    if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f"invalid positive measurement: {value}")
    return value


def host(report):
    context = report["context"]
    if context.get("workload") != "wirelink-framing-v1":
        raise ValueError("unsupported host workload")
    groups = {}
    for row in report["benchmarks"]:
        if row.get("error_occurred"):
            raise ValueError(f"benchmark failed: {row['name']}")
        if row.get("run_type", "iteration") != "iteration":
            continue
        name = row.get("run_name", row["name"])
        mode = name.split("/")[0]
        if mode not in MODES:
            raise ValueError(f"unknown mode: {name}")
        expected_calls = 4 if mode in ("busy3", "retry3") else 1 if mode in ("send", "claim") else 0
        if row["sink_calls_per_op"] != expected_calls or row["context_bytes"] != 896:
            raise ValueError(f"workload invariant changed: {name}")
        scale = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}[row["time_unit"]]
        repeat = row.get("repetition_index", 0)
        samples = groups.setdefault(name, {})
        if repeat in samples:
            raise ValueError(f"duplicate repetition: {name}/{repeat}")
        samples[repeat] = (positive(row["cpu_time"]) * scale, positive(row["real_time"]) * scale)
    if not groups:
        raise ValueError("no iteration measurements")
    counts = {tuple(sorted(samples)) for samples in groups.values()}
    if len(counts) != 1 or next(iter(counts)) != tuple(range(len(next(iter(counts))))):
        raise ValueError("incomplete repetitions")
    keys = ("workload", "host_name", "num_cpus", "caches", "library_version", "library_build_type")
    return ({"kind": "host", **{key: context.get(key) for key in keys}},
            {name: {"cpu_ns": [v[0] for v in samples.values()],
                    "wall_ns": [v[1] for v in samples.values()]} for name, samples in groups.items()})


def firmware(text):
    begin, end, rows = None, None, {}
    for line in text.splitlines():
        # Boot banners may precede the first record on the same RTT line.
        start = line.find("framing_cpu_")
        if start < 0:
            continue
        kind, *items = line[start:].split(",")
        fields = dict(item.split("=", 1) for item in items)
        if kind == "framing_cpu_begin_v1":
            if begin is not None or rows or end is not None:
                raise ValueError("duplicate or misplaced begin")
            begin = {key: int(fields[key]) for key in ("hz", "context_bytes", "irq_masked_batch")}
            positive(begin["hz"])
            if begin["context_bytes"] != 896 or begin["irq_masked_batch"] != 32:
                raise ValueError("firmware workload invariant changed")
        elif kind == "framing_cpu_end_v1":
            if begin is None or end is not None:
                raise ValueError("duplicate or misplaced end")
            end = fields
        elif kind == "framing_cpu_v1":
            if begin is None or end is not None:
                raise ValueError("measurement outside begin/end")
            key = (fields["mode"], *(int(fields[k]) for k in ("e", "i", "p", "b", "r")))
            if key in rows or int(fields["n"]) != 32:
                raise ValueError("duplicate measurement or changed batch")
            rows[key] = positive(int(fields["cycles"])) / 32
        else:
            raise ValueError(f"unknown framing record: {kind}")
    expected = set(itertools.product(MODES, (0, 1), (0,), range(3), (32, 120, 512, 2048), range(5)))
    if set(rows) != expected or end != {"result": "pass", "groups": "168", "samples": "840"}:
        raise ValueError("incomplete firmware matrix (lost console data, reset or assertion failure)")
    groups = {}
    for (mode, envelope, integrity, pattern, size, _), cycles in rows.items():
        name = f"{mode}/e{envelope}/i{integrity}/p{pattern}/b{size}"
        samples = groups.setdefault(name, {"cycles": [], "cpu_ns": []})
        samples["cycles"].append(cycles)
        samples["cpu_ns"].append(cycles * 1e9 / begin["hz"])
    return ({"kind": "firmware", **begin}, groups)


def read(path):
    text = path.read_text(encoding="utf-8", errors="strict")
    return host(json.loads(text)) if path.suffix == ".json" else firmware(text)


def compare(left, right):
    if left[0] != right[0]:
        raise ValueError("context mismatch; do not compare different workloads/machines")
    if left[1].keys() != right[1].keys():
        raise ValueError("measurement set changed")
    rows = []
    for name, metrics in sorted(left[1].items()):
        other = right[1][name]
        if metrics.keys() != other.keys():
            raise ValueError("metric set changed")
        for metric, samples in metrics.items():
            if len(samples) != len(other[metric]):
                raise ValueError(f"repetition count changed: {name}")
            before, after = statistics.median(samples), statistics.median(other[metric])
            rows.append((name, metric, before, after, 100 * (after / before - 1)))
    return rows


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
        # Both sides parsed, but they are not comparable (workload, machine
        # or matrix changed): report incomparable, never a regression.
        pairs, reasons = [], [str(error)]
    if reasons:
        items = [(name, metric) for name, metrics in left[1].items()
                 for metric in metrics]
        rows = incomparable_rows(items, reasons[0], suite="framing",
                                 units=UNITS, severity=severity)
    else:
        rows = gate_rows(((name, metric, before, after)
                          for name, metric, before, after, _ in pairs),
                         suite="framing", units=UNITS, gated=GATED, rel=rel,
                         absolute=absolute, severity=severity)
        print("Median per operation; negative change is faster. Same compiler/flags must be verified separately.")
        for name, metric, before, after, change in pairs:
            print(f"{name:32s} {metric:8s} {before:12.2f} -> {after:12.2f} {change:+7.2f}%")
    return emit(args, rows, reasons=reasons, suite="framing",
                title="Wirelink framing benchmark comparison",
                before=str(args.before), after=str(args.after))


if __name__ == "__main__":
    raise SystemExit(main())
