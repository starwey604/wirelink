# SPDX-License-Identifier: Apache-2.0
import json
import unittest
from counter_rows import counter_windows
from prepare import EVENTS, replace
from analyze import summarize


class CounterTests(unittest.TestCase):
    def fixture(self, change=None):
        counts = dict.fromkeys(EVENTS, 0)
        counts.update(owner_pass=100, step=100, service=200, chunk_attempt=35,
                      chunk_ok=32, chunk_block=3, wait_query=10, wait_take=8, wait_ready=2)
        if change:
            counts.update(change)
        lines = []
        for page in range(3):
            data = dict(page=page, counts={k: counts[k] for k in EVENTS[page*8:(page+1)*8]}, done=page==2)
            lines.append(('owner_census_v1,phase=5,direction=3,size=32,' + json.dumps(data)).encode())
        rows = [dict(event='stream_device', device=dict(received=64, sent=32)),
                dict(event='stream', size=32, direction=3)]
        return b'\n'.join(lines), rows

    def test_complete_window_and_denominator(self):
        result = counter_windows(*self.fixture())[0]
        self.assertEqual(result['data_frames'], 96)
        self.assertEqual(result['per_data_frame']['chunk_ok'], 1/3)

    def test_missing_reordered_or_wrong_phase_pages_rejected(self):
        console, rows = self.fixture()
        for data in [console.split(b'\n', 1)[1], b'\n'.join(reversed(console.split(b'\n'))),
                     console.replace(b'direction=3', b'direction=1')]:
            with self.assertRaises(ValueError):
                counter_windows(data, rows)

    def test_bad_partitions_domain_and_chunk_ledger_rejected(self):
        for change in [dict(chunk_attempt=34), dict(service=201), dict(wait_query=9),
                       dict(send_error=1), dict(chunk_ok=31, chunk_attempt=34),
                       dict(wake=True), dict(wake=-1), dict(wake=2**32)]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                counter_windows(*self.fixture(change))

    def test_strict_source_anchor(self):
        self.assertEqual(replace('abc', 'b', 'B'), 'aBc')
        for s in ['ac', 'abbc']:
            with self.assertRaises(ValueError):
                replace(s, 'b', 'B')

    def test_three_stable_pairs_required_and_zero_references_explicit(self):
        self.assertFalse(summarize([1, 1], [2, 2])['stable'])
        result = summarize([1, 1, 1], [1.1, 1.1, 1.1])
        self.assertTrue(result['stable'] and result['repeated_increase'])
        self.assertFalse(summarize([1, 1, 1.1], [2, 2, 2])['stable'])
        result = summarize([0, 0, 0], [1, 1, 1])
        self.assertTrue(result['new_nonzero'])
        self.assertIsNone(result['median_ratio'])
        self.assertTrue(summarize([0, 0, 0], [0, 0, 0])['stable'])


if __name__ == '__main__':
    unittest.main()
