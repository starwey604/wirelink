# SPDX-License-Identifier: Apache-2.0
import copy
import unittest
from analyze import diagnostic_windows, validate_capture_metadata, EVENTS, HOST_EVENTS, PHASES
from prepare import replace


def valid_rows():
    rows=[]
    for phase in PHASES:
        counts={key:0 for key in EVENTS}
        counts['handler']={'rpc':10000,'low':200}.get(phase,0)
        rows.append(dict(event=(phase.split('-')[0]+'_cpu' if not phase.startswith('32-') else 'stream_cpu')))
        rows.append(dict(event='host_diagnostic', counts={key:0 for key in HOST_EVENTS}))
        for p in range((len(EVENTS)+7)//8):
            rows.append(dict(event='device_diagnostic',device=dict(page=p,counts={key:counts[key] for key in EVENTS[p*8:(p+1)*8]},done=(p+1)*8>=len(EVENTS))))
    return rows


class DiagnosticTests(unittest.TestCase):
    def test_complete_windows(self):
        self.assertEqual(len(diagnostic_windows(valid_rows())), 7)

    def test_missing_duplicate_and_reordered_pages(self):
        for mutation in ('missing','duplicate','reorder'):
            with self.subTest(mutation=mutation):
                rows=valid_rows()
                if mutation=='missing':del rows[3]
                elif mutation=='duplicate':rows.insert(3,copy.deepcopy(rows[2]))
                else:rows[2],rows[3]=rows[3],rows[2]
                with self.assertRaises(ValueError):diagnostic_windows(rows)

    def test_missing_and_extra_phase(self):
        rows=valid_rows()
        with self.assertRaises(ValueError):diagnostic_windows(rows[:-1])
        with self.assertRaises(ValueError):diagnostic_windows(rows+rows[:2])

    def test_no_early_or_false_done(self):
        for index in (2,7):
            rows=valid_rows();rows[index]['device']['done']=not rows[index]['device']['done']
            with self.assertRaises(ValueError):diagnostic_windows(rows)

    def test_invalid_counter_domain(self):
        for value in (-1,2**32,True,1.5):
            rows=valid_rows();rows[2]['device']['counts']['rx_commit']=value
            with self.assertRaises(ValueError):diagnostic_windows(rows)

    def test_handler_and_sem_accounting(self):
        rows=valid_rows();rows[2]['device']['counts']['rx_commit']=0
        handler=next(row for row in rows if row['event']=='device_diagnostic' and 'handler' in row['device']['counts'])
        handler['device']['counts']['handler']=9999
        with self.assertRaises(ValueError):diagnostic_windows(rows)
        rows=valid_rows()
        sem=next(row for row in rows if row['event']=='device_diagnostic' and 'sem_take' in row['device']['counts'])
        sem['device']['counts']['sem_take']=1
        with self.assertRaises(ValueError):diagnostic_windows(rows)

    def test_no_host_snapshot_or_wrong_fields(self):
        rows=valid_rows();del rows[1]
        with self.assertRaises(ValueError):diagnostic_windows(rows)
        rows=valid_rows();rows[1]['counts']['other']=1
        with self.assertRaises(ValueError):diagnostic_windows(rows)

    def test_capture_identity_observer_and_environment_rejection(self):
        entry=dict(group='D',observer=1)
        build=dict(build_id='diagnostic',runner_sha256='runner',artifacts={'zephyr/zephyr.bin':dict(sha256='bin')})
        m=dict(complete=True,qualification_only=False,formal_valid=False,full_valid=False,mode='screen',
               registration_sha256='registration',group='D',observer=1,build_id='diagnostic',
               runner_sha256='runner',device_bin_sha256='bin',environment={'nice':16})
        validate_capture_metadata(m,entry,build,'registration',{'nice':16})
        for field,value in [('complete',False),('qualification_only',True),('mode','full'),
                            ('registration_sha256','changed'),('observer',0),('group','A'),
                            ('build_id','other'),('runner_sha256','other'),('device_bin_sha256','other'),
                            ('environment',{'nice':17})]:
            with self.subTest(field=field):
                changed=dict(m);changed[field]=value
                with self.assertRaises(ValueError):
                    validate_capture_metadata(changed,entry,build,'registration',{'nice':16})

    def test_patch_anchor_drift_is_fatal(self):
        self.assertEqual(replace('abc','b','B'),'aBc')
        for text in ('ac','abbc'):
            with self.assertRaises(ValueError):replace(text,'b','B')


if __name__=='__main__':
    unittest.main()
