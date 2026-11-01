# SPDX-License-Identifier: Apache-2.0
import unittest
from analyze import compare_series


class ComparisonTests(unittest.TestCase):
    def compare(self, metric, a, b):
        return compare_series(0, {'locked': False}, {
            'A': [{metric: value} for value in a],
            'B': [{metric: value} for value in b],
        })[0]

    def test_two_pairs_cannot_establish_benefit_or_rejection(self):
        result = self.compare('sample_api_mean_us', [100, 100], [70, 70])
        self.assertFalse(result['stable'])
        self.assertFalse(result['measured_api_benefit'])
        result = self.compare('stream32-1-h2d', [100, 100], [70, 70])
        self.assertFalse(result['repeated_guard_failure'])

    def test_stable_mean_benefit_and_tail_regression_are_independent(self):
        result = self.compare('sample_api_mean_us', [100] * 3, [70] * 3)
        self.assertTrue(result['measured_api_benefit'])
        result = self.compare('sample_api_p99_us', [100] * 3, [106] * 3)
        self.assertTrue(result['repeated_guard_failure'])

    def test_goodput_protection_uses_opposite_direction(self):
        self.assertTrue(self.compare('stream32-1-h2d', [100] * 3,
                                     [94] * 3)['repeated_guard_failure'])
        self.assertFalse(self.compare('stream32-1-h2d', [100] * 3,
                                      [95] * 3)['repeated_guard_failure'])

    def test_idle_protection_and_stability_use_percentage_points(self):
        result = self.compare('mcu_idle_percent', [.01] * 3, [.51] * 3)
        self.assertTrue(result['stable'])
        self.assertFalse(result['repeated_guard_failure'])
        result = self.compare('mcu_idle_percent', [.01] * 3, [.52] * 3)
        self.assertTrue(result['repeated_guard_failure'])

    def test_reference_or_candidate_span_prevents_stable_inference(self):
        for a, b in [([100, 100, 106], [70] * 3),
                     ([100] * 3, [70, 70, 74])]:
            result = self.compare('sample_api_mean_us', a, b)
            self.assertFalse(result['stable'])
            self.assertFalse(result['measured_api_benefit'])

    def test_empty_or_unbalanced_pairs_rejected(self):
        for a, b in [([], []), ([{}], [])]:
            with self.assertRaisesRegex(ValueError, 'empty or unbalanced'):
                compare_series(0, {}, {'A': a, 'B': b})


if __name__ == '__main__':
    unittest.main()
