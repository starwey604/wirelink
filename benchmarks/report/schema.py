# SPDX-License-Identifier: Apache-2.0
"""wirelink-benchmark-v2 normalization: measurements, documents, hashes.

Suite-native outputs stay authoritative in raw/; this module converts them
into the single logical model described in benchmarks/SPEC.md so that
reporting, comparison and CI consume one format.
"""
import hashlib
import json
import math
import statistics
from pathlib import Path

from . import gate

SCHEMA = "wirelink-benchmark-v2"
COMPARISON_SCHEMA = "wirelink-benchmark-comparison-v1"


def measurement(suite, case, metric, unit, direction, samples):
    """Validate one measurement and attach the median used for gating."""
    gate._validate("direction", direction, gate.DIRECTIONS)
    if not suite or not case or not metric or not unit:
        raise ValueError("suite, case, metric and unit are required")
    values = list(samples)
    if not values:
        raise ValueError(f"{suite}/{case}/{metric}: no samples")
    if any(type(value) not in (int, float) or not math.isfinite(value)
           for value in values):
        raise ValueError(f"{suite}/{case}/{metric}: non-finite sample")
    return {"suite": suite, "case": case, "metric": metric, "unit": unit,
            "direction": direction, "samples": values,
            "median": statistics.median(values)}


def document(testbed, revision, environment, measurements):
    """Assemble and validate a wirelink-benchmark-v2 document."""
    if not testbed:
        raise ValueError("testbed id is required; reports without one are not comparable")
    if not isinstance(environment, dict):
        raise ValueError("environment must be an object")
    rows = list(measurements)
    if not rows:
        raise ValueError("no measurements")
    keys = set()
    for row in rows:
        if (row["suite"], row["case"], row["metric"]) in keys:
            raise ValueError(f"duplicate measurement: {row['suite']}/{row['case']}/{row['metric']}")
        keys.add((row["suite"], row["case"], row["metric"]))
    return {"schema": SCHEMA, "testbed": testbed, "revision": revision,
            "environment": environment, "measurements": rows}


def load(path):
    """Read a normalized document and reject anything else."""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        raise ValueError(f"{path}: not a {SCHEMA} document")
    if not data.get("testbed") or not data.get("measurements"):
        raise ValueError(f"{path}: missing testbed or measurements")
    return data


def checkout_relative(path):
    """Identity for one source path: relative to its checkout root.

    Absolute paths would break every orchestrated pair: the base side
    runs in a worktree, where identical sources sit under a different
    prefix. The nearest ``.git`` marks the root of a main tree, a
    worktree (``.git`` file) or a CI checkout alike.
    """
    resolved = Path(path).resolve()
    for parent in resolved.parents:
        if (parent / ".git").exists():
            return str(resolved.relative_to(parent))
    return str(resolved)


def workload_hash(paths):
    """Stable hash over the workload's own sources.

    Each entry contributes its checkout-relative path and its bytes, so
    the same sources hash the same from a worktree and from the main
    tree — orchestrated ``pair`` compares exactly that way. Two reports
    whose benchmark_source_hash differs were produced by different
    benchmark code and must never be compared: the pair is
    incomparable, not regressing.
    """
    digest = hashlib.sha256()
    for key, path in sorted((checkout_relative(p), Path(p)) for p in paths):
        digest.update(key.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def incomparable_reasons(base, candidate):
    """Reasons these two documents must not be compared; empty means they may."""
    reasons = []
    if base.get("testbed") != candidate.get("testbed"):
        reasons.append(f"testbed: {base.get('testbed')} != {candidate.get('testbed')}")
    left = base.get("environment", {}).get("benchmark_source_hash")
    right = candidate.get("environment", {}).get("benchmark_source_hash")
    if left is not None and right is not None and left != right:
        reasons.append("benchmark workload source hash changed")
    left = {(row["suite"], row["case"], row["metric"], row["unit"],
             row["direction"]) for row in base["measurements"]}
    right = {(row["suite"], row["case"], row["metric"], row["unit"],
              row["direction"]) for row in candidate["measurements"]}
    if left != right:
        missing = sorted(left - right)
        added = sorted(right - left)
        if missing:
            reasons.append(f"measurements missing in candidate: {missing[:3]}")
        if added:
            reasons.append(f"measurements added by candidate: {added[:3]}")
    return reasons


def compare(base, candidate, *, rel=None, absolute=None, severity="fail",
            before="base", after="candidate"):
    """Gate two normalized documents metric by metric.

    Returns (rows, reasons): rows are gate result dicts, reasons are the
    incomparability blockers. When reasons is non-empty the caller must not
    present numbers as a regression; rows then carry status incomparable.
    """
    reasons = incomparable_reasons(base, candidate)
    if reasons:
        rows = [gate.incomparable(row["suite"], row["case"], row["metric"],
                                  row["unit"], "; ".join(reasons), severity=severity)
                for row in base["measurements"]]
        return rows, reasons

    left = {(row["suite"], row["case"], row["metric"]): row
            for row in base["measurements"]}
    right = {(row["suite"], row["case"], row["metric"]): row
             for row in candidate["measurements"]}
    rows = []
    for key in sorted(left):
        b, c = left[key], right[key]
        if len(b["samples"]) != len(c["samples"]):
            rows.append(gate.incomparable(b["suite"], b["case"], b["metric"],
                                          b["unit"],
                                          f"repetition count changed: "
                                          f"{len(b['samples'])} -> {len(c['samples'])}",
                                          severity=severity))
            continue
        rows.append(gate.evaluate(b["median"], c["median"], suite=b["suite"],
                                  case=b["case"], metric=b["metric"],
                                  unit=b["unit"], direction=b["direction"],
                                  rel=rel, absolute=absolute, severity=severity))
    return rows, []


def comparison_document(rows, *, testbed, before, after, reasons=()):
    """Assemble the machine-readable comparison.json payload."""
    return {"schema": COMPARISON_SCHEMA, "testbed": testbed,
            "before": before, "after": after, "results": rows,
            "reasons": list(reasons), "verdict": gate.verdict(rows),
            "exit_code": gate.exit_code(rows)}
