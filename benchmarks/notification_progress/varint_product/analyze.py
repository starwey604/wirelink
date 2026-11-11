# SPDX-License-Identifier: Apache-2.0
"""Replay the no-probe single-byte varint reader A/B screen from raw evidence."""
import argparse
import collections
import json
from pathlib import Path
import statistics
import sys
from metric_contract import PRIMARY, stream_costs


def compare_series(observer, state, series):
    # Reuse original latency/CPU/goodput/idle protections; select the new CPU
    # target explicitly rather than inheriting the encoder's Sample API goal.
    import importlib.util
    path = Path(__file__).parents[1] / 'fixed_encode/analyze.py'
    spec = importlib.util.spec_from_file_location('fixed_screen_guards', path)
    shared = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(shared)
    comparisons = shared.compare_series(observer, state, series)
    for row in comparisons:
        row.pop('measured_api_benefit', None)
        row['primary_benefit'] = (row['observer'] == 1 and
            row['metric'] == PRIMARY and row['stable'] and
            row['median_ratio'] is not None and row['median_ratio'] < .95)
    return comparisons


def validate_handoff(handoff, last, build):
    if not (handoff['name'] == last['name'] and handoff['build_id'] == build['build_id'] and
            handoff['bin_sha256'] == build['artifacts']['zephyr/zephyr.bin']['sha256'] and
            handoff['observer'] == last['observer'] and handoff['healthy_capture_complete'] is True and
            handoff['restore_original'] is False and handoff['flash_after_last_capture'] is False and
            handoff['diagnostic'] is False):
        raise ValueError('last tested firmware handoff')


