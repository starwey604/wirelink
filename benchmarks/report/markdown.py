# SPDX-License-Identifier: Apache-2.0
"""Render a comparison as Markdown for a step summary or a PR comment."""
from . import gate

LABEL = {"pass": "pass", "warn": "warn", "fail": "**FAIL**",
         "incomparable": "incomparable"}


def _number(value):
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def render(rows, *, title="Wirelink benchmark comparison", reasons=(),
           before="base", after="candidate"):
    """Markdown table of every gated metric plus an overall verdict block."""
    counts = gate.verdict(rows)
    code = gate.exit_code(rows)
    lines = [f"## {title}", "",
             f"`{before}` -> `{after}`: "
             f"{counts['pass']} pass, {counts['warn']} warn, "
             f"{counts['fail']} fail, {counts['incomparable']} incomparable",
             ""]
    if reasons:
        lines += ["**Not comparable:**", ""]
        lines += [f"- {reason}" for reason in reasons]
        lines.append("")
    if rows:
        lines += ["| suite | case | metric | before | after | delta | status |",
                  "| --- | --- | --- | ---: | ---: | ---: | --- |"]
        for row in rows:
            if row["status"] == "incomparable":
                before_cell = after_cell = delta_cell = "n/a"
            else:
                before_cell = _number(row["before"])
                after_cell = _number(row["after"])
                delta_cell = f"{row['delta_rel'] * 100:+.1f}%"
            lines.append(
                f"| {row['suite']} | {row['case']} | {row['metric']} "
                f"| {before_cell} | {after_cell} | {delta_cell} "
                f"| {LABEL[row['status']]} |")
        lines.append("")
    lines.append("exit code: " + str(code))
    return "\n".join(lines) + "\n"
