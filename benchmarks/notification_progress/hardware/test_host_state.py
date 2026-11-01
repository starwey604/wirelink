# SPDX-License-Identifier: Apache-2.0
import unittest
from host_state import lock_state, window_state


class HostStateTests(unittest.TestCase):
    def test_lock_sources_and_unknown(self):
        self.assertTrue(lock_state('no', True))
        self.assertTrue(lock_state('yes', False))
        self.assertFalse(lock_state('no', False))
        self.assertIsNone(lock_state(None, False))
        self.assertIsNone(lock_state('no', None))

    def rows(self):
        return [dict(monotonic_ns=i*1_000_000_000,
                     boottime_ns=i*1_000_000_000+500,
                     locked=False, boot_id='boot') for i in range(4)]

    def test_window_bracketing(self):
        rows = self.rows()
        self.assertEqual(len(window_state(rows, 500_000_000, 2_500_000_000, False)), 4)
        for start, end in [(-1, 1), (1, 4_000_000_000)]:
            with self.assertRaises(ValueError):
                window_state(rows, start, end, False)

    def test_state_boot_gap_and_suspend_rejected(self):
        for key, value in [('locked', True), ('locked', None), ('boot_id', 'new'),
                           ('monotonic_ns', 8_000_000_000),
                           ('boottime_ns', 9_000_000_000)]:
            with self.subTest(key=key, value=value):
                rows = self.rows()
                rows[1][key] = value
                with self.assertRaises(ValueError):
                    window_state(rows, 500_000_000, 2_500_000_000, False)


if __name__ == '__main__':
    unittest.main()