def replay(root):
    sys.path.insert(0,str(root/'tools'))
    from usb_evidence import require,sha,verify_files,validate_rows,validate_screen_rows
    from environment import signature
    from host_state import window_state
    registration = root/'final-registration.json'
    reg = json.loads(registration.read_text())
    require(reg['format']=='varint-product-AB-pairs-v1','registration format')
    require(json.loads((root/'status.json').read_text())['stage']=='complete','matrix incomplete')
    expected = [(r,g,o) for r,groups in enumerate(['AB','BA','AB'],1) for o in [0,1] for g in groups]
    require([(e['round'],e['group'],e['observer']) for e in reg['order']]==expected,'registered order')
    require(sha(root/'final-builds.json')==reg['build_registry_sha256'],'build registry changed')
    require(sha(root/'object-code-audit.json')==reg['source_receipt'],'machine code receipt changed')
    for name,digest in reg['tools'].items():require(sha(root/'tools'/name)==digest,'tool changed: '+name)
    builds = json.loads((root/'final-builds.json').read_text())
    for o in [0,1]:
        a,b = [builds[f'{g}-o{o}'] for g in ['A','B']]
        require(a['runner_sha256']==b['runner_sha256'],'host differs within observer')
    def rows_for(path):
        return [json.loads(line) for line in (path/'host.jsonl').read_text().splitlines()]
    def minimal(build):
        return dict(build_id=build['build_id'],library='wirelink',observer=bool(build['observer']),
            wirelink_usb=dict(variant='native',envelope='native_packet',integrity='none',read_max=2048,
                rx_backend='unit_queue',unit_size=2048,unit_slots=2,rx_data_bytes=4096,stream_fifo=0,read_depth=1,tx_inflight=1))
    def qualified(path,build,mode):
        m = json.loads((path/'manifest.json').read_text())
        require(m['complete'] and not m.get('error') and m['qualification_only'] and m['mode']==mode,
                'qualification/return status')
        require(m['build_id']==build['build_id'] and m['runner_sha256']==build['runner_sha256'] and
            m['device_bin_sha256']==build['artifacts']['zephyr/zephyr.bin']['sha256'],'qualification identity')
        verify_files(path,m['artifacts'])
        require(validate_rows(rows_for(path),mode,minimal(build))==m['metrics'],'qualification raw metrics')
    want = {(g,o,'smoke') for g in ['A','B'] for o in [0,1]} | {(g,0,'boundary') for g in ['A','B']}
    require(len(reg['qualifications'])==6 and {(q['group'],q['observer'],q['mode']) for q in reg['qualifications']}==want,
            'missing/duplicate qualification')
    for q in reg['qualifications']:
        path = root/'captures'/q['name']
        require(sha(path/'manifest.json')==q['sha256'],'qualification changed')
        qualified(path,builds[f"{q['group']}-o{q['observer']}"],q['mode'])
    handoff=json.loads((root/'handoff.json').read_text())
    last=reg['order'][-1]
    validate_handoff(handoff, last, builds[f"{last['group']}-o{last['observer']}"])
    sidecar = [json.loads(line) for line in (root/'host-state.jsonl').read_text().splitlines()]
    captures = []
    for entry in reg['order']:
        path = root/'captures'/entry['name'];m = json.loads((path/'manifest.json').read_text())
        b = builds[f"{entry['group']}-o{entry['observer']}"]
        require(m['complete'] and not m.get('error') and not m['qualification_only'] and
            not m['formal_valid'] and not m['full_valid'] and m['mode']=='screen' and not m['diagnostic'],'screen status')
        require((m['group'],m['observer'],m['build_id'])==(entry['group'],entry['observer'],b['build_id']),'screen identity')
        require(m['runner_sha256']==b['runner_sha256']==reg['hosts'][f"{entry['group']}-o{entry['observer']}"] and
            m['device_bin_sha256']==b['artifacts']['zephyr/zephyr.bin']['sha256'],'screen artifacts')
        require(m['registration_sha256']==sha(registration),'screen registration')
        verify_files(path,m['artifacts'])
        ep = root/f"final-environment-{entry['group']}-o{entry['observer']}.json"
        require(sha(ep)==reg['environment'][f"{entry['group']}-o{entry['observer']}"],'registered environment')
        env = json.loads(ep.read_text());require(m['environment']==env,'environment drift')
        timeline = [json.loads(line) for line in (path/'threads.jsonl').read_text().splitlines()]
        require(timeline and timeline[0]['stage']=='pre_warmup' and
            all(signature(t['threads'])==env['runner_threads'] for t in timeline),'native workers/timeline')
        rows = rows_for(path);metrics = validate_screen_rows(rows,'full',minimal(b))
        require(metrics==m['metrics'],'screen derived metrics differ')
        raw = (path/'console.bin').read_bytes()
        require(raw.count(b'usb_fs_v1,ready,')==raw.count(b'usb_fs_v1,final,')==1 and
            not any(x in raw for x in [b'ASSERTION FAIL',b'FATAL ERROR',b'cpu_error=',b'init_error=',b'service_error=']),
            'console lifecycle/health')
        cycles = [v for r in rows if r['event']=='sample_api' for v in r['device']['cycles']]
        require(len(cycles)==1000,'Sample distribution')
        values = dict(rpc_p50_us=metrics['rpc']['p50_ns']/1000,rpc_p99_us=metrics['rpc']['p99_ns']/1000,
            low_p50_us=metrics['low']['p50_ns']/1000,low_p99_us=metrics['low']['p99_ns']/1000,
            host_rpc_cpu_us=metrics['rpc']['host_cpu_ns']/10000/1000,
            host_low_cpu_us=metrics['low']['host_cpu_ns']/200/1000,
            host_sample_cpu_us=metrics['phases']['sample_arrival']['host_cpu_ns']/1000/1000,
            host_idle_percent=metrics['phases']['idle']['host_cpu_ns']/metrics['phases']['idle']['host_wall_ns']*100,
            sample_api_p50_us=metrics['sample_api']['p50_cycles']/240,
            sample_api_p99_us=metrics['sample_api']['p99_cycles']/240,sample_api_mean_us=statistics.mean(cycles)/240)
        if entry['observer']:
            for phase,den,key in [('rpc',10000,'rpc_active_us'),('low',200,'low_active_us'),
                                  ('sample_arrival',1000,'sample_active_us')]:
                cpu = metrics['phases'][phase]['device_cpu'];values[key]=cpu['active_cycles']/den/(cpu['frequency_hz']/1e6)
            cpu=metrics['phases']['idle']['device_cpu'];values['mcu_idle_percent']=cpu['active_cycles']/cpu['wall_cycles']*100
        for stream in metrics['streams']:
            for bit,key in [(1,'h2d'),(2,'d2h')]:
                if stream['direction']&bit:values[f"stream32-{stream['direction']}-{key}"]=stream[key+'_bytes_s']
            values.update({k:v for k,v in stream_costs(stream, entry['observer']).items()
                           if k.startswith('mcu_')})

        captures.append(dict(**entry,values=values,metrics=metrics,
            stream_costs=[dict(direction=s['direction'], **stream_costs(s, entry['observer']))
                          for s in metrics['streams']],
            sample_distribution=dict(histogram=dict(collections.Counter(cycles)),first32=cycles[:32],
                adjacent_changes=sum(a!=b for a,b in zip(cycles,cycles[1:]))),
            window=dict(name=entry['name'],start=timeline[0]['monotonic_ns'],end=timeline[-1]['monotonic_ns'])))
    pairs = {}
    for pair in sorted({c['pair'] for c in captures}):
        saved=json.loads((root/(pair+'-state.json')).read_text());entries=[c for c in captures if c['pair']==pair]
        require(saved['windows']==[c['window'] for c in entries],'pair time bounds')
        target=saved['state'];valid=True;reason=None
        try:
            require(type(target['locked']) is bool and target['monitors'] is not None,'unknown graphical state')
            rows=window_state(sidecar,entries[0]['window']['start'],entries[-1]['window']['end'],target['locked'])
            require(all({k:r[k] for k in ['locked','monitors','boot_id','power']}==target for r in rows),'graphical transition in pair')
            require(len({json.dumps(r['power'],sort_keys=True) for r in rows})==1,'power transition')
        except ValueError as error:valid=False;reason=str(error)
        require(saved['valid']==valid,'pair eligibility differs')
        pairs[pair]=dict(valid=valid,reason=reason,state=target)
    comparisons = []
    for observer in [0,1]:
        eligible=[c for c in captures if c['observer']==observer and pairs[c['pair']]['valid']]
        states={json.dumps(pairs[c['pair']]['state'],sort_keys=True) for c in eligible}
        for state in sorted(states):
            series={g:[c['values'] for c in eligible if c['group']==g and
                json.dumps(pairs[c['pair']]['state'],sort_keys=True)==state] for g in ['A','B']}
            comparisons.extend(compare_series(observer, json.loads(state), series))
    return dict(format='varint-product-AB-result-v1',captures=captures,pairs=pairs,comparisons=comparisons,
        registration_sha256=sha(registration),host_state_sha256=sha(root/'host-state.jsonl'),
        scope='No-probe product A/B 32B screen only. All1000 Sample calls/cold call retained. Cross-state pairs excluded whole and no pooling across states. Existing protection/stability thresholds retained; not full O5 A/B/C, cache-root-cause proof or exclusive core cost.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if args.out.exists():raise ValueError('refusing to overwrite replay output')
    result=replay(args.evidence.resolve())
    with args.out.open('x') as stream:json.dump(result,stream,indent=2);stream.write('\n')
    print('Replayed',len(result['captures']),'product A/B screens')
