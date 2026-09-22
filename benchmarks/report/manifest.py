# SPDX-License-Identifier: Apache-2.0
"""Run identity: revision, build environment, CPU identity, workload hash.

See benchmarks/SPEC.md ("Output layout", the environment fields and the
benchmark_source_hash rule). Every probe is best effort: a missing git
checkout, CMake cache, sysfs entry or compiler executable yields an empty
value instead of an error, because recording identity must never fail a
benchmark run.

The workload source hash ties a report to the benchmark code that produced
it. Two reports carrying different hashes are incomparable (exit 2), never
regressing; ``source_hash_reason`` implements the both-sides rule shared by
every native comparator and by schema.incomparable_reasons.
"""
import argparse
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

from . import schema

REPO = Path(__file__).resolve().parents[2]
MANIFEST_SCHEMA = "wirelink-benchmark-manifest-v1"

# Build identity recorded in api reports and manifests (kept identical to
# the historical benchmarks/api/run.py list).
CMAKE_KEYS = frozenset({
    "CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
    "CMAKE_C_FLAGS", "CMAKE_C_FLAGS_RELEASE", "CMAKE_CXX_FLAGS",
    "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_INTERPROCEDURAL_OPTIMIZATION",
    "WIRELINK_WLC_CODEGEN_CONTRACT_MAJOR", "WIRELINK_WLC_CODEGEN_CONTRACT_MINOR"})


def _cache_path(directory):
    for parent in [Path(directory).resolve(), *Path(directory).resolve().parents]:
        cache = parent / "CMakeCache.txt"
        if cache.is_file():
            return cache
    return None


def _cache_values(cache):
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        values[line.split(":", 1)[0]] = line.split("=", 1)[1]
    return values


def cmake_cache(directory):
    """Build identity from the nearest CMakeCache.txt; {} without one."""
    cache = _cache_path(directory)
    if cache is None:
        return {}
    values = _cache_values(cache)
    return {key: values[key] for key in sorted(CMAKE_KEYS) if key in values}


def cmake_options(directory):
    """Enabled WIRELINK_* cache entries, as SPEC's environment.cmake_options."""
    cache = _cache_path(directory)
    if cache is None:
        return ""
    values = _cache_values(cache)
    return ";".join(f"{key}={values[key]}" for key in sorted(values)
                    if key.startswith("WIRELINK_") and values[key] == "ON")


def revision(repo=None):
    """Git commit of the sources plus a ``-dirty`` marker; None best effort."""
    repo = REPO if repo is None else Path(repo)
    try:
        head = subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"],
                              capture_output=True, text=True, check=True,
                              timeout=30)
        tree = subprocess.run(["git", "-C", str(repo), "status", "--porcelain"],
                              capture_output=True, text=True, check=True,
                              timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    sha = head.stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", sha):
        return None
    return sha + ("-dirty" if tree.stdout.strip() else "")


def wlc_version(repo=None):
    """Pinned codegen contract from include/wirelink/version.h; None best effort."""
    header = ((REPO if repo is None else Path(repo))
              / "include" / "wirelink" / "version.h")
    try:
        text = header.read_text(encoding="utf-8")
    except OSError:
        return None
    major = re.search(r"^#define WIRELINK_CODEGEN_CONTRACT_MAJOR\s+(\d+)$",
                      text, re.M)
    minor = re.search(r"^#define WIRELINK_CODEGEN_CONTRACT_MINOR\s+(\d+)$",
                      text, re.M)
    if not (major and minor):
        return None
    return f"contract {major.group(1)}.{minor.group(1)}"


def cpu_identity():
    """SPEC environment CPU fields; empty values when the platform hides them."""
    model = platform.processor()
    if not model:
        try:
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
                if line.startswith("model name"):
                    model = line.split(":", 1)[1].strip()
                    break
        except OSError:
            model = ""
    affinity = []
    if hasattr(os, "sched_getaffinity"):
        try:
            affinity = sorted(os.sched_getaffinity(0))
        except OSError:
            affinity = []
    governor = ""
    try:
        governor = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
                        ).read_text(encoding="utf-8").strip()
    except OSError:
        pass
    return {"cpu": model or "", "affinity": affinity,
            "governor": governor or "unknown"}


