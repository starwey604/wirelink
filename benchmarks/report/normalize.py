# SPDX-License-Identifier: Apache-2.0
"""Convert suite-native raw outputs into wirelink-benchmark-v2 measurements.

raw/ stays authoritative: each suite's own compare.py parser is reused here
(loaded by path under a unique module name) so normalization cannot drift
from what the comparator accepts, and a raw file the parser rejects makes
the whole report invalid (exit 2), never a partial verdict.
"""
import importlib.util
import json
import sys
from pathlib import Path

from . import schema

BENCH = Path(__file__).resolve().parents[1]
GB_SUITES = ("framing", "codec_plan", "rpc_validation")
# owner_pass rows carry racy activity counters by design; only the
# deterministic contract fields are normalized (see owner_pass/README.md).
OWNER_PASS_METRICS = ("budget", "delivered", "received")


def _suite_module(name, filename="compare.py"):
    """Load benchmarks/<name>/compare.py under a unique module name."""
    if filename not in ("compare.py", "compare_kernel.py"):
        raise ValueError("unsupported comparator entry")
    path = BENCH / name / filename
    spec = importlib.util.spec_from_file_location(f"wirelink_bench_{name}_{Path(filename).stem}",
                                                  path)
    if spec is None or spec.loader is None:
        raise ValueError(f"no comparator for suite {name!r}")
    if str(BENCH) not in sys.path:
        sys.path.insert(0, str(BENCH))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _reports(raw_dir, suffix):
    paths = sorted(Path(raw_dir).glob(f"*{suffix}"))
    if not paths:
        raise ValueError(f"{raw_dir}: no *{suffix} report")
    return paths


def _gb(suite, raw_dir):
    module = _suite_module(suite)
    rows = []
    for path in _reports(raw_dir, ".json"):
        context, groups = module.read(path)
        prefix = "v3/" if context.get("workload") in ("wirelink-framing-v3", "wirelink-codec-validation-v3") else ""
        for case in sorted(groups):
            for metric in ("cpu_ns", "wall_ns"):
                rows.append(schema.measurement(
                    suite, prefix + case, metric, "ns", "lower", groups[case][metric]))
    return rows


def _api(raw_dir):
    module = _suite_module("api")
    paths = _reports(raw_dir, ".json")
    report = json.loads(paths[0].read_text(encoding="utf-8"))
    if report.get("context", {}).get("workload") == "wirelink-kernel-api-v1":
        current = _suite_module("api", "compare_kernel.py")
        rows = []
        for path in paths:
            _context, groups = current.load(path)
            for case, metrics in sorted(groups.items()):
                for metric, values in metrics.items():
                    rows.append(schema.measurement("api", case, metric, current.UNITS[metric],
                        "exact" if metric.endswith("_bytes") else "lower", values))
        return rows
    if report.get("format") != "wirelink-performance-v1":
        raise ValueError("unsupported API report workload")
    if len(paths) != 1:
        raise ValueError("historical API aggregate requires exactly one report")
    # Group exactly like api measurements(): same filters, units and keys,
    # but keep every repetition instead of collapsing to the median.
    groups = {}
    for variant, data in report["micro"].items():
        for row in data["benchmarks"]:
            if row.get("error_occurred"):
                raise ValueError(f"failed benchmark: {variant}/{row['name']}")
            if row.get("run_type", "iteration") != "iteration":
                continue
            scale = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}[row["time_unit"]]
            name = row.get("run_name", row["name"])
            for metric in ("cpu_time", "real_time", "endpoint_bytes",
                           "runtime_arena_bytes"):
                value = row[metric] * (scale if metric.endswith("time") else 1)
                if value <= 0:
                    raise ValueError(f"invalid measurement: {variant}/{name}/{metric}")
                groups.setdefault((f"{variant}/{name}", metric), []).append(value)
    for row in report["udp"]:
        for metric, value in [(key, row[key]) for key in
                              ["p50_ns", "p95_ns", "p99_ns", "max_ns",
                               "cpu_ns_per_call"]] + [
                                  ("server_cpu_ns_per_call",
                                   row["server"]["cpu_ns_per_call"])]:
            if value <= 0:
                raise ValueError(f"invalid UDP measurement: {metric}")
            groups.setdefault((f"udp/{row['payload_bytes']}B", metric), []).append(value)
    if not groups:
        raise ValueError(f"{paths[0]}: empty performance report")
    rows = []
    for (case, metric), samples in sorted(groups.items()):
        direction = "exact" if metric.endswith("_bytes") else "lower"
        rows.append(schema.measurement(
            "api", case, metric, module.unit_of(metric), direction, samples))
    return rows


