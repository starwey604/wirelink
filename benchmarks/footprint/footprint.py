#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure Wirelink's static footprint.

The default ``objects`` mode compiles every core translation unit on its own and
reports the size of each object's text, rodata, data and bss sections. The
``elf`` mode reports the section table of an already linked image (for example
an ESP32-S3 or native_sim firmware). Both modes attach the storage contract that
``wl_config_requirements()`` returns for representative configurations, plus the
size of the public structs, so a report covers code size and the static RAM the
library asks the caller to reserve.

The tool compiles the sources itself, so it needs only a C compiler and a
GNU/LLVM ``size``. It does not require a configured build tree.

Examples::

    python benchmarks/footprint/footprint.py --json footprint.json
    python benchmarks/footprint/footprint.py --cc gcc --cflags=-O2 \
        --json footprint-o2.json
    python benchmarks/footprint/footprint.py --elf build/zephyr/zephyr.elf \
        --size-tool xtensa-espressif_esp32s3_zephyr-elf-size --json hw.json
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCHEMA = "wirelink-footprint-v1"
PROBE_PREFIX = "wirelink_footprint_v1"
DEFAULT_CFLAGS = [
    "-std=c11",
    "-Os",
    "-ffunction-sections",
    "-fdata-sections",
    # Hosted compilers emit unwind tables per object; an embedded image does
    # not. Keep them out so the report reflects target-like section sizes.
    "-fno-asynchronous-unwind-tables",
]
SECTIONS = ("text", "rodata", "data", "bss", "debug", "other")

# Storage-config tuple order emitted by probe.c.
STORAGE_FIELDS = (
    "envelope",
    "integrity",
    "max_payload",
    "mtu",
    "tx_payload",
    "tx_unit",
    "control",
    "rx_fifo",
    "rx_fallback",
)


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            "command failed (%d): %s\n%s"
            % (result.returncode, " ".join(command), result.stderr.strip())
        )
    return result.stdout


def size_tool_for(cc):
    name = Path(cc).name
    match = re.match(r"(.+-(?:gcc|clang|cc))$", name)
    if match:
        candidate = name[: -len("gcc")] + "size"
        if shutil.which(candidate):
            return candidate
    for candidate in ("size", "llvm-size"):
        if shutil.which(candidate):
            return candidate
    raise RuntimeError("no size tool found; pass --size-tool")


def section_class(name):
    # Debug, unwind, comment and vendor metadata never occupy target memory, so
    # keep them out of the loadable-section classes.
    if name.startswith((".debug", ".zdebug", ".comment", ".note", ".gnu",
                        ".stab", ".xt.prop", ".xt.lit", ".xt.insn",
                        ".eh_frame")):
        return "debug"
    if ".bss" in name or ".noinit" in name or ".sbss" in name:
        return "bss"
    if ".data" in name or ".sdata" in name:
        return "data"
    if ".rodata" in name or ".srodata" in name:
        return "rodata"
    if (
        ".text" in name
        or name.startswith(".iram")
        or name.startswith(".itcm")
        or name.startswith(".vectors")
    ):
        return "text"
    return "other"


def parse_size(output):
    """Aggregate ``size -A`` rows into section classes and keep the raw rows."""
    totals = {key: 0 for key in SECTIONS}
    sections = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 2 or not parts[0].startswith("."):
            continue
        try:
            size = int(parts[1], 16) if parts[1].startswith("0x") else int(parts[1])
        except ValueError:
            continue
        if size == 0:
            continue
        sections[parts[0]] = sections.get(parts[0], 0) + size
        totals[section_class(parts[0])] += size
    return totals, sections


def measure_object(size_tool, path):
    return parse_size(run([size_tool, "-A", str(path)]))[0]


def compile_one(cc, flags, source, output):
    run([cc, *flags, "-c", str(source), "-o", str(output)])


