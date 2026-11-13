#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure Wirelink's static footprint.

The default ``objects`` mode compiles every core translation unit on its own and
reports the size of each object's text, rodata, data and bss sections. The
``elf`` mode reports the section table of an already linked image (for example
an ESP32-S3 or native_sim firmware). Both modes attach the storage contract that
the explicit checked-in demo kernel uses in three sample assemblies, plus the
size of the public structs, so a report covers code size and explicit example storage; it is not a default product image footprint.

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
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCHEMA = "wirelink-footprint-v2"
PROBE_PREFIX = "wirelink_footprint_v2"
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

# Explicit example assembly byte fields emitted by probe.c.
STORAGE_FIELDS = ("endpoint", "packet_port", "rx_units", "stream_object",
                  "stream_tx", "byte_fifo_object", "byte_fifo_storage")


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
            values = [int(value) for value in parts[3:]]
            if len(values) != len(STORAGE_FIELDS):
                raise RuntimeError("malformed storage row: %s" % line)
            storage.append({"deployment": parts[2], **dict(zip(STORAGE_FIELDS, values))})
    if not types or not storage:
        raise RuntimeError("probe produced no measurements")
    return types, storage


def measure_elf(path, size_tool):
    totals, sections = parse_size(run([size_tool, "-A", str(path)]))
    return totals, sections


def recorded_flags(flags, root):
    """Compile flags for the report, with paths under the tree root made
    relative.

    A relocated checkout (`bench.py pair --base` creates a git worktree)
    must record the same toolchain identity as the original tree; absolute
    include paths would read as a toolchain mismatch and make the pair
    incomparable.
    """
    # Accept both slash spellings, including POSIX-style flags on Windows.
    prefix = str(root).replace("\\", "/").rstrip("/") + "/"
    recorded = []
    for flag in flags:
        option = "-I" if flag.startswith("-I") else ""
        path = flag[len(option):].replace("\\", "/")
        if path.startswith(prefix):
            recorded.append(option + path[len(prefix):])
        else:
            recorded.append(flag)
    return recorded


def build_report(args):
    compiler = {"identity": "unknown; retain the original image build separately"} if args.elf else {
        "cc": args.cc,
        "flags": recorded_flags(args.cflags, args.root),
        "version": run([args.cc, "--version"]).splitlines()[0],
    }
    report = {
        "schema": SCHEMA,
        "mode": "objects",
        "probe_enabled": args.probe and args.elf is None,
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
        report["elf_sha256"] = hashlib.sha256(args.elf.read_bytes()).hexdigest()
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
        report["source_sha256"] = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in modules}
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
                          str(args.root / "src"), "-I",
                          str(args.root / "examples/kernel/generated")]
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
