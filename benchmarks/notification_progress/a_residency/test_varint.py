# SPDX-License-Identifier: Apache-2.0
import unittest

from varint_fastpath import (FAST, candidate_fallback, canonical_plan,
                             definition, prototype, reference)


class VarintFeasibilityTests(unittest.TestCase):
    def test_only_reader_changes_and_fallback_is_verbatim(self):
        source = '''static int wlc_getv(int value) {
  size_t start = *at, n = 0U;
  return value;
}
static int other(void) { return 0; }
'''
        candidate = prototype(source)
        self.assertEqual(candidate.replace(FAST, ''), source)
        with self.assertRaisesRegex(ValueError, 'boundary changed'):
            prototype(candidate.replace('  size_t start = *at, n = 0U;', ''))
        with self.assertRaisesRegex(ValueError, 'ambiguous frozen function'):
            definition(source + source, 'wlc_getv')

    def test_reference_clones_only_reader_consumers(self):
        source = '\n'.join('static int ' + name + '(void) {\n  return wlc_getv();\n}'
                           for name in ['wlc_getv', 'wlc_skip', 'wlc_read_value', 'wlc_decode'])
        frozen = reference(source)
        self.assertNotIn('wlc_getv', frozen)
        self.assertIn('reference_decode', frozen)
        self.assertEqual(frozen.count('return reference_getv();'), 4)

    def test_canonical_survey_tenth_byte_and_fallback_visits(self):
        tenth = canonical_plan(10)
        self.assertEqual(tenth[15], [True] * 9 + [False])
        self.assertEqual(tenth[16], [True])
        self.assertEqual(tenth[49], [True] * 5 + [False] * 5)
        self.assertEqual(candidate_fallback(tenth)[20], tenth[15])
        self.assertEqual(candidate_fallback({58: [True], 61: [False]}, False),
                         {67: [True], 70: [False], 5: [False]})
        for n in [1, 11]:
            with self.assertRaisesRegex(ValueError, 'outside 2..10'):
                canonical_plan(n)


if __name__ == '__main__':
    unittest.main()
