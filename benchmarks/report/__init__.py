# SPDX-License-Identifier: Apache-2.0
"""Shared benchmark comparison helpers; see benchmarks/SPEC.md.

Every benchmarks/*/compare.py exposes the same surface through
``add_output_arguments`` + ``gate_settings`` + ``emit``: uniform output
flags, one threshold model, and the SPEC three-state exit code (0 pass,
1 regression with severity fail, 2 invalid/incomparable).
"""
import argparse
import math

from . import gate, junit, markdown, schema, manifest  # noqa: F401

__all__ = ["gate", "junit", "markdown", "schema", "manifest", "GATE_MODES",
           "add_output_arguments", "gate_settings", "gate_rows",
           "incomparable_rows", "emit"]

GATE_MODES = ("auto", "warn", "fail", "off")


def _threshold(value):
    try:
        number = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError(f"threshold must be a number: {value!r}")
    if not math.isfinite(number) or number < 0:
        raise argparse.ArgumentTypeError(
            f"threshold must be finite and nonnegative: {value!r}")
    return number


def add_output_arguments(parser, *, absolute_option=None,
                         absolute_help="absolute gate threshold in the "
                                       "metric's own unit"):
    """Add the uniform comparison surface to a compare.py parser.

    ``--gate auto`` (default) blocks only when a threshold is given, so
    existing invocations without flags keep reporting; ``warn`` annotates
    regressions without failing; ``fail`` states blocking intent;
    ``off`` ignores thresholds entirely. Invalid data always exits 2.
    ``absolute_option`` adds the suite's absolute floor (e.g.
    ``--max-regression-ns`` for timing, ``--max-growth-bytes`` for size);
    it is applied only to metrics whose unit matches.
    """
    parser.add_argument("--json-out", default=None,
                        help="write comparison.json (machine readable)")
    parser.add_argument("--markdown-out", default=None,
                        help="write summary.md for a step summary or PR comment")
    parser.add_argument("--junit-out", default=None,
                        help="write junit.xml so CI reports regressions")
    parser.add_argument("--gate", choices=GATE_MODES, default="auto",
                        help="auto: block only when a threshold is given; "
                             "warn: annotate regressions; fail: block; "
                             "off: report only (default: auto)")
    parser.add_argument("--max-regression-percent", type=_threshold, default=None,
                        help="relative gate threshold (percent); applied to "
                             "gated metrics only")
    if absolute_option:
        parser.add_argument(absolute_option, dest="max_regression_abs",
                            type=_threshold, default=None,
                            help=absolute_help)


def gate_settings(args):
    """Map the uniform CLI flags to gate.evaluate() inputs.

    Returns ``(rel, absolute, severity)``. ``rel`` is a fraction, not a
    percent. With ``--gate off`` or no threshold, timing rows stay
    report-only; ``exact`` metrics (storage contract, public struct sizes)
    still find any change. Invalid data is unaffected by this choice.
    """
    if args.gate == "off":
        return None, None, "info"
    severity = {"auto": "fail", "warn": "warn", "fail": "fail"}[args.gate]
    rel = (None if args.max_regression_percent is None
           else args.max_regression_percent / 100.0)
    return rel, getattr(args, "max_regression_abs", None), severity


def gate_rows(pairs, *, suite, units, gated=(), rel=None, absolute=None,
              severity="fail", directions=None):
    """Build gate rows from (case, metric, before, after) tuples.

    Only metrics in ``gated`` receive thresholds, and ``absolute`` is
    applied only when the metric's unit matches, so a ns floor never leaks
    onto a cycles or bytes metric. Metrics not listed in ``directions``
    default to ``lower`` is better.
    """
    gated = set(gated)
    directions = directions or {}
    rows = []
    for case, metric, before, after in pairs:
        rows.append(gate.evaluate(
            before, after, suite=suite, case=str(case), metric=metric,
            unit=units.get(metric, ""), direction=directions.get(metric, "lower"),
            rel=(rel if metric in gated else None),
            absolute=(absolute if metric in gated
                      and units.get(metric) == "ns" else None),
            severity=severity))
    return rows


def incomparable_rows(items, reason, *, suite, units, severity="fail"):
    """Rows for two valid reports that must not be compared, from (case, metric)."""
    return [gate.incomparable(suite, case, metric, units.get(metric, ""),
                              reason, severity=severity)
            for case, metric in items]


def emit(args, rows, *, reasons=(), title="Wirelink benchmark comparison",
         before="base", after="candidate", suite="wirelink-benchmark"):
    """Write the requested outputs and return the SPEC exit code:
    0 pass, 1 regression (severity fail), 2 invalid/incomparable."""
    import json
    from pathlib import Path

    if args.json_out:
        Path(args.json_out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.json_out).write_text(
            json.dumps(schema.comparison_document(
                rows, testbed=suite, before=before, after=after,
                reasons=reasons), indent=2, allow_nan=False) + "\n",
            encoding="utf-8")
    if args.markdown_out:
        Path(args.markdown_out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.markdown_out).write_text(
            markdown.render(rows, title=title, reasons=reasons,
                            before=before, after=after), encoding="utf-8")
    if args.junit_out:
        Path(args.junit_out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.junit_out).write_text(
            junit.render(rows, suite=suite, reasons=reasons), encoding="utf-8")
    return gate.exit_code(rows)
