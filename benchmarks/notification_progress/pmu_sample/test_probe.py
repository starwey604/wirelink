# SPDX-License-Identifier: Apache-2.0
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from validate import calibration, probe_windows, selected, WINDOW_CONTROL, STALL_CONTROL

HERE = Path(__file__).resolve().parent


class ProbeTests(unittest.TestCase):
    def test_native_state_export_wrap_and_ownership(self):
        for compiler in ['gcc', 'clang']:
            with self.subTest(compiler=compiler), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root/'pmu_fake.h').write_text('''
#include <stdint.h>
extern uint32_t regs[16], ticks, events, stalls;
uint32_t fake_read(unsigned addr);
void fake_write(unsigned addr, uint32_t value);
#define ERI_PERFMON_PGM 0U
#define ERI_PERFMON_PM0 1U
#define ERI_PERFMON_PM1 2U
#define ERI_PERFMON_PMCTRL0 3U
#define PMCTRL_TRACELEVEL_SHIFT 4U
#define PMCTRL_SELECT_SHIFT 8U
#define PMCTRL_MASK_SHIFT 16U
#define XTPERF_CNT_CYCLES 0U
#define XTPERF_MASK_CYCLES 1U
#define XTPERF_CNT_EXR 5U
#define XTPERF_MASK_EXR_WINDOW 32U
#define XTPERF_CNT_I_STALL 4U
#define XTPERF_MASK_I_STALL_ALL 511U
#define PGM_PMEN 1U
#define EXTMEM_IBUS_TO_FLASH_START_VADDR_REG 8U
#define EXTMEM_IBUS_TO_FLASH_END_VADDR_REG 9U
#define SOC_IROM_LOW 0x42000000U
#define SOC_IROM_HIGH 0x44000000U
#define REG_WRITE(a,v) (regs[a]=(v))
#define PMU_ACCESS() regs[6]
#define PMU_MISS() regs[7]
#define PMU_READ(a,v) ((v)=fake_read(a))
#define PMU_WRITE(a,v) fake_write(a,v)
#define PMU_CLOCK(v) ((v)=ticks++)
#define PMU_WINDOWS(b,s) do {(b)=10U;(s)=1024U;}while(0)
static inline unsigned irq_lock(void) {return 0;}
static inline void irq_unlock(unsigned key) {(void)key;}
static inline uint32_t k_cycle_get_32(void) {return ticks++;}
''')
                (root/'test.c').write_text('''#include "probe.h"
#include <assert.h>
#include <stdio.h>
uint32_t regs[16], ticks, events, stalls;
static uint32_t ct[2], ce[2], cs[2];
uint32_t fake_read(unsigned addr) {
  ++ticks;
  if (addr==1U || addr==2U) {
    unsigned i=addr-1U, selector=(regs[3U+i]&0x1f00U)>>8U;
    if (selector==0U) return ticks-ct[i];
    if (selector==5U) return events-ce[i];
    return stalls-cs[i];
  }
  return regs[addr];
}
void fake_write(unsigned addr,uint32_t value) {
  if (addr==1U || addr==2U) {unsigned i=addr-1U;ct[i]=ticks;ce[i]=events;cs[i]=stalls;}
  regs[addr]=value;
}
unsigned pmu_cal_depth(unsigned depth) {ticks+=depth*4+10;if(depth>7)events+=(depth-7)*2;return depth;}
unsigned pmu_cal_divide(unsigned n) {ticks+=n*4;stalls+=n;return 0;}
int main(int argc,char **argv) {
  (void)argv;char out[512];
  if(argc>1) regs[0]=1;
  probe_init();assert(probe_config(out,sizeof(out),0));puts(out);
  if(argc>1) {assert(regs[0]==1);return 0;}
  assert(regs[8]==SOC_IROM_LOW && regs[9]==SOC_IROM_HIGH-1U);
  assert(!probe_config(out,sizeof(out),2));
  probe_start();assert(!probe_config(out,sizeof(out),1));assert(!probe_json(out,sizeof(out),0));
  pmu_snapshot_t a={0},b={0};
  for(unsigned i=0;i<1000;++i) {assert(!PMU_SELECTED(i));PMU_FINISH(i,i,500,0,0,a,b,0);}
  probe_stop();assert(probe_json(out,sizeof(out),0));puts(out);
  assert(probe_config(out,sizeof(out),1));
  events=UINT32_MAX-34U;probe_start();
  unsigned selected_count=0,even=0,offsets[64]={0};
  for(unsigned block=0;block<64;++block) {
    unsigned count=0;
    for(unsigned i=block*64;i<(block+1)*64;++i)if(PMU_INDEX(i)){++count;++offsets[i&63U];}
    assert(count==1);
  }
  for(unsigned i=0;i<64;++i)assert(offsets[i]==1);
  for(unsigned i=0;i<1000;++i) {
    bool pick=PMU_SELECTED(i);
    if(pick){++selected_count;even+=(i&1U)==0;}
    PMU_SNAPSHOT(a);events+=2;stalls+=(i&1U)?40:100;regs[6]+=7;regs[7]+=2;PMU_SNAPSHOT(b);
    PMU_FINISH(i,3000000000U+i,1000,0,pick,a,b,2);
  }
  assert(selected_count==16 && even==8);
  PMU_FINISH(1000,0,0,1,0,a,b,0);
  probe_stop();
  for(unsigned p=0;p<5;++p) {assert(!probe_json(out,1,p));assert(probe_json(out,sizeof(out),p));puts(out);}
  assert(!probe_json(out,sizeof(out),5));assert(!probe_json(out,sizeof(out),UINT32_MAX));
  assert(pmu_record_count==16);
  return 0;
}
''')
                exe = root/'test'
                subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                                '-fsanitize=address,undefined', '-DPMU_PROBE_TEST', '-I'+str(root),
                                '-I'+str(HERE), str(root/'test.c'), str(HERE/'probe.c'), '-o', str(exe)], check=True)
                env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1')
                rows = [json.loads(line) for line in subprocess.check_output([str(exe)], text=True, env=env).splitlines()]
                calibration(rows[0], 0)
                self.assertEqual(rows[1]['count'], 0)
                self.assertEqual((rows[2]['attempts'], rows[2]['busy'], rows[2]['count']), (1001, 1, 16))
                records = [r for page in rows[3:] for r in page['records']]
                self.assertEqual([r[0] for r in records], [i for i in range(1000) if selected(i)])
                self.assertTrue(all(r[3] == r[10] == 2 for r in records))
                occupied = json.loads(subprocess.check_output([str(exe), 'occupied'], text=True, env=env))
                self.assertFalse(occupied['calibrated'])
                self.assertEqual((occupied['prior_pgm'], occupied['error']), (1, 1))

    def rows(self):
        config = dict(mode=1, calibrated=True, error=0, prior_pgm=0, ctrl0=WINDOW_CONTROL,
                      ctrl1=STALL_CONTROL, clock=10000, cycles=9990, shallow=0, deep=100,
                      divide_stall=100, empty_cycles=100, empty_windows=0, empty_stalls=20, empty_outer=0)
        rows = [dict(event='pmu_probe_config', device=config)]
        for epoch, name in enumerate(['rpc_cpu', 'idle_cpu', 'low_cpu', 'sample_cpu']+['stream_cpu']*3, 1):
            sample = name == 'sample_cpu'
            rows.append(dict(event=name))
            rows.append(dict(event='pmu_probe', device=dict(page=0, epoch=epoch, mode=1,
                count=16 if sample else 0, attempts=1000 if sample else 0, busy=0, errors=0,
                windows=100, stalls=1000, access=2000, miss=30, done=not sample)))
            if sample:
                records = [[i, 3000000000+i, 1000, 6, 500, 2, 10, 1024, 10, 1024, 8]
                           for i in range(1000) if selected(i)]
                for page in range(4):
                    rows.append(dict(event='pmu_probe', device=dict(page=page+1,
                        records=records[page*4:(page+1)*4], done=page == 3)))
        rows.append(dict(event='sample_api', device=dict(cycles=[1000]*1000,
                          starts=[3000000000+i for i in range(1000)])))
        return rows

    def test_raw_scope_calibration_and_rejection(self):
        self.assertEqual(len(probe_windows(self.rows(), 1)), 7)
        for mutation in ['calibration', 'empty_window', 'selector', 'missing', 'duplicate',
                         'attempts', 'counter', 'clock', 'outer', 'register', 'early_done']:
            with self.subTest(mutation=mutation):
                rows = copy.deepcopy(self.rows())
                record_page = next(r['device'] for r in rows if r['event'] == 'pmu_probe' and 'records' in r['device'])
                if mutation == 'calibration':rows[0]['device']['calibrated'] = False
                elif mutation == 'empty_window':rows[0]['device']['empty_windows'] = 1
                elif mutation == 'selector':rows[0]['device']['ctrl0'] = 0x8005
                elif mutation == 'missing':rows.pop(2)
                elif mutation == 'duplicate':rows.insert(3, rows[2])
                elif mutation == 'attempts':rows[8]['device']['attempts'] = 1001
                elif mutation == 'counter':record_page['records'][0][3] = -1
                elif mutation == 'clock':record_page['records'][0][2] = 999
                elif mutation == 'outer':record_page['records'][0][10] = 5
                elif mutation == 'register':record_page['records'][0][6] = 16
                elif mutation == 'early_done':record_page['done'] = True
                with self.assertRaises(ValueError):probe_windows(rows, 1)


if __name__ == '__main__':
    unittest.main()
