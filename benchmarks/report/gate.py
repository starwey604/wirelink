# SPDX-License-Identifier: Apache-2.0
"""Gate evaluation and exit codes shared by every benchmark comparison.

The rules follow benchmarks/SPEC.md:

- every gated metric carries a direction (``lower``, ``higher``, ``exact``),
  a relative threshold, an absolute threshold, a testbed identity and a
  severity (``info``, ``warn``, ``fail``);
- a timing metric regresses only when the relative *and* the absolute
  threshold are exceeded, so 1 ns -> 2 ns (100%) does not fail by itself;
- exit codes are three-valued: 0 pass, 1 regression, 2 invalid/incomparable.
  Exit 2 never implies a regression verdict.
"""
import math

EXIT_OK = 0
EXIT_REGRESSION = 1
EXIT_INVALID = 2

DIRECTIONS = ("lower", "higher", "exact")
SEVERITIES = ("info", "warn", "fail")
STATUSES = ("pass", "warn", "fail", "incomparable")


def _validate(name, value, allowed):
    if value not in allowed:
        raise ValueError(f"{name} must be one of {allowed}, got {value!r}")


def _worse_relative(before, after, direction):
    """Relative change toward the worse side; inf when growing from zero."""
    delta = after - before
    if direction == "higher":
        delta = -delta
    if before == 0:
        return 0.0 if delta == 0 else math.inf
    return delta / abs(before)


def evaluate(before, after, *, suite="", case="", metric="", unit="",
             direction="lower", rel=None, absolute=None, severity="fail",
             status=None):
    """Build one comparison result row.

    ``rel`` and ``absolute`` are the gate thresholds; either may be None to
    disable that dimension. With both set (the timing default) a metric
    regresses only when both are exceeded. ``status`` forces a status, used
    for ``incomparable`` rows that carry no numeric verdict.
    """
    _validate("direction", direction, DIRECTIONS)
    _validate("severity", severity, SEVERITIES)
    if rel is not None and (not math.isfinite(rel) or rel < 0):
        raise ValueError(f"rel threshold must be finite and nonnegative: {rel}")
    if absolute is not None and (not math.isfinite(absolute) or absolute < 0):
        raise ValueError(f"absolute threshold must be nonnegative: {absolute}")
    if status is not None:
        _validate("status", status, STATUSES)
    if status is None and (not math.isfinite(before) or not math.isfinite(after)):
        status = "incomparable"

    delta = after - before
    worse_abs = -delta if direction == "higher" else delta
    worse_rel = _worse_relative(before, after, direction)

    if status is None:
        if direction == "exact":
            triggered = delta != 0
        else:
            checks = []
            if rel is not None:
                checks.append(worse_rel > rel)
            if absolute is not None:
                checks.append(worse_abs > absolute)
            triggered = all(checks) if checks else False
        status = ("fail" if severity == "fail" else "warn") if triggered else "pass"

    return {"suite": suite, "case": case, "metric": metric, "unit": unit,
            "direction": direction, "before": before, "after": after,
            "delta_abs": delta, "delta_rel": (delta / abs(before) if before else
                                              (0.0 if delta == 0 else math.inf)),
            "threshold": {"rel": rel, "abs": absolute},
            "severity": severity, "status": status}


def incomparable(suite, case, metric, unit, reason, severity="fail"):
    """A row that cannot carry a numeric verdict (workload/testbed mismatch)."""
    row = evaluate(0.0, 0.0, suite=suite, case=case, metric=metric, unit=unit,
                   direction="exact", severity=severity, status="incomparable")
    row["reason"] = reason
    return row


def verdict(rows):
    """Aggregate counts per status."""
    counts = {name: 0 for name in STATUSES}
    for row in rows:
        counts[row["status"]] += 1
    return counts


def exit_code(rows):
    """0 pass, 1 regression, 2 invalid. Invalid dominates: an incomparable
    run yields no trustworthy verdict, so it must not read as a regression
    or as a pass."""
    statuses = {row["status"] for row in rows}
    if "incomparable" in statuses:
        return EXIT_INVALID
    if "fail" in statuses:
        return EXIT_REGRESSION
    return EXIT_OK
