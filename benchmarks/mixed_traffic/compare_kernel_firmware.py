# SPDX-License-Identifier: Apache-2.0
"""Validate current model firmware captures; keep simulated time and cycles separate."""
import argparse
import json
from pathlib import Path
import sys

import compare_kernel as model
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from report import add_output_arguments, emit, gate_rows, gate_settings, incomparable_rows

CPU_KEYS = {(name, envelope) for name in model.SCENARIOS for envelope in range(3)}
UNITS = {**model.UNITS, "cpu_ns_per_tick": "ns", "max_tick_ns": "ns"}


def fields(line):
    parts = [part.split("=", 1) for part in line.split(",")[1:]]
    result = dict(parts)
    if len(result) != len(parts):
        raise ValueError("duplicate firmware field")
    return result


def parse(text):
    begin = end = None
    traffic, cpu = [], {}
    for line in text.splitlines():
        if not line.strip(): continue
        if line.startswith("mixed_kernel_fw_begin_v1,"):
            if begin is not None or end is not None: raise ValueError("duplicate begin/reset")
            record = fields(line)
            if set(record) != {"hz", "lto", "endpoint_bytes", "barriers"}:
                raise ValueError("changed firmware context")
            begin = {key: int(value) for key, value in record.items()}
            if not 0 < begin["hz"] <= 2**64 - 1 or begin["lto"] not in (0, 1) or not 0 < begin["endpoint_bytes"] <= 2**32 - 1 or begin["barriers"] != 1:
                raise ValueError("invalid firmware context")
        elif begin is None:
            if line.startswith("*** Booting Zephyr OS "): continue
            raise ValueError("measurement without current begin")
        elif end is not None:
            raise ValueError("data after end/reset")
        elif line.startswith('{"format":'):
            traffic.append(json.loads(line))
        elif line.startswith("mixed_kernel_cpu_v1,"):
            record = fields(line)
            if set(record) != {"scenario", "e", "n", "cycles", "max"}:
                raise ValueError("changed cycle fields")
            key = record["scenario"], int(record["e"])
            total, maximum = int(record["cycles"]), int(record["max"])
            if key not in CPU_KEYS or key in cpu or int(record["n"]) != 2400 or not 0 < maximum <= total <= min(maximum * 2400, 2**64 - 1):
                raise ValueError("invalid/duplicate cycle group")
            cpu[key] = {"cpu_ns_per_tick": total * 1e9 / begin["hz"] / 2400,
                        "max_tick_ns": maximum * 1e9 / begin["hz"]}
        elif line.startswith("mixed_kernel_fw_end_v1,"):
            end = fields(line)
        else:
            raise ValueError("unexpected firmware record")
    if begin is None or end != {"result": "pass", "traffic_rows": "42", "cpu_groups": "21"} or set(cpu) != CPU_KEYS:
        raise ValueError("incomplete current firmware capture")
    return begin, model.validate(traffic), cpu


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    add_output_arguments(parser, absolute_option="--max-regression-ns")
    args = parser.parse_args(argv)
    rel, absolute, severity = gate_settings(args)
    try:
        before, after = (parse(path.read_text()) for path in (args.before, args.after))
    except (ValueError, KeyError, TypeError, OSError) as error: parser.error(str(error))
    if before[0] != after[0]:
        reasons = ["firmware clock/layout/instrumentation context changed"]
        rows = incomparable_rows(((name, "cpu_ns_per_tick") for name in model.SCENARIOS), reasons[0],
            suite="mixed_traffic_firmware", units=UNITS, severity=severity)
    else:
        reasons = []
        pairs = [(name, metric, model.summary(before[1], name, metric), model.summary(after[1], name, metric))
                 for name in model.SCENARIOS for metric in model.SUMMARY_METRICS]
        pairs += [(f"{name}/e{envelope}", metric, before[2][name, envelope][metric], after[2][name, envelope][metric])
                  for name, envelope in sorted(CPU_KEYS) for metric in ("cpu_ns_per_tick", "max_tick_ns")]
        rows = gate_rows(pairs, suite="mixed_traffic_firmware", units=UNITS, gated={"cpu_ns_per_tick"},
                         rel=rel, absolute=absolute, severity=severity)
    print("IRQ-masked model cycles include copying and application bookkeeping; simulated ms remain separate.")
    return emit(args, rows, reasons=reasons, suite="mixed_traffic_firmware",
        title="Current kernel model firmware", before=str(args.before), after=str(args.after))


if __name__ == "__main__": raise SystemExit(main())