def measure_objects(cc, flags, size_tool, root, workdir):
    sources = sorted((root / "src").glob("*.c"))
    if not sources:
        raise RuntimeError("no sources under %s" % (root / "src"))
    modules = {}
    objects = []
    for source in sources:
        obj = workdir / (source.stem + ".o")
        compile_one(cc, flags, source, obj)
        objects.append(obj)
        modules[str(source.relative_to(root))] = measure_object(size_tool, obj)
    return modules, objects


def run_probe(cc, flags, root, objects, workdir):
    probe_source = Path(__file__).resolve().with_name("probe.c")
    probe_object = workdir / "probe.o"
    compile_one(cc, flags, probe_source, probe_object)
    executable = workdir / "probe"
    run([cc, str(probe_object), *[str(obj) for obj in objects], "-o",
         str(executable)])
    types = {}
    storage = []
    for line in run([str(executable)]).splitlines():
        parts = line.strip().split(",")
        if len(parts) < 3 or parts[0] != PROBE_PREFIX:
            continue
        if parts[1] == "type":
            types[parts[2]] = int(parts[3])
        elif parts[1] == "storage":
            values = [int(value) for value in parts[2:]]
            if len(values) != len(STORAGE_FIELDS):
                raise RuntimeError("malformed storage row: %s" % line)
            storage.append(dict(zip(STORAGE_FIELDS, values)))
    if not types or not storage:
        raise RuntimeError("probe produced no measurements")
    return types, storage


def measure_elf(path, size_tool):
    totals, sections = parse_size(run([size_tool, "-A", str(path)]))
    return totals, sections


def build_report(args):
    compiler = {
        "cc": args.cc,
        "flags": list(args.cflags),
    }
    report = {
        "schema": SCHEMA,
        "mode": "objects",
        "compiler": compiler,
        "target": None,
        "modules": None,
        "sections": None,
        "totals": {key: 0 for key in SECTIONS},
        "types": {},
        "storage": [],
    }

    if args.elf is not None:
        size_tool = args.size_tool
        totals, sections = measure_elf(args.elf, size_tool)
        report["mode"] = "elf"
        report["target"] = str(args.elf)
        report["sections"] = sections
        report["totals"] = totals
        return report

    root = args.root
    size_tool = args.size_tool
    with tempfile.TemporaryDirectory(prefix="wirelink-footprint-") as tmp:
        workdir = Path(tmp)
        modules, objects = measure_objects(args.cc, args.cflags, size_tool, root,
                                           workdir)
        report["modules"] = modules
        for module in modules.values():
            for section in SECTIONS:
                report["totals"][section] += module[section]
        if args.probe:
            types, storage = run_probe(args.cc, args.cflags, root, objects,
                                       workdir)
            report["types"] = types
            report["storage"] = storage
    return report


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2],
                        help="repository root (default: the tool's repository)")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"),
                        help="C compiler for objects mode")
    parser.add_argument("--cflags", action="append", default=None,
                        help="extra compiler flags, shell-quoted; repeatable")
    parser.add_argument("--size-tool", default=None,
                        help="size binary (default: derived from --cc)")
    parser.add_argument("--elf", type=Path, default=None,
                        help="report a linked ELF instead of compiling objects")
    parser.add_argument("--no-probe", dest="probe", action="store_false",
                        help="skip the storage/structure probe")
    parser.add_argument("--json", type=Path, default=None,
                        help="output path (default: stdout)")
    args = parser.parse_args(argv)
    if args.elf is not None and args.size_tool is None:
        parser.error("--elf requires --size-tool")
    base = list(DEFAULT_CFLAGS)
    for group in args.cflags or []:
        base.extend(shlex.split(group))
    args.cflags = base + ["-I", str(args.root / "include"), "-I",
                          str(args.root / "src")]
    if args.size_tool is None:
        args.size_tool = size_tool_for(args.cc)
    return args


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    report = build_report(args)
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.json is None:
        sys.stdout.write(text)
    else:
        args.json.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
