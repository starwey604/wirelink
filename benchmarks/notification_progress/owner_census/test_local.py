# SPDX-License-Identifier: Apache-2.0
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from local_rows import local_windows
from prepare_local import DEFINITIONS, EVENTS, HEADER
from local_gate import reference_ready, representative


class LocalTests(unittest.TestCase):
    def fixture(self, **changes):
        counts = dict(zip(EVENTS, [100, 35, 3, 3, 1]))
        counts.update(changes)
        console = b'owner_local_v1,phase=5,direction=3,size=32,' + json.dumps(counts).encode()
        rows = [dict(event='stream_device', device=dict(received=64, sent=32)),
                dict(event='stream', size=32, direction=3)]
        return console, rows

    def test_five_raw_fields_and_derived_admissions(self):
        w = local_windows(*self.fixture())[0]
        self.assertEqual(w['successful'], dict(chunk=32, progress=2))
        self.assertEqual(list(w['counts']), EVENTS)
        self.assertEqual(w['data_frames'], 96)

    def test_bad_schema_phase_and_admission_rejected(self):
        for changes in [dict(chunk_block=36), dict(chunk_attempt=34), dict(owner_pass=True),
                        dict(owner_pass=-1), dict(owner_pass=2**32), dict(extra=1)]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                local_windows(*self.fixture(**changes))
        console, rows = self.fixture()
        for raw in [console.replace(b'direction=3', b'direction=1'), b'', console + b'\n' + console]:
            with self.assertRaises(ValueError):
                local_windows(raw, rows)

    def test_snapshot_unsigned_wrap_and_no_helper_increments(self):
        source = HEADER + DEFINITIONS + '''
#include <assert.h>
int main(void) {
  owner_local_current.owner_pass = UINT32_MAX;
  owner_local_start();
  ++owner_local_current.owner_pass;
  ++owner_local_current.chunk_attempt;
  ++owner_local_current.progress_block;
  owner_local_counts_t out;
  owner_local_snapshot(&out);
  assert(out.owner_pass == 1U && out.chunk_attempt == 1U && out.chunk_block == 0U);
  assert(out.progress_attempt == 0U && out.progress_block == 1U);
  owner_local_start();
  owner_local_snapshot(&out);
  assert(out.owner_pass == 0U && out.chunk_attempt == 0U && out.progress_block == 0U);
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='owner-local-test-') as tmp:
            path = Path(tmp)
            (path / 'test.c').write_text(source)
            for compiler in ['gcc', 'clang']:
                subprocess.run([compiler, '-std=c11', '-Os', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                                str(path / 'test.c'), '-o', str(path / 'test')], check=True, capture_output=True)
                subprocess.run([str(path / 'test')], check=True, capture_output=True)

    def test_gate_rejects_absent_contrast_state_change_and_inverted_probe(self):
        reference = dict(valid=True, state={'locked': True}, ratios={'h2d': .88, 'bidir_d2h': .90})
        self.assertTrue(reference_ready(reference)['pass_gate'])
        self.assertTrue(representative(reference, reference)['pass_gate'])
        for local in [dict(reference, valid=False), dict(reference, state={'locked': False}),
                      dict(reference, ratios={'h2d': 1.05, 'bidir_d2h': .90}),
                      dict(reference, ratios={'h2d': .88, 'bidir_d2h': .95})]:
            self.assertFalse(representative(reference, local)['pass_gate'])
        self.assertFalse(reference_ready(dict(reference, valid=False))['pass_gate'])


if __name__ == '__main__':
    unittest.main()
