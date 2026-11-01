#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay registered O1 raw records and compare diagnostic perturbation with A."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import statistics

PHASES = ('rpc', 'idle', 'low', 'sample', '32-h2d', '32-d2h', '32-bidi')
EVENTS = tuple(re.findall(r'X\((\w+)\)', Path(__file__).with_name('diagnostic.h').read_text()))
HOST_EVENTS = {'notify', 'merged', 'consumed', 'wait', 'wait_success', 'service', 'ready'}


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def diagnostic_windows(rows):
    """Each CPU export must be followed by one host snapshot and all device pages."""
    windows = []
    current = None
    for row in rows:
        event = row['event']
        if event in ('rpc_cpu', 'idle_cpu', 'low_cpu', 'sample_cpu', 'stream_cpu'):
            require(current is None, 'incomplete diagnostic window')
            require(len(windows) < len(PHASES), 'extra diagnostic window')
            expected = PHASES[len(windows)].split('-')[0]
            require(event == (expected + '_cpu' if expected != '32' else 'stream_cpu'), 'diagnostic phase order')
            current = dict(phase=PHASES[len(windows)], device={}, host=None, pages=0)
        elif event == 'host_diagnostic':
            require(current is not None and current['host'] is None, 'duplicate/unscoped host diagnostic')
            counts = row['counts']
            require(set(counts) == HOST_EVENTS, 'host diagnostic fields')
            require(all(type(x) is int and 0 <= x < 2**64 for x in counts.values()), 'invalid host count')
            current['host'] = counts
        elif event == 'device_diagnostic':
            require(current is not None and current['host'] is not None, 'unscoped device diagnostic')
            data = row['device']
            page = current['pages']
            require(type(data['page']) is int and data['page'] == page, 'missing/duplicate diagnostic page')
            expected = EVENTS[page*8:(page+1)*8]
            require(list(data['counts']) == list(expected) and expected, 'diagnostic counter names/order')
            require(all(type(x) is int and 0 <= x < 2**32 for x in data['counts'].values()), 'invalid device count')
            current['device'].update(data['counts'])
            current['pages'] += 1
            last = current['pages']*8 >= len(EVENTS)
            require(type(data['done']) is bool and data['done'] == last, 'diagnostic completion marker')
            if last:
                c = current['device']
                require(c['step'] == c['owner_pass'] == c['step_no_calls'], 'server no-call/owner accounting')
                require(c['generation_slot'] == c['step']*4, 'generation snapshot accounting')
                require(c['terminal_slot'] == c['terminal_scan']*4, 'empty terminal scan accounting')
                require(c['ready_call_slot'] == c['ready']*4, 'ready call scan accounting')
                require(c['service'] == c['step']*2, 'service/owner accounting')
                require(c['sem_take'] == c['sem_immediate'] + c['sem_block'], 'semaphore take accounting')
                require(c['step_empty'] <= c['step'] and c['sem_timeout'] <= c['sem_block'], 'empty/timeout bounds')
                require(c['rx_abort'] == c['tx_error'] == c['tx_busy'] == c['step_terminal'] == 0, 'unexpected O1 fault/busy/terminal')
                expected_calls = {'rpc':10000, 'low':200}.get(current['phase'], 0)
                require(c['handler'] == expected_calls, 'actual handler ledger')
                # BEGIN/STOP occur inside a step; work deltas can straddle that step.
                counted = sum(c[k] for k in ('step_rx','step_tx','step_terminal','step_completion'))
                require(abs(c['step_work'] - counted) <= 16, 'step boundary/work accounting')
                windows.append(current)
                current = None
    require(current is None and len(windows) == len(PHASES), 'missing diagnostic windows')
    return windows


def validate_capture_metadata(m, entry, build, registration_digest, environment):
    require(m['complete'] and not m.get('error') and not m['qualification_only'] and
            not m['formal_valid'] and not m['full_valid'] and m['mode']=='screen', 'capture status')
    require(m['registration_sha256'] == registration_digest, 'capture registration')
    require((m['group'],m['observer']) == (entry['group'],entry['observer']), 'capture group/observer')
    require(m['build_id'] == build['build_id'] and m['runner_sha256'] == build['runner_sha256'] and
            m['device_bin_sha256'] == build['artifacts']['zephyr/zephyr.bin']['sha256'], 'capture build identity')
    require(m['environment'] == environment, 'capture environment drift')


