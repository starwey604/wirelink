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
import contextlib
import glob
import hashlib
import io
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
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


class RunFailure(RuntimeError):
    """A suite could not produce trustworthy evidence (never a regression)."""


def _select_suites(args, data, parser):
    """(suites, manifest_suite_label): --suite overrides --preset."""
    if getattr(args, "suite", None):
        unknown = [name for name in args.suite if name not in data["suites"]]
        if unknown:
            parser.error(f"unknown suite(s): {', '.join(unknown)}; known: "
                         f"{', '.join(sorted(data['suites']))}")
        names = list(dict.fromkeys(args.suite))
        label = "+".join(names)
    else:
        if args.preset not in data["presets"]:
            parser.error(f"unknown preset: {args.preset}; known: "
                         f"{', '.join(sorted(data['presets']))}")
        names = data["presets"][args.preset]["suites"]
        label = args.preset
    return [(name, data["suites"][name]) for name in names], label


def _resolve(argv, mapping):
    """Substitute {python}/{out}/{bin}/... placeholders with exact strings."""
    resolved = []
    for item in argv:
        for key, value in mapping.items():
            item = item.replace("{" + key + "}", str(value))
        resolved.append(item)
    return resolved


def _run(argv, cwd=REPO, stdout_path=None, step="command"):
    try:
        if stdout_path is None:
            code = subprocess.run(argv, cwd=cwd).returncode
        else:
            with open(stdout_path, "w", encoding="utf-8") as sink:
                code = subprocess.run(argv, cwd=cwd, stdout=sink).returncode
    except OSError as error:
        raise RunFailure(f"{step}: {error}")
    if code != 0:
        head = " ".join(str(part) for part in argv[:4])
        raise RunFailure(f"{step} exited {code}: {head}")


def _build(suite, args):
    """Configure and build one suite; returns its build directory."""
    bin_dir = REPO / "build" / "bench" / suite["name"]
    mapping = {"python": sys.executable, "out": bin_dir, "bin": bin_dir}
    if suite.get("codegen"):
        wlc = os.environ.get("WIRELINK_WLC_EXECUTABLE") or shutil.which("wlc")
        if not wlc:
            raise RunFailure("WLC not found (set WIRELINK_WLC_EXECUTABLE or "
                             "put wlc on PATH)")
        out_dir = bin_dir / suite["codegen"]["out"]
        _run([wlc, "compile", str(REPO / suite["codegen"]["schema"]),
              "--out-dir", str(out_dir)], step="wlc compile")
    source = REPO if suite["build"] == "top" else REPO / suite["source"]
    configure = ["cmake", "-S", str(source), "-B", str(bin_dir),
                 "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
                 *_resolve(suite.get("cmake_options", []), mapping)]
    if "google-benchmark" in suite.get("requires", ()):
        gb = os.environ.get("WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR")
        if gb:
            configure.append(f"-DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR={gb}")
    _run(configure, step="cmake configure")
    build = ["cmake", "--build", str(bin_dir), "--parallel"]
    if args.jobs:
        build.append(str(args.jobs))
    _run(build, step="cmake build")
    return bin_dir


def _run_suite(name, suite, raw, out, args):
    bin_dir = None
    if suite["build"] != "none":
        bin_dir = _build(suite, args) if not args.skip_build \
            else REPO / "build" / "bench" / name
    mapping = {"python": sys.executable, "out": raw, "bin": bin_dir or ""}
    stdout_path = raw / suite["stdout"] if suite.get("stdout") else None
    _run(_resolve(suite["run"], mapping), stdout_path=stdout_path,
         step=f"{name} runner")
    if suite.get("stamp"):
        sources = [REPO / source for source in suite["stamp"]]
        reports = sorted(raw.glob("*.json"))
        if not reports:
            raise RunFailure("no report to stamp")
        for report in reports:
            try:
                from report import manifest
                manifest.stamp(report, sources)
            except (OSError, ValueError) as error:
                raise RunFailure(f"stamp {report.name}: {error}")
    if bin_dir and (bin_dir / "CMakeCache.txt").is_file():
        shutil.copy2(bin_dir / "CMakeCache.txt",
                     out / "artifacts" / f"{name}.CMakeCache.txt")
    binary = suite.get("binary")
    if binary and bin_dir and (bin_dir / binary).is_file():
        digest = hashlib.sha256((bin_dir / binary).read_bytes()).hexdigest()
        with open(out / "artifacts" / "binaries.sha256", "a",
                  encoding="utf-8") as stream:
            stream.write(f"{digest}  {name}/{Path(binary).name}\n")


