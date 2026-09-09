# SPDX-License-Identifier: Apache-2.0
import unittest
from analyze import parse


def cpu(second, window):
    return (f"[00:00:{second:02d}.000,000] wl_h7_cpu_v1,w={window},ms=2000,hz=1000,owner=100,busy=200,total=2000\n"
            f"[00:00:{second:02d}.000,000] wl_h7_lock_v1,w={window},n=10,wait=1,hold=2,waitmax=1,holdmax=1,on=1\n")


class Windows(unittest.TestCase):
    def test_excludes_warmup_idle_and_partial_intervals(self):
        text = "".join(cpu(second, second // 2) for second in [2, 4, 6, 8, 10, 12])
        # Active interval 3..9 s; only 4..6 and 6..8 s fit fully inside.
        text += "[00:00:10.000,000] willow_wirelink_hil_device_v2,m=work,w=3,f=1,ems=7000,cms=6000,cmd=5000\n"
        result = parse(text)
        self.assertEqual([s["w"] for s in result["samples"]], [3, 4])
        self.assertEqual(result["summary"]["owner_percent"]["median"], 5)

    def test_missing_final_or_lock_record_is_not_success(self):
        with self.assertRaises(ValueError):
            parse(cpu(4, 1))
        final = "[00:00:10.000,000] willow_wirelink_hil_device_v2,m=work,w=3,f=1,ems=9000,cms=8000,cmd=5000\n"
        with self.assertRaises(ValueError):
            parse(cpu(4, 1).splitlines()[0] + "\n" + final)


if __name__ == "__main__":
    unittest.main()
