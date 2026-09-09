# SPDX-License-Identifier: Apache-2.0
import copy
import unittest
import compare


def capture():
    rows = ["rpc_validation_begin_v2,hz=550000000,batch=32"]
    rows += [f"rpc_validation_v2,k={k},s={s},b={b},r={r},n=32,cycles=3200"
             for k, s, b in sorted(compare.GROUPS) for r in range(5)]
    return "\n".join(rows + ["rpc_validation_end_v2,groups=65,samples=325,result=pass"])


def report():
    return {"context": {"workload": "wirelink-rpc-validation-v2"}, "benchmarks": [
        {"name": compare.name(k), "cpu_time": 100, "real_time": 110, "time_unit": "ns",
         "encoded_bytes": 10, "repetition_index": r} for k in compare.GROUPS for r in range(3)]}


class CompareTests(unittest.TestCase):
    def test_valid(self):
        for parsed in (compare.h7(capture()), compare.host(report())):
            self.assertEqual(len(list(compare.compare(parsed, parsed))), 130)

    def test_missing_h7(self):
        with self.assertRaises(ValueError):
            compare.h7(capture().replace("rpc_validation_v2,k=0,s=0,b=0,r=0,n=32,cycles=3200\n", ""))

    def test_h7_duplicate_reset_or_wrong_version(self):
        for text in (capture() + "\n" + capture(), capture().replace("_v2", "_v1")):
            with self.assertRaises(ValueError):
                compare.h7(text)

    def test_host_missing_duplicate_error(self):
        base = report()
        for variant in (base["benchmarks"][:-1], base["benchmarks"] + base["benchmarks"][:1],
                        [{**base["benchmarks"][0], "error_occurred": True}]):
            with self.assertRaises(ValueError):
                compare.host({**base, "benchmarks": variant})

    def test_nonfinite(self):
        for value in (0, -1, float("nan"), float("inf")):
            bad = report()
            bad["benchmarks"][0]["cpu_time"] = value
            with self.assertRaises(ValueError):
                compare.host(bad)

    def test_different_context_or_sizes(self):
        left = report()
        for field, value in (("host_name", "different"), ("workload", "wrong")):
            right = copy.deepcopy(left)
            right["context"][field] = value
            with self.assertRaises(ValueError):
                list(compare.compare(compare.host(left), compare.host(right)))
        right = copy.deepcopy(left)
        for row in right["benchmarks"]:
            row["encoded_bytes"] = 20
        with self.assertRaises(ValueError):
            list(compare.compare(compare.host(left), compare.host(right)))


if __name__ == "__main__":
    unittest.main()
