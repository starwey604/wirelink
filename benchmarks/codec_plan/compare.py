# SPDX-License-Identifier: Apache-2.0
"""Strict comparison of complete codec-plan host or H7 captures."""
import argparse
import json
import math
from pathlib import Path
import statistics

HOST = {(k, p, s) for k in range(12) for p in range(3) for s in range(3)}
H7 = {(m, k, p, s) for m in range(3) for k in range(1 if m == 2 else 12)
      for p in range(3) for s in range(3)}


def positive(value):
    if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError("invalid measurement")
    return value


def host(data):
    context = data["context"]
    if context.get("workload") != "wirelink-codec-plan-v1":
        raise ValueError("workload mismatch")
    groups, sizes = {}, {}
    for row in data["benchmarks"]:
        if row.get("error_occurred"):
            raise ValueError("benchmark error")
        if row.get("run_type", "iteration") != "iteration":
            continue
        key = row.get("run_name", row["name"])
        repeat = row.get("repetition_index", 0)
        samples = groups.setdefault(key, {})
        if repeat in samples:
            raise ValueError("duplicate repetition")
        size = positive(row["encoded_bytes"])
        if sizes.setdefault(key, size) != size:
            raise ValueError("encoded length changed")
        scale = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}[row["time_unit"]]
        samples[repeat] = (positive(row["cpu_time"]) * scale, positive(row["real_time"]) * scale)
    if groups.keys() != {f"k{k}/p{p}/s{s}" for k, p, s in HOST}:
        raise ValueError("incomplete host matrix")
    repeats = {tuple(sorted(v)) for v in groups.values()}
    if len(repeats) != 1 or next(iter(repeats)) != tuple(range(len(next(iter(repeats))))):
        raise ValueError("missing repetition")
    keys = ("host_name", "num_cpus", "caches", "library_version", "library_build_type")
    return ({"kind": "host", "sizes": sizes, **{k: context.get(k) for k in keys}},
            {k: {"cpu_ns": [v[0] for v in samples.values()], "wall_ns": [v[1] for v in samples.values()]}
             for k, samples in groups.items()})


def h7(text):
    begin, end, rows = None, None, {}
    for line in text.splitlines():
        at = line.find("codec_plan_")
        if at < 0:
            continue
        record, *items = line[at:].split(",")
        f = dict(item.split("=", 1) for item in items)
        if record == "codec_plan_begin_v1":
            if begin is not None or end is not None or rows:
                raise ValueError("duplicate or misplaced begin")
            begin = {"hz": positive(int(f["hz"])), "lto": int(f["lto"])}
            if f.get("barriers") != "1":
                raise ValueError("preflight capture without compiler timing barriers")
            if begin["lto"] not in (0, 1):
                raise ValueError("invalid LTO configuration")
        elif record == "codec_plan_end_v1":
            if begin is None or end is not None:
                raise ValueError("duplicate or misplaced end")
            end = f
        elif record == "codec_plan_v1":
            if begin is None or end is not None:
                raise ValueError("sample outside run")
            key = tuple(int(f[k]) for k in ("m", "k", "p", "s", "r"))
            batch = 36 if key[0] == 2 else 32
            if key in rows or int(f["n"]) != batch:
                raise ValueError("duplicate sample or changed batch")
            rows[key] = positive(int(f["cycles"])) / batch
        else:
            raise ValueError("unknown record")
    if rows.keys() != {(*k, r) for k in H7 for r in range(5)} or end != {
            "groups": "225", "samples": "1125", "result": "pass"}:
        raise ValueError("incomplete H7 capture")
    groups = {}
    for (m, k, p, s, _), cycles in rows.items():
        v = groups.setdefault(f"m{m}/k{k}/p{p}/s{s}", {"cpu_ns": [], "cycles": []})
        v["cycles"].append(cycles)
        v["cpu_ns"].append(cycles * 1e9 / begin["hz"])
    return ({"kind": "h7", **begin}, groups)


def read(path):
    text = Path(path).read_text(encoding="utf-8")
    return host(json.loads(text)) if str(path).endswith(".json") else h7(text)


def compare(a, b):
    if a[0] != b[0] or a[1].keys() != b[1].keys():
        raise ValueError("workload, machine or LTO mismatch")
    for key in sorted(a[1]):
        for metric, values in a[1][key].items():
            other = b[1][key][metric]
            if len(values) != len(other):
                raise ValueError("repetition count changed")
            x, y = statistics.median(values), statistics.median(other)
            yield key, metric, x, y, 100 * (y / x - 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before")
    parser.add_argument("after")
    args = parser.parse_args()
    try:
        rows = list(compare(read(args.before), read(args.after)))
    except (KeyError, TypeError, ValueError, OSError) as e:
        parser.error(str(e))
    print("Median per operation; negative is faster. Verify compiler/flags separately.")
    for name, metric, before, after, change in rows:
        print(f"{name:22s} {metric:8s} {before:12.2f} -> {after:12.2f} {change:+7.2f}%")


if __name__ == "__main__":
    main()
