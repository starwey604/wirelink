# SPDX-License-Identifier: Apache-2.0
"""Current contracts must survive the unified entry point and reject corruption."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bench
from report import normalize


def api_capture():
    module = normalize._suite_module("api", "compare_kernel.py")
    return {"context": {"workload": "wirelink-kernel-api-v1", "clock": "caller_manual_tick"},
        "benchmarks": [{"name": name, "repetition_index": i,
            "cpu_time": 100 + i, "real_time": 120 + i, "time_unit": "ns",
            "calls_per_op": counts[0], "samples_per_op": counts[1],
            "endpoint_bytes": 1900, "fixture_bytes": 14000}
            for name, counts in module.CASES.items() for i in range(3)]}


def mixed_capture():
    module = normalize._suite_module("mixed_traffic", "compare_kernel.py")
    rows = []
    for name, envelope, receiver in sorted(module.EXPECTED):
        request = name in ("drop_request", "loss_backpressure", "saturated_wrap")
        response = name in ("drop_response", "loss_backpressure", "saturated_wrap")
        pressure = name in ("backpressure", "loss_backpressure", "saturated_wrap")
        rpc = name != "telemetry_only"
        rows.append({"format": 2, "clock": "simulated_ms", "duration_ms": 2400,
            "scenario": name, "envelope": envelope, "receiver": receiver,
            "accepted": 100, "received": 90, "max_age_ms": 50,
            "max_arrival_age_ms": 10, "max_gap_ms": 5, "mean_age_ms": 20.5,
            "rpc_completed": 10 if rpc else 0, "rpc_handled": 10 * rpc + 2 * response,
            "rpc_timed_out": 2 * (request + response), "max_rpc_ms": 120 if rpc else 0,
            "dropped_requests": int(request), "dropped_responses": int(response),
            "busy": int(pressure)})
    return rows


def owner_capture():
    return [{"format": "wirelink-owner-kernel-v1", "case": "kernel_command_burst",
        "budget": 8, "delivered": 32, "received": 1, "queue_full": 1,
        "maximum_command_batch": 8, "services": 999},
        {"format": "wirelink-owner-kernel-v1", "case": "kernel_stop_reject",
        "budget": 8, "delivered": 0, "received": 0, "rejected": 32, "stop_calls": 1}]


class CurrentNormalize(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def raw(self, suite, capture):
        directory = self.root / suite
        directory.mkdir(exist_ok=True)
        sequence = isinstance(capture, list)
        path = directory / ("raw.jsonl" if sequence else "raw.json")
        path.write_text("".join(json.dumps(row) + "\n" for row in capture) if sequence else json.dumps(capture))
        return directory, path

    def test_api_counts_repetitions_and_corruption(self):
        directory, path = self.raw("api", api_capture())
        rows = normalize.normalize("api", directory)
        self.assertEqual(len(rows), 7 * 4)
        cpu = next(row for row in rows if row["case"] == "kernel_rpc_4x2" and row["metric"] == "cpu_ns")
        self.assertEqual(cpu["samples"], [100, 101, 102])
        self.assertTrue(all(row["direction"] == "exact" for row in rows if row["metric"].endswith("_bytes")))
        invalid = api_capture(); invalid["benchmarks"][0]["calls_per_op"] = 99
        path.write_text(json.dumps(invalid))
        with self.assertRaises(ValueError): normalize.normalize("api", directory)

    def test_mixed_new_version_has_separate_cases_and_duplicate_invariant(self):
        directory, path = self.raw("mixed_traffic", mixed_capture())
        rows = normalize.normalize("mixed_traffic", directory)
        self.assertEqual(len(rows), 21)
        self.assertTrue(all(row["case"].startswith("kernel-v2/") for row in rows))
        invalid = mixed_capture()
        next(row for row in invalid if row["scenario"] == "drop_response")["rpc_handled"] = 10
        path.write_text("".join(json.dumps(row) + "\n" for row in invalid))
        with self.assertRaises(ValueError): normalize.normalize("mixed_traffic", directory)

    def test_owner_contract_complete_and_services_diagnostic(self):
        directory, path = self.raw("owner_pass", owner_capture())
        rows = normalize.normalize("owner_pass", directory)
        self.assertEqual(len(rows), 6)
        for invalid in (owner_capture()[:1], owner_capture() * 2):
            path.write_text("".join(json.dumps(row) + "\n" for row in invalid))
            with self.assertRaises(ValueError): normalize.normalize("owner_pass", directory)
        invalid = owner_capture(); invalid[0]["queue_full"] = False
        path.write_text("".join(json.dumps(row) + "\n" for row in invalid))
        with self.assertRaises(ValueError): normalize.normalize("owner_pass", directory)

    def test_current_native_comparators_selected_by_registry_and_common_exit_codes(self):
        for name, capture in (("api", api_capture()), ("mixed_traffic", mixed_capture())):
            directory, before = self.raw(name, capture)
            after = directory / before.name.replace("raw", "after")
            after.write_text(before.read_text())
            suite = bench.load_presets()["suites"][name]
            result = directory / "comparison.json"
            code, error = bench._compare_native(name, suite, before, after,
                {"mode": "fail", "percent": 0.0}, result)
            self.assertEqual((code, error), (0, None))
            self.assertEqual(json.loads(result.read_text())["exit_code"], 0)
            worse = copy.deepcopy(capture)
            if name == "api":
                for row in worse["benchmarks"]: row["cpu_time"] *= 2
            else:
                for row in worse: row["max_age_ms"] *= 2
            after.write_text("".join(json.dumps(row) + "\n" for row in worse) if isinstance(worse, list) else json.dumps(worse))
            code, error = bench._compare_native(name, suite, before, after,
                {"mode": "fail", "percent": 0.0}, result)
            self.assertEqual((code, error), (1, None))
            self.assertEqual(json.loads(result.read_text())["exit_code"], 1)


if __name__ == "__main__": unittest.main()
