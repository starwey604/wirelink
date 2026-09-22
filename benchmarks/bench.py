#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Unified benchmark entry point; see benchmarks/SPEC.md.

``bench.py list``   print the suite registry and presets from presets.toml
``bench.py doctor`` check the tools this machine needs for a preset

``run``, ``pair``, ``report``, ``calibrate`` and ``hil`` build on the same
registry and land per SPEC's adoption status. Every command follows the
three-state contract: 0 = fine, 1 = regression (gate surfaces only), 2 =
invalid environment or input — never a performance conclusion.
"""
import argparse
import glob
import os
import platform
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11; load_presets() reports it.
    tomllib = None

BENCH = Path(__file__).resolve().parent
REPO = BENCH.parent
PRESETS_FILE = BENCH / "presets.toml"

KINDS = ("size", "model", "gb")
BUILDS = ("none", "top", "standalone")
DEPS = ("wlc", "google-benchmark", "west", "serial")
GATE_MODES = ("auto", "warn", "fail", "off")
PLACEHOLDER_NAMES = ("python", "out", "bin", "before", "after")
REQUIRED_KEYS = ("kind", "build", "description", "run")
_PLACEHOLDER = re.compile(r"\{(\w+)\}")


def _placeholders(text):
    found = set(_PLACEHOLDER.findall(text))
    unknown = sorted(found - set(PLACEHOLDER_NAMES))
    if unknown:
        raise SystemExit(f"unknown placeholder(s): {', '.join('{'+u+'}' for u in unknown)}; "
                         f"known: {', '.join('{'+n+'}' for n in PLACEHOLDER_NAMES)}")


def _validate_suite(path, name, suite):
    where = f"{path}: [suites.{name}]"
    if not isinstance(suite, dict):
        raise SystemExit(f"{where}: expected a table")
    for key in REQUIRED_KEYS:
        if key not in suite:
            raise SystemExit(f"{where}: missing {key!r}")
    if suite["kind"] not in KINDS:
        raise SystemExit(f"{where}: kind must be one of {KINDS}")
    if suite["build"] not in BUILDS:
        raise SystemExit(f"{where}: build must be one of {BUILDS}")
    if not isinstance(suite["description"], str) or not suite["description"]:
        raise SystemExit(f"{where}: description must be a non-empty string")
    for key in ("run", "compare", "stamp"):
        if key in suite and (not isinstance(suite[key], list)
                             or not all(isinstance(item, str) for item in suite[key])):
            raise SystemExit(f"{where}: {key} must be a list of strings")
    if not suite["run"]:
        raise SystemExit(f"{where}: run must not be empty")
    if "requires" in suite:
        if not (isinstance(suite["requires"], list)
                and all(isinstance(dep, str) for dep in suite["requires"])):
            raise SystemExit(f"{where}: requires must be a list of strings")
        unknown = sorted(set(suite["requires"]) - set(DEPS))
        if unknown:
            raise SystemExit(f"{where}: unknown requirement(s): {', '.join(unknown)}")
    if suite["build"] == "standalone" and not suite.get("source"):
        raise SystemExit(f"{where}: standalone builds need source")
    if "cmake_options" in suite:
        if not isinstance(suite["cmake_options"], list) or not all(
                isinstance(item, str) for item in suite["cmake_options"]):
            raise SystemExit(f"{where}: cmake_options must be a list of strings")
    if "codegen" in suite:
        if not isinstance(suite["codegen"], dict) or not {"schema", "out"} <= suite["codegen"].keys():
            raise SystemExit(f"{where}: codegen needs schema and out")
    for key in ("run", "compare", "cmake_options"):
        for item in suite.get(key, []):
            _placeholders(item)


def _validate_preset(path, name, preset, suites):
    where = f"{path}: [presets.{name}]"
    if not isinstance(preset, dict):
        raise SystemExit(f"{where}: expected a table")
    chosen = preset.get("suites")
    if not isinstance(chosen, list) or not chosen:
        raise SystemExit(f"{where}: suites must be a non-empty list")
    missing = [item for item in chosen if item not in suites]
    if missing:
        raise SystemExit(f"{where}: unknown suite(s): {', '.join(missing)}")
    if len(set(chosen)) != len(chosen):
        raise SystemExit(f"{where}: duplicate suites")
    gate = preset.get("gate", {"mode": "auto"})
    if not isinstance(gate, dict) or gate.get("mode", "auto") not in GATE_MODES:
        raise SystemExit(f"{where}: gate.mode must be one of {GATE_MODES}")
    for key, value in gate.items():
        if key == "mode":
            continue
        if key not in ("percent", "ns", "ms", "bytes") or isinstance(value, bool) \
                or not isinstance(value, (int, float)) or value < 0:
            raise SystemExit(f"{where}: gate.{key} must be a nonnegative number")


def load_presets(path=PRESETS_FILE):
    """Parse and validate presets.toml; SystemExit(2) with a reason on error."""
    if tomllib is None:
        raise SystemExit(f"bench.py needs Python 3.11+ (tomllib); "
                         f"found {platform.python_version()}")
    try:
        with open(path, "rb") as stream:
            data = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise SystemExit(f"{path}: {error}")
    suites, presets = data.get("suites"), data.get("presets")
    if not isinstance(suites, dict) or not suites:
        raise SystemExit(f"{path}: [suites.*] registry is missing or empty")
    if not isinstance(presets, dict) or not presets:
        raise SystemExit(f"{path}: [presets.*] registry is missing or empty")
    for name, suite in suites.items():
        _validate_suite(path, name, suite)
    for name, preset in presets.items():
        _validate_preset(path, name, preset, suites)
    return {"suites": suites, "presets": presets}


def requirements_for(suites):
    """Tool dependencies implied by a suite selection."""
    deps, build, size = set(), False, False
    for suite in suites:
        deps.update(suite.get("requires", ()))
        build = build or suite["build"] != "none"
        size = size or suite["kind"] == "size"
    return deps, build, size


def run_id():
    """Directory name for one run: UTC timestamp plus the source revision."""
    from report import manifest
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    revision = manifest.revision()
    return stamp + (f"-{revision[:9]}" if revision else "")


def cmd_list(_args, data):
    suites, presets = data["suites"], data["presets"]
    print("Suites:")
    for name in sorted(suites):
        suite = suites[name]
        print(f"  {name:<16} {suite['kind']:<6} {suite['build']:<11} "
              f"{suite['description']}")
    print("Presets:")
    for name in sorted(presets):
        preset = presets[name]
        print(f"  {name:<16} {','.join(preset['suites'])}")
        print(f"  {'':<16} gate: " + " ".join(
            f"{key}={value}" for key, value in
            dict({"mode": "auto"}, **preset.get("gate", {})).items()))
        if preset.get("description"):
            print(f"  {'':<16} {preset['description']}")
    return 0


def _probe(argv, timeout=30):
    """First stdout/stderr line of a successful probe; None when it fails."""
    try:
        result = subprocess.run(argv, capture_output=True, text=True,
                                timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    lines = (result.stdout or result.stderr).strip().splitlines()
    return lines[0] if lines else "ok"


def _check_python():
    version = platform.python_version()
    if sys.version_info >= (3, 11):
        return "ok", version
    return "missing", f"{version} (< 3.11, no tomllib)"


def _check_cmake():
    line = _probe(["cmake", "--version"])
    if line is None:
        return "missing", "cmake not found on PATH"
    match = re.search(r"(\d+)\.(\d+)", line)
    if match and (int(match.group(1)), int(match.group(2))) < (3, 21):
        return "missing", f"{line} (< 3.21)"
    return "ok", line


def _check_cc():
    for candidate in [os.environ.get("CC") or "", "cc", "gcc", "clang"]:
        if candidate and shutil.which(candidate):
            return "ok", (_probe([candidate, "--version"]) or candidate)
    return "missing", "no C compiler (set CC, or provide cc/gcc/clang)"


def _check_size():
    if not shutil.which("size"):
        return "missing", "GNU/LLVM `size` not found on PATH"
    return "ok", (_probe(["size", "--version"]) or "present (version unknown)")


def _check_wlc():
    candidate = os.environ.get("WIRELINK_WLC_EXECUTABLE") or shutil.which("wlc")
    if not candidate:
        return "missing", "set WIRELINK_WLC_EXECUTABLE or put wlc on PATH"
    if not (shutil.which(candidate) or Path(candidate).is_file()):
        return "missing", f"WIRELINK_WLC_EXECUTABLE not a file: {candidate}"
    return "ok", (_probe([candidate, "--version"]) or str(candidate))


def _check_google_benchmark():
    configured = os.environ.get("WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR")
    if configured and Path(configured).is_dir():
        return "ok", configured
    return ("unverified",
            "WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR unset; "
            "build falls back to find_package(benchmark)")


def _check_west():
    line = _probe(["west", "--version"])
    if line:
        return "ok", line
    return "missing", "west not found (needed only for firmware/HIL)"


def _check_serial():
    if platform.system() != "Linux":
        return "skip", "serial ports are only probed on Linux"
    ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    if ports:
        return "ok", " ".join(ports)
    return "missing", "no /dev/ttyACM* or /dev/ttyUSB* (needed only for HIL)"


def cmd_doctor(args, data):
    chosen = [data["suites"][name] for name in data["presets"][args.preset]["suites"]]
    deps, needs_build, needs_size = requirements_for(chosen)
    checks = [
        ("python", _check_python, True),
        ("cmake", _check_cmake, needs_build),
        ("c compiler", _check_cc, needs_build or needs_size),
        ("size", _check_size, needs_size),
        ("wlc", _check_wlc, "wlc" in deps),
        ("google-benchmark", _check_google_benchmark, "google-benchmark" in deps),
        ("west", _check_west, False),
        ("serial ports", _check_serial, False),
    ]
    print(f"doctor: requirements for preset {args.preset} "
          f"({', '.join(data['presets'][args.preset]['suites'])})")
    blocking = 0
    for name, check, required in checks:
        status, detail = check()
        if required and status in ("missing",):
            blocking += 1
            detail += " [required]"
        elif not required:
            detail += " (optional)"
        print(f"  {name:<16} {status:<11} {detail}")
    if blocking:
        print(f"{blocking} required check(s) missing; "
              "this machine cannot produce trustworthy evidence for preset "
              f"{args.preset}")
        return 2
    print(f"environment OK for preset {args.preset}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="bench.py",
        description="Unified Wirelink benchmark entry point; see benchmarks/SPEC.md.")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="print the suite registry and presets")
    doctor = commands.add_parser(
        "doctor", help="check the tools this machine needs (exit 2 if a "
                       "required one is missing)")
    doctor.add_argument(
        "--preset", default="smoke",
        help="gate requirements against this preset (default: smoke)")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        data = load_presets()
    except SystemExit as error:
        if isinstance(error.code, str):  # message from validation → exit 2
            parser.error(error.code)
        raise
    if args.command == "list":
        return cmd_list(args, data)
    if args.preset not in data["presets"]:
        parser.error(f"unknown preset: {args.preset}; "
                     f"known: {', '.join(sorted(data['presets']))}")
    return cmd_doctor(args, data)


if __name__ == "__main__":
    raise SystemExit(main())