def cmd_run(args, data, parser):
    from report import manifest
    suites, label = _select_suites(args, data, parser)
    out = args.out if args.out else REPO / "out" / run_id()
    try:
        out.mkdir(parents=True)
    except FileExistsError:
        print(f"bench: refusing to overwrite existing run directory {out}")
        return 2
    (out / "raw").mkdir()
    (out / "artifacts").mkdir()
    testbed = args.testbed or f"local-{platform.system().lower()}-" \
        f"{platform.machine()}"
    identity = manifest.manifest(suite=label, testbed=testbed)
    manifest.write_manifest(out / "manifest.json", identity)
    failed = []
    for name, suite in suites:
        raw = out / "raw" / name
        raw.mkdir()
        named = dict(suite, name=name)
        try:
            _run_suite(name, named, raw, out, args)
        except RunFailure as error:
            print(f"bench: {name}: {error}")
            failed.append(name)
    if failed:
        print(f"bench: {len(failed)} suite(s) failed ({', '.join(failed)}); "
              f"partial evidence kept in {out}")
        return 2
    print(f"bench: {len(suites)} suite(s) complete: {out}\n"
          f"bench: next: python3 benchmarks/bench.py report {out}")
    return 0


def cmd_report(args, _data):
    from report import normalize, schema
    run_dir = args.run
    manifest_path = run_dir / "manifest.json"
    raw_root = run_dir / "raw"
    if not manifest_path.is_file() or not raw_root.is_dir():
        print(f"bench: {run_dir}: not a run directory "
              "(manifest.json and raw/ are required)")
        return 2
    try:
        identity = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(f"bench: {manifest_path}: {error}")
        return 2
    rows, errors, count = [], [], 0
    for suite_dir in sorted(path for path in raw_root.iterdir()
                            if path.is_dir()):
        count += 1
        try:
            rows.extend(normalize.normalize(suite_dir.name, suite_dir))
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(f"{suite_dir.name}: {error}")
    if errors:
        for error in errors:
            print(f"bench: invalid raw evidence: {error}")
        return 2
    try:
        document = schema.document(identity.get("testbed"),
                                   identity.get("revision"),
                                   identity.get("environment") or {}, rows)
    except ValueError as error:
        print(f"bench: cannot build wirelink-benchmark-v2 document: {error}")
        return 2
    (run_dir / "normalized.json").write_text(
        json.dumps(document, indent=2, allow_nan=False) + "\n",
        encoding="utf-8")
    print(f"bench: {run_dir / 'normalized.json'}: {len(rows)} measurement(s) "
          f"from {count} suite(s)")
    return 0


# --- pair: three-state comparison of two runs -------------------------------

_STATUS_RANK = {"pass": 0, "warn": 1, "fail": 2, "incomparable": 3}
_SEVERITY = {"auto": "fail", "fail": "fail", "warn": "warn", "off": "info"}


def _severity_for(gate_table):
    return _SEVERITY[gate_table.get("mode", "auto")]


def _raw_name(name, suite):
    """Authoritative raw file: stdout capture or the runner's --json output."""
    return suite.get("stdout") or f"{name}.json"


def _gate_argv(suite, gate_table):
    """Preset gate table -> the suite comparator's uniform gate flags."""
    argv = ["--gate", gate_table.get("mode", "auto")]
    if "percent" in gate_table:
        argv += ["--max-regression-percent", str(gate_table["percent"])]
    flag = suite.get("gate_absolute_flag")
    if flag and flag.rsplit("-", 1)[-1] in gate_table:
        # --max-regression-ns <- ns, --max-regression-ms <- ms,
        # --max-growth-bytes <- bytes; absent keys mean "not calibrated yet".
        argv += [flag, str(gate_table[flag.rsplit("-", 1)[-1]])]
    return argv


def _rev_label(identity):
    revision = identity.get("revision") or ""
    return revision[:9] if revision else "unknown"


