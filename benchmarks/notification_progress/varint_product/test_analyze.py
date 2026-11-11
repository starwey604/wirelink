# SPDX-License-Identifier: Apache-2.0
import unittest

from analyze import compare_series, validate_handoff
from metric_contract import stream_costs


class ProductGuardTests(unittest.TestCase):
    def rows(self, a, b, observer=1, metric='mcu_stream1_active_us_per_frame'):
        return compare_series(observer, {}, {'A': [{metric: v} for v in a],
                                              'B': [{metric: v} for v in b]})[0]

    def test_primary_requires_stable_on_cpu_not_sample_api(self):
        self.assertTrue(self.rows([100] * 3, [90] * 3)['primary_benefit'])
        self.assertFalse(self.rows([100] * 3, [90] * 3, observer=0)['primary_benefit'])
        self.assertFalse(self.rows([100] * 2, [90] * 2)['primary_benefit'])
        self.assertFalse(self.rows([100] * 3, [90, 100, 90])['primary_benefit'])
        self.assertFalse(self.rows([100] * 3, [90] * 3, metric='sample_api_mean_us')['primary_benefit'])

    def test_stream_cpu_increase_is_bad_goodput_increase_is_good(self):
        self.assertTrue(self.rows([100] * 3, [110] * 3)['repeated_guard_failure'])
        self.assertFalse(self.rows([100] * 3, [110] * 3, metric='stream32-1-h2d')['repeated_guard_failure'])
        self.assertTrue(self.rows([100] * 3, [90] * 3, metric='stream32-3-d2h')['repeated_guard_failure'])

    def test_instability_is_not_a_pass(self):
        row = self.rows([100, 110, 100], [90] * 3)
        self.assertFalse(row['stable'])
        self.assertFalse(row['primary_benefit'])

    def test_equivalent_frame_cpu_uses_concurrent_rate(self):
        stream = dict(direction=1, size=32, h2d_bytes_s=32000, d2h_bytes_s=0,
                      host_window_cpu_ns=1000000, actual_host_window_ns=1000000000,
                      device_cpu=dict(active_cycles=500000000, wall_cycles=1000000000))
        self.assertEqual(stream_costs(stream, 1), {
            'host_stream1_cpu_us_per_frame': 1, 'mcu_stream1_active_us_per_frame': 500})
        self.assertNotIn('mcu_stream1_active_us_per_frame', stream_costs(stream, 0))

    def test_handoff_binds_last_healthy_product_without_restore(self):
        last = dict(name='r3-B-o1', group='B', observer=1)
        build = dict(build_id='V-on', artifacts={'zephyr/zephyr.bin': {'sha256': 'image'}})
        handoff = dict(name=last['name'], build_id='V-on', bin_sha256='image', observer=1,
                       healthy_capture_complete=True, restore_original=False,
                       flash_after_last_capture=False, diagnostic=False)
        validate_handoff(handoff, last, build)
        for field, value in [('name', 'return-A-on'), ('build_id', 'A-on'),
                             ('bin_sha256', 'other'), ('observer', 0),
                             ('healthy_capture_complete', False), ('restore_original', True),
                             ('flash_after_last_capture', True), ('diagnostic', True)]:
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'handoff'):
                validate_handoff(dict(handoff, **{field: value}), last, build)


if __name__ == '__main__':
    unittest.main()