def _footprint(raw_dir):
    module = _suite_module("footprint")
    rows = []
    for path in _reports(raw_dir, ".json"):
        report = module.load(str(path))
        # compare(report, report) yields this single report's rows.
        for row in module.compare(report, report):
            exact = row.group in module.EXACT_GROUPS or row.group.startswith("storage ")
            rows.append(schema.measurement(
                "footprint", ("v2/" if report.get("schema") == "wirelink-footprint-v2" else "") + row.group, row.item, module.UNITS.get(row.item, "bytes"),
                "exact" if exact else "lower", [float(row.after)]))
    return rows


def _reliable_loss(raw_dir):
    module = _suite_module("reliable_loss")
    rows = []
    for path in _reports(raw_dir, ".json"):
        report = module.load(str(path))
        for case in report["cases"]:
            label = module.loss_label(case["loss_ppm"])
            for metric in module.LOWER_IS_BETTER + module.HIGHER_IS_BETTER:
                rows.append(schema.measurement(
                    "reliable_loss", label, metric, module.UNITS[metric],
                    module.DIRECTIONS[metric], [float(case.get(metric, 0))]))
    return rows


def _mixed_traffic(raw_dir):
    rows = []
    for path in _reports(raw_dir, ".jsonl"):
        raw = [json.loads(line) for line in path.read_text().splitlines()]
        current = bool(raw and raw[0].get("format") == 2)
        module = _suite_module("mixed_traffic", "compare_kernel.py" if current else "compare.py")
        capture = module.validate(raw)
        for scenario in module.SCENARIOS:
            for metric in module.SUMMARY_METRICS:
                rows.append(schema.measurement(
                    "mixed_traffic", ("kernel-v2/" if current else "") + scenario, metric, "ms", "lower",
                    [float(module.summary(capture, scenario, metric))]))
    return rows


def _owner_pass(raw_dir):
    rows = []
    for path in _reports(raw_dir, ".jsonl"):
        capture = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        if any("format" in row for row in capture):
            expected = {
                "kernel_command_burst": {"budget": 8, "delivered": 32, "received": 1,
                    "queue_full": 1, "maximum_command_batch": 8},
                "kernel_stop_reject": {"budget": 8, "delivered": 0, "received": 0,
                    "rejected": 32, "stop_calls": 1}}
            if len(capture) != 2 or {row.get("case") for row in capture} != set(expected):
                raise ValueError("incomplete current owner model")
            for row in capture:
                if row.get("format") != "wirelink-owner-kernel-v1" or any(
                    type(row.get(key)) is not int or row[key] != value
                    for key, value in expected[row["case"]].items()):
                    raise ValueError("invalid current owner contract")
        for row in capture:
            case = row.get("case")
            if not isinstance(case, str) or not case:
                raise ValueError(f"{path}: row without case")
            for metric in OWNER_PASS_METRICS:
                value = row.get(metric)
                if type(value) is not int or value < 0:
                    raise ValueError(f"{path}: invalid {metric} for {case}")
                rows.append(schema.measurement(
                    "owner_pass", case, metric, "count", "exact", [float(value)]))
    return rows


NORMALIZERS = {
    "api": _api,
    "footprint": _footprint,
    "mixed_traffic": _mixed_traffic,
    "owner_pass": _owner_pass,
    "reliable_loss": _reliable_loss,
    **{name: (lambda raw, name=name: _gb(name, raw)) for name in GB_SUITES},
}


def normalize(suite, raw_dir):
    """All measurements for one suite's raw directory; ValueError if invalid."""
    try:
        normalizer = NORMALIZERS[suite]
    except KeyError:
        raise ValueError(f"no normalizer for suite {suite!r}")
    return normalizer(Path(raw_dir))
