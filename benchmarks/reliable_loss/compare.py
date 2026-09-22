# SPDX-License-Identifier: Apache-2.0
"""Compare two reliable-loss reports.

The benchmark is deterministic for a fixed seed and configuration, so a change
in convergence time, attempt cost or failure count is a change in the reliable
path itself, not run-to-run noise. For latency and attempt metrics a lower value
is better; for completed transactions and goodput a higher value is better. The
optional ``--max-regression-percent`` gate flags either direction.
"""

import argparse
import json
import sys
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from report import add_output_arguments, emit, gate, gate_settings

SCHEMA = "wirelink-reliable-loss-v1"
CONFIG_KEYS = ("payload", "delay_ms", "ack_timeout_ms", "max_retries",
               "samples", "warmup", "seed", "duplicate_ppm", "reorder_ppm",
               "reorder_extra_ms", "burst_start_ppm", "burst_length",
               "burst_drop_ppm")
LOWER_IS_BETTER = ("failed", "mean_ms", "p95_ms", "p99_ms", "max_ms",
                   "attempts_per_success")
HIGHER_IS_BETTER = ("completed", "goodput_bytes_per_s")

Row = namedtuple("Row", "loss item direction before after")

UNITS = {item: ("ms" if item.endswith("_ms") else
                "bytes/s" if item == "goodput_bytes_per_s" else "count")
         for item in LOWER_IS_BETTER + HIGHER_IS_BETTER}
DIRECTIONS = {item: ("lower" if item in LOWER_IS_BETTER else "higher")
              for item in LOWER_IS_BETTER + HIGHER_IS_BETTER}


def load(path):
    report = json.loads(Path(path).read_text())
    if not isinstance(report, dict) or report.get("schema") != SCHEMA:
        raise ValueError(f"{path}: not a {SCHEMA} report")
    if not report.get("cases"):
        raise ValueError(f"{path}: no cases")
    return report


def loss_label(ppm):
    return "%.2f%%" % (ppm / 10000.0)


def compare(before, after):
    for key in CONFIG_KEYS:
        if before["config"].get(key) != after["config"].get(key):
            raise ValueError(f"config mismatch: {key}")
    left = {case["loss_ppm"]: case for case in before["cases"]}
    right = {case["loss_ppm"]: case for case in after["cases"]}
    if set(left) != set(right):
        raise ValueError("loss cases differ")
    rows = []
    for ppm in sorted(left):
        for metric in LOWER_IS_BETTER + HIGHER_IS_BETTER:
            direction = "lower" if metric in LOWER_IS_BETTER else "higher"
            rows.append(Row(loss_label(ppm), metric, direction,
                            left[ppm].get(metric, 0), right[ppm].get(metric, 0)))
    return rows


def regression_percent(row):
    """Positive means worse; lower-is-better metrics grow, higher-is-better drop."""
    if row.before == 0:
        return 0.0 if row.after == 0 else (100.0 if row.direction == "lower"
                                           else -100.0)
    change = (row.after - row.before) * 100.0 / row.before
    return change if row.direction == "lower" else -change


def exceeding(rows, limit):
    if limit is None:
        return []
    return [row for row in rows if regression_percent(row) > limit]


def format_rows(rows):
    lines = [f"{'loss':>8} {'metric':<24} {'before':>12} {'after':>12} "
             f"{'delta':>12} {'worse':>8}"]
    for row in rows:
        lines.append(f"{row.loss:>8} {row.item:<24} {row.before:>12.3f} "
                     f"{row.after:>12.3f} {row.after - row.before:>+12.3f} "
                     f"{regression_percent(row):>+7.1f}%")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("before")
    parser.add_argument("after")
    add_output_arguments(parser)
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    rel, absolute, severity = gate_settings(args)
    # Absolute floors are meaningless here: this is a deterministic
    # simulated-clock benchmark, so only the relative bound applies.
    absolute = None

    try:
        before, after = load(args.before), load(args.after)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))  # invalid data: exit 2, no verdict
    try:
        compared = compare(before, after)
        reasons = []
    except ValueError as error:
        compared, reasons = [], [str(error)]
    if reasons:
        rows = [gate.incomparable("reliable_loss", case["loss_ppm"], metric,
                                  UNITS[metric], reasons[0], severity=severity)
                for case in before["cases"]
                for metric in LOWER_IS_BETTER + HIGHER_IS_BETTER]
    else:
        rows = []
        for row in compared:
            rows.append(gate.evaluate(
                row.before, row.after, suite="reliable_loss", case=row.loss,
                metric=row.item, unit=UNITS[row.item], direction=row.direction,
                rel=rel, absolute=None, severity=severity))
        print(format_rows(compared))
        hits = exceeding(compared, args.max_regression_percent)
        if hits:
            print("\nregressions above %.1f%%:" % args.max_regression_percent)
            for row in hits:
                print("  %s %s: %g -> %g" % (row.loss, row.item, row.before,
                                             row.after))
    return emit(args, rows, reasons=reasons, suite="reliable_loss",
                title="Wirelink reliable-loss benchmark comparison",
                before=str(args.before), after=str(args.after))


if __name__ == "__main__":
    sys.exit(main())
