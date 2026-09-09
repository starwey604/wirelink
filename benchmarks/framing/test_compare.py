# SPDX-License-Identifier: Apache-2.0
import copy
import itertools
import unittest
import compare


def host_report():
    return {"context": {"workload": "wirelink-framing-v1", "host_name": "test"},
            "benchmarks": [{"name": "busy3/e0/i0/p2/b120", "run_type": "iteration",
                            "repetition_index": i, "cpu_time": 100 + i, "real_time": 120 + i,
                            "time_unit": "ns", "sink_calls_per_op": 4, "context_bytes": 896}
                           for i in range(3)]}


def h7_report():
    rows = ["boot: framing_h7_begin_v1,hz=550000000,context_bytes=896,irq_masked_batch=32"]
    for mode, e, p, b, r in itertools.product(compare.MODES, range(2), range(3), (32, 120, 512, 2048), range(5)):
        rows.append(f"framing_h7_v1,mode={mode},e={e},i=0,p={p},b={b},r={r},n=32,cycles=17600")
    rows.append("framing_h7_end_v1,result=pass,groups=168,samples=840")
    return "\n".join(rows)


class Reports(unittest.TestCase):
    def test_host_medians_and_units(self):
        before = host_report()
        after = copy.deepcopy(before)
        for row in after["benchmarks"]:
            row["time_unit"] = "us"
            row["cpu_time"] /= 2000
            row["real_time"] /= 1000
        rows = compare.compare(compare.host(before), compare.host(after))
        self.assertEqual(rows[0][2:], (101, 50.5, -50))
        self.assertEqual(rows[1][2:], (121, 121, 0))

    def test_host_failed_aggregate_not_ignored(self):
        report = host_report()
        report["benchmarks"].append({"name": "failed", "run_type": "aggregate", "error_occurred": True})
        with self.assertRaises(ValueError):
            compare.host(report)

    def test_host_invalid_measurements_and_invariants(self):
        for key, value in (("cpu_time", float("nan")), ("real_time", 0),
                           ("context_bytes", 900), ("sink_calls_per_op", 3)):
            report = host_report()
            report["benchmarks"][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                compare.host(report)

    def test_host_duplicate_or_missing_repeats(self):
        report = host_report()
        report["benchmarks"].append(report["benchmarks"][0])
        with self.assertRaises(ValueError):
            compare.host(report)
        report = host_report()
        del report["benchmarks"][1]
        with self.assertRaises(ValueError):
            compare.host(report)

    def test_compare_context_or_group_mismatch(self):
        before = compare.host(host_report())
        after = copy.deepcopy(before)
        after[0]["host_name"] = "different"
        with self.assertRaises(ValueError):
            compare.compare(before, after)
        after = copy.deepcopy(before)
        after[1].clear()
        with self.assertRaises(ValueError):
            compare.compare(before, after)

    def test_h7_complete_matrix_and_units(self):
        report = compare.h7(h7_report())
        self.assertEqual(len(report[1]), 168)
        self.assertEqual(report[1]["spare/e0/i0/p0/b32"]["cycles"], [550] * 5)
        self.assertEqual(report[1]["spare/e0/i0/p0/b32"]["cpu_ns"], [1000] * 5)
        self.assertTrue(all(row[4] == 0 for row in compare.compare(report, report)))

    def test_h7_missing_record_or_footer(self):
        lines = h7_report().splitlines()
        for index in (0, 100, -1):
            damaged = lines.copy()
            del damaged[index]
            with self.subTest(index=index), self.assertRaises(ValueError):
                compare.h7("\n".join(damaged))

    def test_h7_duplicate_or_reset(self):
        text = h7_report()
        for damaged in (text + "\n" + text, text.replace("framing_h7_end_v1", text.splitlines()[1] + "\nframing_h7_end_v1")):
            with self.assertRaises(ValueError):
                compare.h7(damaged)

    def test_h7_invalid_batch_or_cycle(self):
        for old, new in (("n=32", "n=31"), ("cycles=17600", "cycles=0"),
                         ("hz=550000000", "hz=0"), ("result=pass", "result=fail")):
            with self.subTest(old=old), self.assertRaises(ValueError):
                compare.h7(h7_report().replace(old, new, 1))


if __name__ == "__main__":
    unittest.main()
