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


def _suite_module(name):
    """Load benchmarks/<name>/compare.py under a unique module name."""
    path = BENCH / name / "compare.py"
    spec = importlib.util.spec_from_file_location(f"wirelink_bench_{name}_compare",
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
        _context, groups = module.read(path)
        for case in sorted(groups):
            for metric in ("cpu_ns", "wall_ns"):
                rows.append(schema.measurement(
                    suite, case, metric, "ns", "lower", groups[case][metric]))
    return rows


def _api(raw_dir):
    module = _suite_module("api")
    paths = _reports(raw_dir, ".json")
    report = json.loads(paths[0].read_text(encoding="utf-8"))
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
                "footprint", row.group, row.item, module.UNITS.get(row.item, "bytes"),
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
    module = _suite_module("mixed_traffic")
    rows = []
    for path in _reports(raw_dir, ".jsonl"):
        capture = module.load(path)
        for scenario in module.SCENARIOS:
            for metric in module.SUMMARY_METRICS:
                rows.append(schema.measurement(
                    "mixed_traffic", scenario, metric, "ms", "lower",
                    [float(module.summary(capture, scenario, metric))]))
    return rows


def _owner_pass(raw_dir):
    rows = []
    for path in _reports(raw_dir, ".jsonl"):
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
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