def compiler_version(build):
    """First line of the configured C compiler's --version output."""
    compiler = (build or {}).get("CMAKE_C_COMPILER")
    if not compiler:
        return ""
    try:
        result = subprocess.run([compiler, "--version"], capture_output=True,
                                text=True, check=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return compiler
    lines = result.stdout.strip().splitlines()
    return lines[0] if lines else compiler


def source_hash(paths):
    """SPEC benchmark_source_hash over the workload's own sources."""
    return schema.workload_hash(paths)


def source_hash_reason(before, after):
    """Reason these two report contexts must not be compared; None if they may.

    Both sides must record a hash: one-sided or equal hashes stay
    comparable, only a recorded difference blocks the pair.
    """
    left = before.get("benchmark_source_hash")
    right = after.get("benchmark_source_hash")
    if left is not None and right is not None and left != right:
        return "benchmark workload source hash changed; the pair is incomparable"
    return None


def environment(build_dir=None, sources=(), extra=None):
    """Assemble the SPEC environment object; missing pieces stay empty."""
    build = cmake_cache(build_dir) if build_dir else {}
    env = {
        "compiler": compiler_version(build),
        "flags": " ".join(filter(None, (build.get("CMAKE_C_FLAGS"),
                                        build.get("CMAKE_C_FLAGS_RELEASE")))).strip(),
        "cmake_options": cmake_options(build_dir) if build_dir else "",
        **cpu_identity(),
        "wlc": wlc_version() or "",
    }
    if sources:
        env["benchmark_source_hash"] = source_hash(sources)
    if extra:
        env.update(extra)
    return env


def manifest(*, suite, testbed, build_dir=None, sources=(), extra=None):
    """Build the manifest.json document from SPEC's output layout."""
    if not suite or not testbed:
        raise ValueError("suite and testbed are required")
    return {"schema": MANIFEST_SCHEMA, "suite": suite, "testbed": testbed,
            "revision": revision(),
            "environment": environment(build_dir=build_dir, sources=sources,
                                       extra=extra)}


def write_manifest(path, document):
    """Write manifest.json; never overwrite existing evidence."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(document, stream, indent=2, allow_nan=False)
        stream.write("\n")


def stamp(path, sources):
    """Record context.benchmark_source_hash in a Google Benchmark JSON file.

    Only this one context field is added; measurements stay untouched.
    Stamp immediately after capture, before editing workload sources: the
    hash records the benchmark code that produced this report.
    """
    sources = [Path(p) for p in sources]
    if not sources:
        raise ValueError("at least one workload source is required")
    path = Path(path)
    report = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(report, dict):
        raise ValueError(f"{path}: not a benchmark report object")
    context = report.setdefault("context", {})
    if not isinstance(context, dict):
        raise ValueError(f"{path}: context is not an object")
    digest = source_hash(sources)
    context["benchmark_source_hash"] = digest
    path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n",
                    encoding="utf-8")
    return digest


def main(argv=None):
    """CLI: ``stamp`` a report or write a ``manifest`` (see benchmarks/stamp.py)."""
    parser = argparse.ArgumentParser(
        prog="manifest.py",
        description="Record benchmark run identity (see benchmarks/SPEC.md).")
    commands = parser.add_subparsers(dest="command", required=True)
    stamp_cmd = commands.add_parser(
        "stamp",
        help="add context.benchmark_source_hash to a Google Benchmark JSON report")
    stamp_cmd.add_argument("report", type=Path)
    stamp_cmd.add_argument("sources", nargs="+", type=Path,
                           help="workload sources hashed into the report")
    manifest_cmd = commands.add_parser("manifest", help="write manifest.json for a run")
    manifest_cmd.add_argument("out", type=Path)
    manifest_cmd.add_argument("--suite", required=True)
    manifest_cmd.add_argument("--testbed", required=True)
    manifest_cmd.add_argument("--build-dir", type=Path, default=None)
    manifest_cmd.add_argument("--sources", nargs="*", type=Path, default=[])
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if args.command == "stamp":
            digest = stamp(args.report, args.sources)
            print(f"{args.report}: benchmark_source_hash={digest}")
        else:
            write_manifest(args.out, manifest(
                suite=args.suite, testbed=args.testbed,
                build_dir=args.build_dir, sources=args.sources))
            print(f"{args.out}: manifest written")
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
