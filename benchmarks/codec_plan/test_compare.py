# SPDX-License-Identifier: Apache-2.0
import unittest
import compare


def capture(lto=1):
    rows = [f"codec_plan_begin_v1,hz=550000000,lto={lto},barriers=1"]
    rows += [f"codec_plan_v1,m={m},k={k},p={p},s={s},r={r},n={36 if m == 2 else 32},cycles=3200"
             for m, k, p, s in sorted(compare.H7) for r in range(5)]
    return "\n".join(rows + ["codec_plan_end_v1,groups=225,samples=1125,result=pass"])


class Tests(unittest.TestCase):
    def test_complete(self):
        parsed = compare.h7(capture())
        self.assertEqual(len(list(compare.compare(parsed, parsed))), 450)

    def test_reject_reset_loss_error_and_batch(self):
        lines = capture().splitlines()
        for text in (capture() + "\n" + capture(), "\n".join(lines[:3] + lines[4:]),
                     capture().replace("result=pass", "result=fail"), capture().replace("n=36", "n=32")):
            with self.assertRaises(ValueError):
                compare.h7(text)

    def test_lto_mismatch(self):
        with self.assertRaises(ValueError):
            list(compare.compare(compare.h7(capture(0)), compare.h7(capture(1))))

    def test_host_complete_and_missing(self):
        data = {"context": {"workload": "wirelink-codec-plan-v1"}, "benchmarks": [
            {"name": f"k{k}/p{p}/s{s}", "cpu_time": 100, "real_time": 101,
             "time_unit": "ns", "encoded_bytes": 5} for k, p, s in compare.HOST]}
        parsed = compare.host(data)
        self.assertEqual(len(list(compare.compare(parsed, parsed))), 216)
        data["benchmarks"].pop()
        with self.assertRaises(ValueError):
            compare.host(data)


if __name__ == "__main__":
    unittest.main()
