# SPDX-License-Identifier: Apache-2.0
"""Validate complete mixed-traffic H7 captures; do not confuse simulated age with CPU time."""
import argparse
import json
from pathlib import Path
from statistics import median

from compare import SCENARIOS, validate

CPU_KEYS = {(name, envelope) for name in SCENARIOS for envelope in range(3)}
RETRY_KEYS = {(mode, envelope, claim, size, repeat)
              for mode in range(4) for envelope in range(3) for claim in range(2)
              for size in (32, 512, 2048) for repeat in range(5)}


def fields(line):
    pairs = [item.split("=", 1) for item in line.split(",")[1:]]
    result = dict(pairs)
    if len(result) != len(pairs):
        raise ValueError("duplicate field")
    return result


def cycles(row, n):
    total, maximum = int(row["cycles"]), int(row["max"])
    if int(row["n"]) != n or not 0 < maximum <= total <= maximum * n:
        raise ValueError("invalid cycle count or workload")


def parse(text):
    begin = end = None
    traffic, cpu, retry = [], {}, {}
    for line in text.splitlines():
        if not line.strip():
            continue
        if begin is None and (line.startswith(("*** Booting Zephyr OS ", "SEGGER J-Link V"))
                              or line == "Process: JLinkExe"):
            continue
        if end is not None:
            raise ValueError("data after end marker")
        if line.startswith("mixed_h7_begin_v1,"):
            if begin is not None:
                raise ValueError("duplicate boot")
            begin = fields(line)
            if (set(begin) != {"hz", "lto", "coexist", "endpoint_bytes", "barriers"}
                    or begin["lto"] != "1" or begin["barriers"] != "1"
                    or begin["coexist"] not in ("0", "1")
                    or int(begin["hz"]) <= 0 or int(begin["endpoint_bytes"]) <= 0):
                raise ValueError("unexpected measurement context")
            continue
        if begin is None:
            raise ValueError("record before boot marker")
        if line.startswith("{"):
            traffic.append(json.loads(line))
        elif line.startswith("mixed_cpu_v1,"):
            row = fields(line)
            if set(row) != {"scenario", "e", "n", "cycles", "max"}:
                raise ValueError("unexpected CPU record")
            key = (row["scenario"], int(row["e"]))
            if key not in CPU_KEYS or key in cpu:
                raise ValueError("unexpected/duplicate CPU group")
            cycles(row, 2400)
            cpu[key] = row
        elif line.startswith("mixed_retry_v1,"):
            row = fields(line)
            if set(row) != {"m", "e", "claim", "b", "r", "n", "cycles", "max"}:
                raise ValueError("unexpected retry record")
            key = tuple(int(row[field]) for field in ("m", "e", "claim", "b", "r"))
            if key not in RETRY_KEYS or key in retry:
                raise ValueError("unexpected/duplicate retry sample")
            cycles(row, 16)
            retry[key] = row
        elif line.startswith("mixed_h7_end_v1,"):
            end = fields(line)
        else:
            raise ValueError(f"unrecognized output: {line!r}")
    if (begin is None or end != {"result": "pass", "traffic_rows": "42",
                               "cpu_groups": "21", "retry_samples": "360"}
            or set(cpu) != CPU_KEYS or set(retry) != RETRY_KEYS):
        raise ValueError("incomplete capture")
    return {"context": begin, "traffic": validate(traffic), "cpu": cpu, "retry": retry}


def summarize(capture):
    hz = int(capture["context"]["hz"])
    return {
        "context": capture["context"],
        "traffic": list(capture["traffic"].values()),
        "cpu": [{"scenario": name, "envelope": envelope,
                 "us_per_two_endpoint_tick": int(row["cycles"]) / 2400 / hz * 1e6,
                 "max_tick_us": int(row["max"]) / hz * 1e6}
                for (name, envelope), row in sorted(capture["cpu"].items())],
        "retry": [{"mode": mode, "envelope": envelope, "claim": claim, "bytes": size,
                   "median_us": median(int(capture["retry"][mode, envelope, claim, size, r]["cycles"])
                                       / 16 / hz * 1e6 for r in range(5))}
                  for mode, envelope, claim, size in sorted({key[:4] for key in RETRY_KEYS})],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, nargs="+")
    args = parser.parse_args()
    captures = {str(path): parse(path.read_text(encoding="utf-8")) for path in args.captures}
    contexts = [{key: value for key, value in cap["context"].items() if key != "coexist"}
                for cap in captures.values()]
    if any(context != contexts[0] for context in contexts):
        raise ValueError("incompatible contexts")
    print(json.dumps({path: summarize(cap) for path, cap in captures.items()}, indent=2))
