# SPDX-License-Identifier: Apache-2.0
import json
from pathlib import Path
import sys
import unittest
import compare_kernel_firmware as current
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from report.test_current_normalize import mixed_capture


def capture():
    return "\n".join(["*** Booting Zephyr OS test", "mixed_kernel_fw_begin_v1,hz=240000000,lto=0,endpoint_bytes=4096,barriers=1",
        *(json.dumps(row, separators=(",", ":")) for row in mixed_capture()),
        *(f"mixed_kernel_cpu_v1,scenario={name},e={envelope},n=2400,cycles=240000,max=1000"
          for name, envelope in sorted(current.CPU_KEYS)),
        "mixed_kernel_fw_end_v1,result=pass,traffic_rows=42,cpu_groups=21"])


class Firmware(unittest.TestCase):
    def test_complete_and_units(self):
        context, traffic, cpu = current.parse(capture())
        self.assertEqual((len(traffic), len(cpu)), (42, 21))
        self.assertEqual(cpu['rpc_clean', 0]['cpu_ns_per_tick'], 1000 / 2.4)
        self.assertEqual(context['barriers'], 1)

    def test_incomplete_reset_old_version_duplicate_bad_cycles_and_missing_effect(self):
        base = capture()
        invalid = [base.rsplit("\n", 1)[0], base + "\n" + base,
            base.replace("mixed_kernel_fw_begin_v1", "mixed_fw_begin_v1"),
            base.replace('"rpc_handled":12', '"rpc_handled":10'),
            base.replace('n=2400,cycles=240000,max=1000', 'n=2400,cycles=240000,max=1', 1),
            base.replace('barriers=1', 'barriers=0'),
            base.replace('cycles=240000', 'cycles=' + '9' * 400),
            base.replace('"format":2', '"format":1', 1)]
        sample = next(line for line in base.splitlines() if line.startswith('mixed_kernel_cpu_v1,'))
        invalid.append(base.replace(sample, sample + '\n' + sample))
        for text in invalid:
            with self.assertRaises(ValueError): current.parse(text)


if __name__ == "__main__": unittest.main()