def _merge_conservative(rows):
    """One row per metric: A/B/B/A compares each metric twice; keep the worse
    status so a regression visible in either bracket is not averaged away."""
    merged = {}
    for row in rows:
        key = (row.get("suite"), row.get("case"), row.get("metric"))
        kept = merged.get(key)
        if kept is None or _STATUS_RANK[row["status"]] > _STATUS_RANK[kept["status"]]:
            merged[key] = row
    return list(merged.values())


def _compare_native(name, suite, before, after, gate_table, out_json):
    """Run one suite comparator in-process; returns (exit code, crash reason).
    Its own tested invariants (workload hash, context, case sets) apply."""
    from report import normalize
    module = normalize._suite_module(name)
    argv = [str(before), str(after), "--json-out", str(out_json),
            *_gate_argv(suite, gate_table)]
    captured = io.StringIO()
    try:
        with contextlib.redirect_stdout(captured):
            rc = module.main(argv)
    except SystemExit as error:
        rc = error.code if isinstance(error.code, int) else 2
    except Exception as error:  # comparator crash = invalid evidence
        return 2, f"{name}: comparator crashed: {error}"
    rc = 0 if rc is None else int(rc)
    if rc not in (0, 1, 2):
        return 2, f"{name}: comparator exited {rc}"
    if rc == 2 and captured.getvalue().strip():
        print(f"bench: {name} comparator: "
              f"{captured.getvalue().strip().splitlines()[-1]}")
    return rc, None


def _compare_owner_pass(before, after, severity):
    """owner_pass has no comparator: its deterministic contract fields are
    compared as exact measurements from both sides' raw evidence."""
    from report import normalize, schema
    docs = []
    for run_dir in (before, after):
        identity = json.loads((run_dir / "manifest.json").read_text(
            encoding="utf-8"))
        rows = normalize.normalize("owner_pass", run_dir / "raw" / "owner_pass")
        docs.append(schema.document(identity.get("testbed"),
                                    identity.get("revision"),
                                    identity.get("environment") or {}, rows))
    return schema.compare(docs[0], docs[1], severity=severity)


def _compare_runs(before, after, suites, gate_table):
    """All gate rows plus reasons across one (before, after) bracket."""
    from report import gate
    rows, reasons = [], []
    severity = _severity_for(gate_table)
    with tempfile.TemporaryDirectory(prefix="bench-pair-") as tmp:
        work = Path(tmp)
        for name, suite in suites:
            raw = _raw_name(name, suite)
            left = before / "raw" / name / raw
            right = after / "raw" / name / raw
            if not left.is_file() or not right.is_file():
                missing = "before" if not left.is_file() else "after"
                reason = f"{name}: missing raw report on the {missing} side"
                reasons.append(reason)
                rows.append(gate.incomparable(name, "-", "-", "", reason))
                continue
            if not suite.get("compare"):  # owner_pass: normalized exact gate
                try:
                    suite_rows, suite_reasons = _compare_owner_pass(
                        before, after, severity)
                except (OSError, ValueError, KeyError, TypeError) as error:
                    reason = f"{name}: {error}"
                    reasons.append(reason)
                    rows.append(gate.incomparable(name, "-", "-", "", reason))
                    continue
                rows.extend(suite_rows)
                reasons.extend(suite_reasons)
                continue
            out_json = work / f"{name}.json"
            rc, crash = _compare_native(name, suite, left, right, gate_table,
                                        out_json)
            if crash or not out_json.is_file():
                reason = crash or (f"{name}: comparator wrote no output "
                                   f"(exit {rc})")
                reasons.append(reason)
                rows.append(gate.incomparable(name, "-", "-", "", reason))
                continue
            payload = json.loads(out_json.read_text(encoding="utf-8"))
            rows.extend(payload.get("results", []))
    return rows, list(dict.fromkeys(reasons))


def _worktree(path, rev):
    result = subprocess.run(["git", "worktree", "add", "--detach", str(path),
                             rev], cwd=REPO, capture_output=True, text=True)
    if result.returncode != 0:
        raise RunFailure(f"git worktree add {rev}: "
                         f"{result.stderr.strip() or result.stdout.strip()}")
    return path


def _worktree_drop(path):
    subprocess.run(["git", "worktree", "remove", "--force", str(path)],
                   cwd=REPO, capture_output=True)


