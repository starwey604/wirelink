# SPDX-License-Identifier: Apache-2.0
import copy
import unittest
from compare import compare, measurements


def report():
    return {"format": "wirelink-performance-v1", "context": {
        "system": "test", "machine": "test", "host": "test", "samples": 100, "warmup": 10},
        "udp": [], "micro": {"one": {"context": {"workload": "wirelink-api-v1"}, "benchmarks": [
            {"name": "Rpc/32/1", "cpu_time": n, "real_time": n * 2, "time_unit": "ns",
             "endpoint_bytes": 1000, "runtime_arena_bytes": 500} for n in [10, 11, 90]]}}}


class Reports(unittest.TestCase):
    def test_uses_median_not_outlier(self):
        self.assertEqual(measurements(report())[("one/Rpc/32/1", "cpu_time")], 11)

    def test_normalizes_units_and_compares(self):
        before, after = report(), report()
        for row in after["micro"]["one"]["benchmarks"]:
            row["time_unit"] = "us"
            row["cpu_time"] /= 1000
            row["real_time"] /= 1000
        self.assertTrue(all(abs(row[4]) < 1e-9 for row in compare(before, after)))

    def test_rejects_failures_missing_rows_and_mixed_context(self):
        for change in ["error", "missing", "context", "nan"]:
            before, after = report(), copy.deepcopy(report())
            if change == "error":
                after["micro"]["one"]["benchmarks"][0]["error_occurred"] = True
            elif change == "missing":
                after["micro"]["one"]["benchmarks"] = []
            elif change == "context":
                after["context"]["host"] = "different"
            else:
                after["micro"]["one"]["benchmarks"][0]["cpu_time"] = float("nan")
            with self.assertRaises(ValueError):
                compare(before, after)

    def test_build_flags_must_match_but_abi_and_binary_may_change(self):
        before, after = report(), report()
        before["context"]["build"] = {"CMAKE_C_FLAGS_RELEASE": "-O3", "WIRELINK_WLC_CODEGEN_ABI": "27"}
        after["context"]["build"] = {"CMAKE_C_FLAGS_RELEASE": "-O3", "WIRELINK_WLC_CODEGEN_ABI": "28"}
        before["artifacts"], after["artifacts"] = {"one": "before"}, {"one": "after"}
        self.assertTrue(compare(before, after))
        after["context"]["build"]["CMAKE_C_FLAGS_RELEASE"] = "-O0"
        with self.assertRaises(ValueError):
            compare(before, after)


if __name__ == "__main__":
    unittest.main()
