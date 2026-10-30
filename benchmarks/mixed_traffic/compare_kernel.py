# SPDX-License-Identifier: Apache-2.0
"""Validate current kernel model captures; report simulated-time changes only."""
import argparse
import json
import math
import sys
from pathlib import Path

SCENARIOS = ("telemetry_only", "rpc_clean", "drop_request", "drop_response",
             "backpressure", "loss_backpressure", "saturated_wrap")
EXPECTED = {(name, envelope, receiver) for name in SCENARIOS
            for envelope in range(3) for receiver in range(2)}
COUNTS = ("accepted", "received", "max_age_ms", "max_arrival_age_ms", "max_gap_ms",
          "rpc_completed", "rpc_handled", "rpc_timed_out", "max_rpc_ms",
          "dropped_requests", "dropped_responses", "busy")


def validate(rows):
    result = {}
    for row in rows:
        if row.get("format") != 2 or row.get("clock") != "simulated_ms" or row.get("duration_ms") != 2400:
            raise ValueError("unsupported kernel model context; old ARQ captures are not comparable")
        key = (row.get("scenario"), row.get("envelope"), row.get("receiver"))
        if key not in EXPECTED or key in result:
            raise ValueError(f"unexpected or duplicate case: {key}")
        if any(type(row.get(field)) is not int or row[field] < 0 for field in COUNTS):
            raise ValueError(f"invalid counter: {key}")
        if not 0 < row["received"] <= row["accepted"] <= 2400:
            raise ValueError("invalid delivery counts")
        if not 0 <= row["max_arrival_age_ms"] <= row["max_age_ms"] < 2400 or row["max_gap_ms"] >= 2400:
            raise ValueError("invalid age or gap")
        mean = row.get("mean_age_ms")
        if type(mean) not in (int, float) or not math.isfinite(mean) or not 0 <= mean <= row["max_age_ms"]:
            raise ValueError("invalid mean age")
        name = key[0]
        request_loss = name in ("drop_request", "loss_backpressure", "saturated_wrap")
        response_loss = name in ("drop_response", "loss_backpressure", "saturated_wrap")
        pressure = name in ("backpressure", "loss_backpressure", "saturated_wrap")
        rpc = name != "telemetry_only"
        if (row["dropped_requests"] != int(request_loss) or row["dropped_responses"] != int(response_loss)
                or (row["busy"] > 0) != pressure):
            raise ValueError("fault injection missing or unexpected")
        if (row["rpc_completed"] != (10 if rpc else 0)
                or row["rpc_handled"] != (10 if rpc else 0) + 2 * response_loss
                or row["rpc_timed_out"] != 2 * (request_loss + response_loss)
                or not 0 <= row["max_rpc_ms"] < 240
                or (not rpc and row["max_rpc_ms"] != 0)):
            raise ValueError("RPC completion, duplicate execution or bounded retry invariant failed")
        result[key] = row
    if set(result) != EXPECTED:
        raise ValueError("incomplete kernel model capture")
    return result


def load(path):
    return validate([json.loads(line) for line in path.read_text().splitlines()])


SUMMARY_METRICS = ("max_age_ms", "max_gap_ms", "max_rpc_ms")
UNITS = {metric: "ms" for metric in SUMMARY_METRICS}


def summary(rows, name, metric):
    return max(row[metric] for key, row in rows.items() if key[0] == name)


def main(argv=None):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from report import add_output_arguments, emit, gate_settings, gate_rows
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    add_output_arguments(parser, absolute_option="--max-regression-ms",
        absolute_help="absolute threshold in simulated milliseconds")
    args = parser.parse_args(argv)
    rel, absolute, severity = gate_settings(args)
    try:
        before, after = load(args.before), load(args.after)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))
    pairs = [(name, metric, summary(before, name, metric), summary(after, name, metric))
        for name in SCENARIOS for metric in SUMMARY_METRICS]
    rows = gate_rows(pairs, suite="mixed_traffic", units=UNITS,
        gated=SUMMARY_METRICS, rel=rel, absolute=absolute, severity=severity)
    print("Simulated milliseconds only; validate identical workload sources and build flags separately.")
    for name, metric, left, right in pairs:
        print(f"{name} {metric} {left}->{right}")
    return emit(args, rows, suite="mixed_traffic", title="Explicit application retry model",
        before=str(args.before), after=str(args.after))


if __name__ == "__main__":
    raise SystemExit(main())
