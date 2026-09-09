# SPDX-License-Identifier: Apache-2.0
import unittest
import copy

from executor_run import parse_stages
from executor_analyze import summarize


class ExecutorReportTests(unittest.TestCase):
    def test_stage_name_is_not_a_numeric_field(self):
        stages = parse_stages([
            "wirelink_host_profile_v1,cpu_clock=thread",
            "wirelink_host_profile_v1,stage=rpc_admit_wait,cycle=0,count=2,wall_ns=30,cpu_ns=0,max_ns=20,slow=0",
            "wirelink_host_profile_v1,stage=usb_rx,cycle=0,count=0,wall_ns=0,cpu_ns=0,max_ns=0,slow=0",
        ])
        self.assertEqual(stages["rpc_admit_wait"]["wall_ns"], 30)
        self.assertEqual(stages["rpc_admit_wait"]["count"], 2)
        self.assertEqual(stages["usb_rx"]["count"], 0)
        self.assertNotIn("stage", stages["rpc_admit_wait"])

    def test_off_has_no_stages(self):
        self.assertEqual(parse_stages([]), {})

    def fixture(self):
        rows = []
        for repeat, value in enumerate([10, 12, 100]):
            row = {"profile": "locks", "errors": 0, "calls": 100, "abi": 28,
                   "mode": "rpc", "producers": 1, "bytes": 32, "period_us": 0,
                   "repetition": repeat, "stages": {"rpc_admit_wait": {
                       "count": 50, "wall_ns": value * 50, "max_ns": value * 2}}}
            for key in ["wall_ns", "cpu_ns_per_call", "calls_per_second", "p50_ns", "p95_ns",
                        "p99_ns", "max_ns", "dispatched", "coalesced"]:
                row[key] = value
            rows.append(row)
        return {"format": "wirelink-executor-performance-v1", "profile": "locks",
                "host": "test", "system": "test", "binary_sha256": "test", "runs": rows}

    def test_repetition_median_and_per_acquisition_vs_per_call(self):
        row = summarize(self.fixture())["groups"][0]
        self.assertEqual(row["metrics"]["p99_ns"]["median"], 12)
        self.assertEqual(row["stages"]["rpc_admit_wait"]["mean_ns"]["median"], 12)
        self.assertEqual(row["stages"]["rpc_admit_wait"]["ns_per_call"]["median"], 6)
        self.assertEqual(row["stages"]["rpc_admit_wait"]["max_ns"], 200)

    def test_rejects_invalid_or_mixed_runs(self):
        for field, value in [("errors", 1), ("calls", 0), ("abi", 27), ("profile", "off"),
                             ("repetition", 1), ("stages", {}), ("p99_ns", float("nan"))]:
            report = copy.deepcopy(self.fixture())
            report["runs"][0][field] = value
            with self.assertRaises(ValueError):
                summarize(report)

    def activity_fixture(self):
        report = self.fixture()
        for row in report["runs"]:
            row.update(activity_enabled=True, latest_budget=4, endpoint_steps=400,
                       sink_samples=row["dispatched"], sink_p50_ns=40, sink_p99_ns=80,
                       activity={"passes": 100, "rpc_jobs": 100, "rpc_batches": 25})
        return report

    def test_activity_and_sink_age_are_separate_from_submit_latency(self):
        item = summarize(self.activity_fixture())["groups"][0]
        self.assertEqual(item["metrics"]["passes_per_call"]["median"], 1)
        self.assertEqual(item["metrics"]["rpc_jobs_per_batch"]["median"], 4)
        self.assertEqual(item["metrics"]["sink_p99_ns"]["median"], 80)
        self.assertEqual(item["metrics"]["p99_ns"]["median"], 12)

    def test_rejects_mixed_or_incomplete_activity(self):
        for field, value in [("activity_enabled", False), ("latest_budget", 1),
                             ("activity", {}), ("sink_samples", 0)]:
            report = self.activity_fixture()
            report["runs"][0][field] = value
            with self.assertRaises(ValueError):
                summarize(report)
        report = self.activity_fixture()
        del report["runs"][0]["sink_p50_ns"]
        with self.assertRaises(ValueError):
            summarize(report)

    def test_disabled_activity_does_not_report_zero_cost(self):
        report = self.activity_fixture()
        for row in report["runs"]:
            row["activity_enabled"] = False
            row["activity"] = dict.fromkeys(row["activity"], 0)
        item = summarize(report)["groups"][0]
        self.assertNotIn("activity", item)
        self.assertNotIn("passes_per_call", item["metrics"])


if __name__ == "__main__":
    unittest.main()