def _invoke_run(tree, dest, args):
    """One isolated `bench.py run` inside a source tree (base or candidate)."""
    if not (tree / "benchmarks" / "bench.py").is_file():
        raise RunFailure(f"{tree} has no benchmarks/bench.py — the revision "
                         "predates the unified benchmark entry point")
    argv = [sys.executable, str(tree / "benchmarks" / "bench.py"), "run",
            "--out", str(dest)]
    if args.suite:
        for name in args.suite:
            argv += ["--suite", name]
    else:
        argv += ["--preset", args.preset]
    if args.jobs:
        argv += ["--jobs", str(args.jobs)]
    code = subprocess.run(argv, cwd=tree).returncode
    if code != 0:
        raise RunFailure(f"run in {tree} exited {code} ({dest})")
    return dest


def _pair_orchestrated(args, suites, label, gate_table):
    """Build base/candidate in isolated trees, alternate, then compare."""
    out = args.out or REPO / "out" / f"pair-{run_id()}"
    try:
        out.mkdir(parents=True)
    except FileExistsError:
        print(f"bench: refusing to overwrite existing output directory {out}")
        return 2
    (out / "runs").mkdir()
    order = ([("base", 1), ("candidate", 1)] if args.order == "ab" else
             [("base", 1), ("candidate", 1), ("candidate", 2), ("base", 2)])
    trees, run_dirs = [], {}
    rows, reasons = [], []
    try:
        base_tree = _worktree(out / "src-base", args.base)
        trees.append(base_tree)
        candidate = args.candidate or "."
        cand_tree = (REPO if candidate == "."
                     else _worktree(out / "src-candidate", candidate))
        if cand_tree is not REPO:
            trees.append(cand_tree)
        for side, attempt in order:
            tree = base_tree if side == "base" else cand_tree
            dest = out / "runs" / f"{side}{attempt}"
            print(f"bench: pair {args.order}: {side} attempt {attempt}")
            _invoke_run(tree, dest, args)
            run_dirs[(side, attempt)] = dest
        brackets = ([(("base", 1), ("candidate", 1))] if args.order == "ab"
                    else [(("base", 1), ("candidate", 1)),
                          (("base", 2), ("candidate", 2))])
        for base_key, cand_key in brackets:
            bracket_rows, bracket_reasons = _compare_runs(
                run_dirs[base_key], run_dirs[cand_key], suites, gate_table)
            rows.extend(bracket_rows)
            reasons.extend(bracket_reasons)
    except RunFailure as error:
        print(f"bench: {error}")
        return 2
    finally:
        for tree in trees:
            _worktree_drop(tree)
    rows = _merge_conservative(rows)
    reasons = list(dict.fromkeys(reasons))
    before_manifest = json.loads(
        (run_dirs[("base", 1)] / "manifest.json").read_text(encoding="utf-8"))
    after_manifest = json.loads(
        (run_dirs[("candidate", 1)] / "manifest.json").read_text(
            encoding="utf-8"))
    return _emit_pair(out, rows, reasons, label,
                      before=_rev_label(before_manifest),
                      after=_rev_label(after_manifest),
                      testbed=after_manifest.get("testbed", ""))


def _emit_pair(out, rows, reasons, label, *, before, after, testbed):
    from report import emit, gate
    export = argparse.Namespace(
        json_out=str(out / "comparison.json"),
        markdown_out=str(out / "summary.md"),
        junit_out=str(out / "junit.xml"))
    code = emit(export, rows, reasons=reasons,
                title=f"Wirelink benchmark comparison ({label})",
                before=before, after=after, suite=testbed)
    counts = gate.verdict(rows)
    print(f"bench: {out / 'comparison.json'}: pass={counts['pass']} "
          f"warn={counts['warn']} fail={counts['fail']} "
          f"incomparable={counts['incomparable']}")
    for reason in reasons:
        print(f"bench: not comparable: {reason}")
    print(f"bench: summary: {out / 'summary.md'}  junit: {out / 'junit.xml'}")
    return code


