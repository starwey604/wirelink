# SPDX-License-Identifier: Apache-2.0
import unittest

from compare_h7 import CPU_KEYS, RETRY_KEYS, parse, summarize
from test_compare import fixture
import json


def capture():
    lines = ["mixed_h7_begin_v1,hz=550000000,lto=1,coexist=1,endpoint_bytes=3888,barriers=1"]
    lines += [json.dumps(row) for row in fixture()]
    lines += [f"mixed_cpu_v1,scenario={name},e={e},n=2400,cycles=24000,max=10"
              for name, e in sorted(CPU_KEYS)]
    lines += [f"mixed_retry_v1,m={m},e={e},claim={c},b={b},r={r},n=16,cycles=160,max=10"
              for m, e, c, b, r in sorted(RETRY_KEYS)]
    lines += ["mixed_h7_end_v1,result=pass,traffic_rows=42,cpu_groups=21,retry_samples=360"]
    return "\n".join(lines)


class H7CaptureTests(unittest.TestCase):
    def test_complete(self):
        result = summarize(parse(capture()))
        self.assertEqual(len(result["retry"]), 72)
        self.assertEqual(len(result["cpu"]), 21)

    def test_partial_duplicate_or_corrupt(self):
        text = capture()
        lines = text.splitlines()
        for broken in ("\n".join(lines[:-1]), "\n".join(lines[:44] + lines[45:]),
                       "\n".join(lines[:3] + lines[2:]), text + "\n" + text,
                       text.replace("result=pass", "result=fail"),
                       text.replace("barriers=1", "barriers=0"),
                       text.replace("cycles=160", "cycles=0"),
                       text.replace("n=16", "n=32"), text + "\n\ufffd"):
            with self.assertRaises(ValueError):
                parse(broken)


if __name__ == "__main__":
    unittest.main()
