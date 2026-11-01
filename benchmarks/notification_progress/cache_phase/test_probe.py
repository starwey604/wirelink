# SPDX-License-Identifier: Apache-2.0
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from validate import probe_windows

HERE = Path(__file__).resolve().parent

class ProbeTests(unittest.TestCase):
    def test_state_wrap_and_bounded_export(self):
        for compiler in ['gcc', 'clang']:
            with self.subTest(compiler=compiler), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root/'soc').mkdir()
                (root/'soc/soc.h').write_text('''#include <stdint.h>
extern uint32_t regs[4];
#define REG_READ(x) regs[x]
#define REG_WRITE(x,v) (regs[x]=(v))
#define SOC_IROM_LOW 0x42000000U
#define SOC_IROM_HIGH 0x44000000U
''')
                (root/'soc/extmem_reg.h').write_text('''
#define EXTMEM_IBUS_ACS_CNT_REG 0
#define EXTMEM_IBUS_ACS_MISS_CNT_REG 1
#define EXTMEM_IBUS_TO_FLASH_START_VADDR_REG 2
#define EXTMEM_IBUS_TO_FLASH_END_VADDR_REG 3
''')
                (root/'test.c').write_text('''#include "probe.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
uint32_t regs[4];
int main(void) {
  probe_init(); assert(regs[2] == 0x42000000U && regs[3] == 0x43ffffffU);
  char out[512];
  assert(!probe_config(out, sizeof(out), 2));
  assert(probe_config(out, sizeof(out), 0)); puts(out);
  probe_start(); assert(!probe_config(out, sizeof(out), 1));
  assert(!probe_json(out, sizeof(out), 0));
  regs[0] = regs[1] = 7; probe_stop();
  assert(probe_json(out, sizeof(out), 0)); puts(out);
  assert(probe_config(out, sizeof(out), 1)); puts(out);
  regs[0] = UINT32_MAX-4U; regs[1] = UINT32_MAX-1U;
  probe_start(); regs[0] += 160; regs[1] += 32; probe_stop();
  assert(!probe_json(out, 1, 0));
  assert(probe_json(out, sizeof(out), 0)); puts(out);
  assert(!probe_json(out, sizeof(out), 1));
  assert(!probe_json(out, sizeof(out), UINT32_MAX));
  return 0;
}
''')
                exe = root/'test'
                subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Wpedantic',
                                '-Werror', '-fsanitize=address,undefined',
                                '-I'+str(root), '-I'+str(HERE), str(root/'test.c'),
                                str(HERE/'probe.c'), '-o', str(exe)], check=True)
                rows = [json.loads(line) for line in subprocess.check_output(
                    [str(exe)], text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1')).splitlines()]
                self.assertEqual(rows[0]['scope'], 'phase')
                self.assertEqual((rows[1]['mode'], rows[1]['access'], rows[1]['miss']), (0, 0, 0))
                self.assertEqual((rows[3]['epoch'], rows[3]['access'], rows[3]['miss']), (2, 160, 32))

    def rows(self):
        rows=[dict(event='cache_probe_config',device=dict(mode=1,scope='phase',filter_start=0x42000000,filter_end=0x43ffffff))]
        for i,event in enumerate(['rpc_cpu','idle_cpu','low_cpu','sample_cpu']+['stream_cpu']*3,1):
            rows.extend([dict(event=event),dict(event='cache_probe',device=dict(page=0,epoch=i,mode=1,access=100,miss=5,done=True))])
        return rows

    def test_raw_phase_order_and_rejections(self):
        self.assertEqual(len(probe_windows(self.rows(),1)),7)
        for mutation in ['missing','duplicate','counter','mode','epoch','done','inactive','extra']:
            with self.subTest(mutation=mutation):
                rows=self.rows()
                if mutation=='missing':rows.pop(2)
                elif mutation=='duplicate':rows.insert(2,rows[2].copy())
                elif mutation=='counter':rows[2]['device']['miss']=-1
                elif mutation=='mode':rows[0]['device']['mode']=0
                elif mutation=='epoch':rows[2]['device']['epoch']=2
                elif mutation=='done':rows[2]['device']['done']=False
                elif mutation=='inactive':rows[2]['device']['access']=0
                elif mutation=='extra':rows.append(rows[-1])
                with self.assertRaises(ValueError):probe_windows(rows,1)
        with self.assertRaises(ValueError):probe_windows(self.rows(),-1)

if __name__=='__main__':unittest.main()