def cmd_pair(args, data, parser):
    if args.preset not in data["presets"]:
        parser.error(f"unknown preset: {args.preset}; known: "
                     f"{', '.join(sorted(data['presets']))}")
    if (args.before is None) != (args.after is None):
        parser.error("--before and --after must be given together")
    if args.base and args.before:
        parser.error("--before/--after and --base/--candidate are exclusive")
    if args.candidate and not args.base:
        parser.error("--candidate requires --base")
    if not args.base and not args.before:
        parser.error("pair needs --before/--after (existing runs) or "
                     "--base/--candidate (revision comparison)")
    suites, label = _select_suites(args, data, parser)
    gate_table = data["presets"][args.preset].get("gate", {"mode": "auto"})
    if args.base:
        return _pair_orchestrated(args, suites, label, gate_table)

    from report import gate
    manifests = {}
    for run_dir in (args.before, args.after):
        path = run_dir / "manifest.json"
        if not path.is_file():
            print(f"bench: {run_dir}: not a run directory "
                  "(manifest.json is required)")
            return 2
        manifests[run_dir] = json.loads(path.read_text(encoding="utf-8"))
    testbed = manifests[args.after].get("testbed", "")
    if manifests[args.before].get("testbed") != testbed:
        reason = (f"testbed: {manifests[args.before].get('testbed')} "
                  f"!= {testbed}")
        rows = [gate.incomparable(name, "-", "-", "", reason)
                for name, _ in suites]
        reasons = [reason]
    else:
        rows, reasons = _compare_runs(args.before, args.after, suites,
                                      gate_table)
    out = args.out or args.after  # SPEC: pair outputs live in the run dir
    out.mkdir(parents=True, exist_ok=True)
    return _emit_pair(out, _merge_conservative(rows), list(dict.fromkeys(reasons)),
                      label, before=_rev_label(manifests[args.before]),
                      after=_rev_label(manifests[args.after]), testbed=testbed)


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
    run = commands.add_parser(
        "run", help="build, execute and collect suites into out/<run-id>/")
    run.add_argument("--preset", default="smoke",
                     help="suites to run when --suite is not given "
                          "(default: smoke)")
    run.add_argument("--suite", action="append", default=None, metavar="NAME",
                     help="run exactly this suite (repeatable)")
    run.add_argument("--out", type=Path, default=None,
                     help="run directory (default: out/<run-id>); never "
                          "overwritten")
    run.add_argument("--testbed", default=None,
                     help="testbed id (default: local-<os>-<arch>)")
    run.add_argument("--jobs", type=int, default=None,
                     help="parallel build jobs (default: cmake default)")
    run.add_argument("--skip-build", action="store_true",
                     help="reuse existing build/bench/<suite> trees")
    report = commands.add_parser(
        "report", help="normalize raw/ into normalized.json (idempotent)")
    report.add_argument("run", type=Path, metavar="RUN_DIR",
                        help="run directory containing manifest.json and raw/")
    pair = commands.add_parser(
        "pair", help="compare two runs: exit 0 pass, 1 regression, "
                     "2 invalid/incomparable")
    pair.add_argument("--preset", default="smoke",
                      help="suites and gate profile (default: smoke)")
    pair.add_argument("--suite", action="append", default=None, metavar="NAME",
                      help="limit to this suite (repeatable; the gate profile "
                           "still comes from --preset)")
    pair.add_argument("--before", type=Path, default=None,
                      help="existing base run directory")
    pair.add_argument("--after", type=Path, default=None,
                      help="existing candidate run directory (comparison "
                           "outputs land here unless --out is given)")
    pair.add_argument("--base", default=None, metavar="REV",
                      help="git revision to build the base side from in an "
                           "isolated worktree")
    pair.add_argument("--candidate", default=None, metavar="REV",
                      help="git revision for the candidate side "
                           "(default: the working tree)")
    pair.add_argument("--order", choices=("ab", "abba"), default="abba",
                      help="alternation for --base: abba brackets the "
                           "candidate between two base runs (default)")
    pair.add_argument("--out", type=Path, default=None,
                      help="output directory (orchestrated mode default: "
                           "out/pair-<run-id>)")
    pair.add_argument("--jobs", type=int, default=None,
                      help="parallel build jobs for orchestrated runs")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        data = load_presets()
    except SystemExit as error:
        if isinstance(error.code, str):  # message from validation → exit 2
            parser.error(error.code)
        raise
    if args.command == "list":
        return cmd_list(args, data)
    if args.command == "report":
        return cmd_report(args, data)
    if args.command == "pair":
        return cmd_pair(args, data, parser)
    if args.command == "doctor":
        if args.preset not in data["presets"]:
            parser.error(f"unknown preset: {args.preset}; "
                         f"known: {', '.join(sorted(data['presets']))}")
        return cmd_doctor(args, data)
    return cmd_run(args, data, parser)


if __name__ == "__main__":
    raise SystemExit(main())
