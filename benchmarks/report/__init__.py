# SPDX-License-Identifier: Apache-2.0
"""Shared benchmark comparison helpers; see benchmarks/SPEC.md."""
from . import gate, junit, markdown, schema  # noqa: F401

__all__ = ["gate", "junit", "markdown", "schema",
           "add_output_arguments", "gate_settings", "emit"]


def add_output_arguments(parser):
    """The uniform --json-out/--markdown-out/--junit-out/--gate surface that
    every benchmarks/*/compare.py exposes."""
    parser.add_argument("--json-out", type=str, default=None,
                        help="write comparison.json (machine readable)")
    parser.add_argument("--markdown-out", type=str, default=None,
                        help="write summary.md for a step summary or PR comment")
    parser.add_argument("--junit-out", type=str, default=None,
                        help="write junit.xml so CI reports regressions")
    parser.add_argument("--gate", choices=("off", "warn", "fail"), default="off",
                        help="off: report only; warn: regressions are annotated "
                             "but exit 0; fail: regressions exit 1")
    parser.add_argument("--max-regression-percent", type=float, default=None,
                        help="relative gate threshold (percent)")
    parser.add_argument("--max-regression-ns", type=float, default=None,
                        help="absolute gate threshold; a timing metric fails "
                             "only when both thresholds are exceeded")


def gate_settings(args):
    """Map the uniform CLI flags to gate.evaluate() inputs.

    Returns (rel, absolute, severity). With --gate off no threshold is
    applied, so rows stay report-only; --gate warn annotates regressions
    without failing; --gate fail makes them blocking. Invalid data (exit 2)
    is unaffected by this choice.
    """
    severity = {"off": "info", "warn": "warn", "fail": "fail"}[args.gate]
    if args.gate == "off" or (args.max_regression_percent is None
                              and args.max_regression_ns is None):
        return None, None, severity
    rel = None
    if args.max_regression_percent is not None:
        rel = args.max_regression_percent / 100.0
    return rel, args.max_regression_ns, severity


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
