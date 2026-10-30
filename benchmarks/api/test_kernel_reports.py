# SPDX-License-Identifier: Apache-2.0
import copy
import unittest
import compare_kernel as current


def report():
    return {"context": {"workload": "wirelink-kernel-api-v1", "clock": "caller_manual_tick"},
            "benchmarks": [{"name": "kernel_rpc_4x2", "repetition_index": i,
                            "cpu_time": 100 + i, "real_time": 120 + i, "time_unit": "ns",
                            "calls_per_op": 8, "samples_per_op": 0,
                            "endpoint_bytes": 1900, "fixture_bytes": 14000} for i in range(3)]}


class Reports(unittest.TestCase):
    def test_units_medians_and_resource_fields(self):
        before = report(); after = copy.deepcopy(before)
        for row in after["benchmarks"]:
            row["cpu_time"] /= 2000; row["real_time"] /= 1000; row["time_unit"] = "us"
        rows = current.compare(current.host(before), current.host(after))
        self.assertIn(("kernel_rpc_4x2", "cpu_ns", 101, 50.5), rows)
        self.assertIn(("kernel_rpc_4x2", "endpoint_bytes", 1900, 1900), rows)

    def test_failure_bad_counts_duplicate_missing_and_layout(self):
        variants = []
        for key, value in (("calls_per_op", 4), ("samples_per_op", True), ("cpu_time", float("nan")),
                           ("fixture_bytes", 100), ("endpoint_bytes", 1901), ("error_occurred", True)):
            broken = report(); broken["benchmarks"][0][key] = value; variants.append(broken)
        broken = report(); broken["benchmarks"].pop(1); variants.append(broken)
        broken = report(); broken["benchmarks"].append(broken["benchmarks"][0]); variants.append(broken)
        broken = report(); broken["context"]["workload"] = "wirelink-api-v1"; variants.append(broken)
        for broken in variants:
            with self.assertRaises(ValueError): current.host(broken)

    def test_source_hash_and_machine_mismatch(self):
        before = report(); after = copy.deepcopy(before)
        before["context"]["benchmark_source_hash"] = "a" * 64
        after["context"]["benchmark_source_hash"] = "b" * 64
        with self.assertRaises(ValueError): current.compare(current.host(before), current.host(after))
        after = report(); after["context"]["host_name"] = "another"
        with self.assertRaises(ValueError): current.compare(current.host(report()), current.host(after))


if __name__ == "__main__": unittest.main()
