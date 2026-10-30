# SPDX-License-Identifier: Apache-2.0
"""Compare two Wirelink footprint reports.

Both inputs must come from ``footprint.py`` with the same schema, mode and
toolchain identity. The comparison reports byte deltas for the section totals,
each module or ELF section, the storage contract and the public struct sizes.
``--max-regression-percent`` (optionally paired with ``--max-growth-bytes``)
is an optional gate for a dedicated runner; it is not a default, because
compiler and standard-library changes legitimately move code size by a few
percent.
"""

import argparse
import json
import sys
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from report import add_output_arguments, emit, gate, gate_settings

SCHEMA = "wirelink-footprint-v2"
LEGACY_SCHEMA = "wirelink-footprint-v1"
SECTIONS = ("text", "rodata", "data", "bss", "debug", "other")
LEGACY_STORAGE_METRICS = ("tx_payload", "tx_unit", "control", "rx_fifo", "rx_fallback")
STORAGE_METRICS = ("endpoint", "packet_port", "rx_units", "stream_object", "stream_tx",
                   "byte_fifo_object", "byte_fifo_storage")
UNITS = {name: "bytes" for name in SECTIONS}
UNITS.update({metric: "bytes" for metric in STORAGE_METRICS + LEGACY_STORAGE_METRICS})
# Section/module rows are lower-is-better; public struct sizes are exact:
# any change alters every consumer's ABI. The storage contract is the most
# important thing this benchmark tracks, so it is exact too.
EXACT_GROUPS = {"type"}

Row = namedtuple("Row", "group item before after")


def load(path):
    report = json.loads(Path(path).read_text())
    if not isinstance(report, dict) or report.get("schema") not in (SCHEMA, LEGACY_SCHEMA):
        raise ValueError(f"{path}: not a {SCHEMA} report")
    if report["schema"] == SCHEMA:
        if report.get("mode") not in ("objects", "elf") or type(report.get("probe_enabled")) is not bool:
            raise ValueError("invalid current footprint context")
        if report["probe_enabled"]:
            expected = {"demo_usb_native_none", "demo_serial_cobs_crc16", "demo_length16_none"}
            entries = report.get("storage", [])
            if len(entries) != 3 or {entry.get("deployment") for entry in entries} != expected:
                raise ValueError("incomplete or duplicate assembly budgets")
            for entry in entries:
                if any(type(entry.get(key)) is not int or entry[key] < 0 for key in STORAGE_METRICS):
                    raise ValueError("invalid assembly byte budget")
            if not report.get("types") or any(type(value) is not int or value <= 0 for value in report["types"].values()):
                raise ValueError("invalid measured structure sizes")
    return report


def _storage_key(entry):
    if "deployment" in entry:
        return entry["deployment"]
    return "env%d/int%d/p%d" % (entry["envelope"], entry["integrity"],
                                entry["max_payload"])


def compare(before, after):
    rows = []
    for section in SECTIONS:
        rows.append(Row("total", section, int(before["totals"].get(section, 0)),
                        int(after["totals"].get(section, 0))))

    names = set((before.get("modules") or {})) | set((after.get("modules") or {}))
    for name in sorted(names):
        left = (before.get("modules") or {}).get(name, {})
        right = (after.get("modules") or {}).get(name, {})
        for section in SECTIONS:
            old = int(left.get(section, 0))
            new = int(right.get(section, 0))
            if old or new:
                rows.append(Row(name, section, old, new))

    sections = set((before.get("sections") or {})) | set((after.get("sections") or {}))
    for name in sorted(sections):
        old = int((before.get("sections") or {}).get(name, 0))
        new = int((after.get("sections") or {}).get(name, 0))
        rows.append(Row("section", name, old, new))

    indexed = {}
    for entry in before.get("storage") or []:
        indexed.setdefault(_storage_key(entry), {})["before"] = entry
    for entry in after.get("storage") or []:
        indexed.setdefault(_storage_key(entry), {})["after"] = entry
    for key in sorted(indexed):
        old = indexed[key].get("before", {})
        new = indexed[key].get("after", {})
        metrics = STORAGE_METRICS if "deployment" in old or "deployment" in new else LEGACY_STORAGE_METRICS
        for metric in metrics:
            rows.append(Row("storage " + key, metric, int(old.get(metric, 0)),
                            int(new.get(metric, 0))))

    types = set(before.get("types") or {}) | set(after.get("types") or {})
    for name in sorted(types):
        rows.append(Row("type", name, int((before.get("types") or {}).get(name, 0)),
                        int((after.get("types") or {}).get(name, 0))))
    return rows


def percent(before, after):
    if before == 0:
        return 0.0 if after == 0 else float("inf")
    return (after - before) * 100.0 / before


def exceeding(rows, limit):
    if limit is None:
        return []
    hits = []
    for row in rows:
        if percent(row.before, row.after) > limit:
            hits.append(row)
    return hits


def format_rows(rows):
    lines = [f"{'group':<30} {'item':<12} {'before':>9} {'after':>9} "
             f"{'delta':>9} {'percent':>8}"]
    for row in rows:
        change = percent(row.before, row.after)
        shown = "new" if change == float("inf") else f"{change:+.1f}%"
        lines.append(f"{row.group:<30} {row.item:<12} {row.before:>9} "
                     f"{row.after:>9} {row.after - row.before:>+9} {shown:>8}")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("before")
    parser.add_argument("after")
    add_output_arguments(parser, absolute_option="--max-growth-bytes",
                         absolute_help="absolute gate threshold in bytes; "
                                       "size fails only when both thresholds "
                                       "are exceeded")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    rel, absolute, severity = gate_settings(args)

    try:
        before, after = load(args.before), load(args.after)
    except (ValueError, OSError) as error:
        parser.error(str(error))  # invalid data: exit 2, no verdict
    unknown_image_build = before.get("schema") == SCHEMA and before.get("mode") == "elf"
    if (unknown_image_build or before.get("schema") != after.get("schema") or before.get("mode") != after.get("mode")
            or before.get("compiler") != after.get("compiler")):
        reason = "unqualified image build" if unknown_image_build else "schema, mode or toolchain mismatch; size reports are not comparable"
        keys = {(row.group, row.item) for row in compare(before, after)}
        rows = [gate.incomparable("footprint", group, item, "bytes", reason,
                                  severity=severity)
                for group, item in sorted(keys)]
        return emit(args, rows, reasons=[reason], suite="footprint",
                    title="Wirelink footprint comparison",
                    before=str(args.before), after=str(args.after))
    rows = []
    for row in compare(before, after):
        exact = row.group in EXACT_GROUPS or row.group.startswith("storage ")
        rows.append(gate.evaluate(
            row.before, row.after, suite="footprint", case=row.group,
            metric=row.item, unit=UNITS.get(row.item, "bytes"),
            direction="exact" if exact else "lower",
            rel=None if exact else rel, absolute=None if exact else absolute,
            severity=severity))
    print(format_rows(compare(before, after)))
    return emit(args, rows, suite="footprint",
                title="Wirelink footprint comparison",
                before=str(args.before), after=str(args.after))


if __name__ == "__main__":
    sys.exit(main())
