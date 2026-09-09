# SPDX-License-Identifier: Apache-2.0
"""Compare complete v2 host or H7 RPC validation workloads; reject partial runs."""
import argparse
import json
import math
from pathlib import Path
import statistics

STAGES = ("decode", "canonical", "convert", "pipeline", "owned_decode")
GROUPS = {(k, s, b) for k in range(3) for s in range(5)
          for b in ((0, 16, 32) if k == 0 else (0, 16, 32, 256, 512))}


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
    keys = ("workload", "host_name", "num_cpus", "caches", "library_version", "library_build_type")
    return ({"kind": "host", "encoded_bytes": sizes, **{k: context.get(k) for k in keys}},
            {k: {"cpu_ns": [v[0] for v in samples.values()],
                 "wall_ns": [v[1] for v in samples.values()]} for k, samples in groups.items()})


def h7(text):
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
            raise ValueError("unsupported H7 record")
    if set(rows) != {(*k, r) for k in GROUPS for r in range(5)} or end != {
            "groups": "65", "samples": "325", "result": "pass"}:
        raise ValueError("incomplete H7 capture")
    groups = {}
    for (*key, _), cycles in rows.items():
        values = groups.setdefault(name(key), {"cpu_ns": [], "cycles": []})
        values["cycles"].append(cycles)
        values["cpu_ns"].append(cycles * 1e9 / begin["hz"])
    return ({"kind": "h7", **begin}, groups)


def read(path):
    text = path.read_text(encoding="utf-8")
    return host(json.loads(text)) if path.suffix == ".json" else h7(text)


def compare(left, right):
    if left[0] != right[0] or left[1].keys() != right[1].keys():
        raise ValueError("workload or machine changed")
    for key in sorted(left[1]):
        for metric, values in left[1][key].items():
            other = right[1][key][metric]
            if len(values) != len(other):
                raise ValueError("repetition count changed")
            before, after = statistics.median(values), statistics.median(other)
            yield key, metric, before, after, 100 * (after / before - 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()
    try:
        rows = list(compare(read(args.before), read(args.after)))
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))
    print("Median per operation; negative is faster. Verify compiler/flags separately.")
    for key, metric, before, after, change in rows:
        print(f"{key:26s} {metric:8s} {before:12.2f} -> {after:12.2f} {change:+7.2f}%")


if __name__ == "__main__":
    main()
