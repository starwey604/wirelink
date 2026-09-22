# SPDX-License-Identifier: Apache-2.0
"""Render a comparison as JUnit XML so CI shows regressions as test failures."""
from . import gate
from xml.sax.saxutils import escape, quoteattr

HEADER = '<?xml version="1.0" encoding="UTF-8"?>\n'


def _case(row, classname):
    name = quoteattr(f"{row['suite']}.{row['case']}.{row['metric']}")
    lines = [f"  <testcase classname={classname} name={name}>"]
    if row["status"] == "fail":
        detail = (f"regression: {row['before']:g} -> {row['after']:g} "
                  f"({row['delta_rel'] * 100:+.1f}%), "
                  f"rel>{row['threshold']['rel']} "
                  f"abs>{row['threshold']['abs']}")
        lines.append(f"    <failure message={quoteattr(detail)} />")
    elif row["status"] == "incomparable":
        reason = escape(row.get("reason", "measurements not comparable"))
        lines.append(f'    <error message="not comparable" type="incomparable">'
                     f"{reason}</error>")
    elif row["status"] == "warn":
        detail = (f"warn: {row['before']:g} -> {row['after']:g} "
                  f"({row['delta_rel'] * 100:+.1f}%)")
        lines.append(f"    <system-out>{escape(detail)}</system-out>")
    lines.append("  </testcase>")
    return lines


def render(rows, *, suite="wirelink-benchmark", reasons=()):
    """One testsuite; failed metrics are <failure>, incomparable ones <error>."""
    counts = gate.verdict(rows)
    lines = [HEADER,
             f'<testsuites name={quoteattr(suite)} tests="{len(rows)}" '
             f'failures="{counts["fail"]}" errors="{counts["incomparable"]}">',
             f'  <testsuite name={quoteattr(suite)} tests="{len(rows)}" '
             f'failures="{counts["fail"]}" errors="{counts["incomparable"]}">']
    for reason in reasons:
        lines.append(f"    <system-out>{escape(str(reason))}</system-out>")
    classname = quoteattr(suite)
    for row in rows:
        lines.extend(_case(row, classname))
    lines += ["  </testsuite>", "</testsuites>"]
    return "\n".join(lines) + "\n"