def load_validator(root):
    path = root / 'tools/usb_evidence.py'
    spec = importlib.util.spec_from_file_location('o1_usb_evidence', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def replay(root):
    registration_path = root / 'final-registration.json'
    registration = json.loads(registration_path.read_text())
    require(registration['format'] == 'static-kernel-cpu-o1-registration-v1', 'registration format')
    require(sha(root/'final-builds.json') == registration['build_registry_sha256'], 'changed build registry')
    for name, digest in registration['tools'].items():
        require(sha(root/'tools'/name) == digest, 'registered tool changed: '+name)
    builds = json.loads((root/'final-builds.json').read_text())
    validator = load_validator(root)
    captures = []
    order = registration['order']
    require([(e['round'],e['group'],e['observer']) for e in order] ==
            [(r,g,o) for r,groups in enumerate(('AD','DA','AD'),1) for g in groups for o in (0,1)], 'registered order')
    for entry in order:
        directory = root / 'captures' / entry['name']
        m = json.loads((directory/'manifest.json').read_text())
        build = builds[f"{entry['group']}-o{entry['observer']}"]
        env_path = root/f"final-environment-{entry['group']}-o{entry['observer']}.json"
        require(sha(env_path) == registration['environment'][env_path.stem.removeprefix('final-environment-')], 'changed environment')
        validate_capture_metadata(m, entry, build, sha(registration_path), json.loads(env_path.read_text()))
        for name, info in m['artifacts'].items():
            relative = Path(name)
            require(not relative.is_absolute() and '..' not in relative.parts, 'unsafe capture artifact')
            path = directory / relative
            require(path.stat().st_size == info['size'] and sha(path) == info['sha256'], 'changed capture artifact: '+name)
        expected_threads = m['environment']['runner_threads']
        for line in (directory/'threads.jsonl').read_text().splitlines():
            threads = json.loads(line)['threads']
            fields = lambda t: {k:t[k] for k in ('policy','nice','affinity')}
            main = [fields(t) for t in threads if t['main']]
            workers = sorted([fields(t) for t in threads if not t['main']], key=lambda t:(t['policy'],t['nice'],t['affinity']))
            require(main == [expected_threads['main']] and workers == expected_threads['workers'], 'native thread timeline drift')
        rows = [json.loads(line) for line in (directory/'host.jsonl').read_text().splitlines()]
        minimal = dict(build_id=build['build_id'], library='wirelink', observer=bool(entry['observer']),
                       wirelink_usb=dict(variant='native',envelope='native_packet',integrity='none',read_max=2048,
                       rx_backend='unit_queue',unit_size=2048,unit_slots=2,rx_data_bytes=4096,stream_fifo=0,read_depth=1,tx_inflight=1))
        metrics = validator.validate_screen_rows(rows, 'full', minimal)
        require(metrics == m['metrics'], 'stored metrics do not match raw replay')
        raw = (directory/'console.bin').read_bytes()
        require(raw.count(b'usb_fs_v1,ready,') == raw.count(b'usb_fs_v1,final,') == 1, 'console lifecycle')
        require(not any(x in raw for x in (b'ASSERTION FAIL',b'FATAL ERROR',b'cpu_error=',b'init_error=',b'service_error=')), 'firmware failure')
        captures.append(dict(**entry, metrics=metrics,
                             diagnostic=diagnostic_windows(rows) if entry['group']=='D' else None))
    comparisons = []
    for o in (0,1):
        rounds = []
        for r in (1,2,3):
            a = next(c['metrics'] for c in captures if c['round']==r and c['group']=='A' and c['observer']==o)
            d = next(c['metrics'] for c in captures if c['round']==r and c['group']=='D' and c['observer']==o)
            ratios = dict(rpc_rtt_p50=d['rpc']['p50_ns']/a['rpc']['p50_ns'],
                          rpc_rtt_p99=d['rpc']['p99_ns']/a['rpc']['p99_ns'],
                          sample_api_p50=d['sample_api']['p50_cycles']/a['sample_api']['p50_cycles'])
            if o:
                for phase in ('rpc','sample_arrival'):
                    ratios[phase+'_active'] = d['phases'][phase]['device_cpu']['active_cycles']/a['phases'][phase]['device_cpu']['active_cycles']
            for direction in (1,2,3):
                for field in ('h2d_bytes_s','d2h_bytes_s'):
                    av=a['streams'][direction-1][field];dv=d['streams'][direction-1][field]
                    if av: ratios[f'32-{direction}-{field}'] = dv/av
            rounds.append(dict(round=r, ratios_D_over_A=ratios))
        comparisons.append(dict(observer=o,rounds=rounds,median_ratios_D_over_A={k:statistics.median(r['ratios_D_over_A'][k] for r in rounds) for k in rounds[0]['ratios_D_over_A']}))
    return dict(format='wirelink-cpu-o1-raw-replay-v1', complete=True, product_optimization=False,
                scope='12 registered A/D screen captures; diagnostics and observer perturbation, no C/full-size final acceptance; raw-only replay, no deleted build replay',
                comparisons=comparisons,captures=captures)


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a=p.parse_args()
    require(not a.out.exists(), 'refusing result overwrite')
    result=replay(a.evidence.resolve())
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(complete=result['complete'],captures=len(result['captures']),comparisons=result['comparisons']),indent=2))
