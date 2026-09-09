# SPDX-License-Identifier: Apache-2.0
"""Validate full deterministic captures, then compare freshness, gaps and RPC time."""
import argparse
import json
import math
from pathlib import Path

SCENARIOS = (
    "telemetry_only", "rpc_clean", "single_ack", "drop_ack", "backpressure",
    "drop_ack_backpressure", "saturated_wrap",
)
EXPECTED = {(name, envelope, receiver)
            for name in SCENARIOS for envelope in range(3) for receiver in range(2)}
COUNTS = ("accepted", "received", "max_age_ms", "max_arrival_age_ms", "max_gap_ms",
          "rpc_completed", "max_rpc_ms", "ack_drops", "busy")


def validate(rows):
    result = {}
    for row in rows:
        if row.get("format") != 1 or row.get("clock") != "simulated_ms" or row.get("duration_ms") != 2400:
            raise ValueError("unsupported measurement context")
        key = (row.get("scenario"), row.get("envelope"), row.get("receiver"))
        if key not in EXPECTED or key in result:
            raise ValueError(f"unexpected/duplicate case: {key}")
        if any(type(row.get(field)) is not int or row[field] < 0 for field in COUNTS):
            raise ValueError(f"invalid counter: {key}")
        if not 0 < row["received"] <= row["accepted"] <= 2400:
            raise ValueError(f"invalid delivery counts: {key}")
        if not 0 <= row["max_arrival_age_ms"] <= row["max_age_ms"] < 2400 or row["max_gap_ms"] >= 2400:
            raise ValueError(f"invalid age/gap: {key}")
        mean = row.get("mean_age_ms")
        if type(mean) not in (int, float) or not math.isfinite(mean) or not 0 <= mean <= row["max_age_ms"]:
            raise ValueError(f"invalid mean age: {key}")
        rpc = key[0] != "telemetry_only"
        if row["rpc_completed"] != (10 if rpc else 0) or not 0 <= row["max_rpc_ms"] < 1500:
            raise ValueError(f"RPC failed or incomplete: {key}")
        if not rpc and row["max_rpc_ms"] != 0:
            raise ValueError(f"unexpected RPC measurement: {key}")
        loss = key[0] in ("single_ack", "drop_ack", "drop_ack_backpressure", "saturated_wrap")
        pressure = key[0] in ("backpressure", "drop_ack_backpressure", "saturated_wrap")
        if (row["ack_drops"] > 0) != loss or (row["busy"] > 0) != pressure:
            raise ValueError(f"fault injection missing or unexpected: {key}")
        if key[0] == "single_ack" and row["ack_drops"] != 1:
            raise ValueError("single-loss case must drop one ACK per direction")
        result[key] = row
    if set(result) != EXPECTED:
        raise ValueError(f"incomplete capture: missing {EXPECTED - set(result)}")
    return result


def load(path):
    return validate([json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()])


def summary(rows, name, metric):
    return max(row[metric] for key, row in rows.items() if key[0] == name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()
    before, after = load(args.before), load(args.after)
    print("Simulated milliseconds; maxima across three envelopes and both directions.")
    print("scenario                         max age         max gap         max RPC")
    for scenario in SCENARIOS:
        pairs = [f"{summary(before, scenario, metric)} -> {summary(after, scenario, metric)}"
                 for metric in ("max_age_ms", "max_gap_ms", "max_rpc_ms")]
        print(f"{scenario:32s} {pairs[0]:15s} {pairs[1]:15s} {pairs[2]}")
