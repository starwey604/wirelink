# SPDX-License-Identifier: Apache-2.0
"""Replay same-image Q/P whole-phase cache-counter pairs without restoring images."""
import argparse
import collections
import json
from pathlib import Path
import statistics
import sys


def replay(root):
    sys.path.insert(0, str(root/'tools'))
    from usb_evidence import require, sha, verify_files, validate_screen_rows
    from probe_validation import probe_windows
    from environment import signature
    from host_state import window_state
    reg_path = root/'final-registration.json'
    reg = json.loads(reg_path.read_text())
    require(reg['format'] == 'cache-phase-QP-pairs-v1', 'registration format')
    require(json.loads((root/'status.json').read_text())['stage'] == 'complete',
            'matrix or board return incomplete')
    order = [(r, g, o) for r, groups in enumerate(['QP', 'PQ', 'QP'], 1)
             for o in [0, 1] for g in groups]
    require([(e['round'], e['group'], e['observer']) for e in reg['order']] == order,
            'registered order')
    require(sha(root/'final-builds.json') == reg['build_registry_sha256'], 'build registry changed')
    require(sha(root/'object-audit.json') == reg['source_receipt'], 'object receipt changed')
    for name, digest in reg['tools'].items():
        require(sha(root/'tools'/name) == digest, 'registered tool changed: '+name)
    builds = json.loads((root/'final-builds.json').read_text())
    for o in [0, 1]:
        a, q, p = [builds[f'{g}-o{o}'] for g in ['A', 'Q', 'P']]
        require([a['sampling_mode'], q['sampling_mode'], p['sampling_mode']] == [-1, 0, 1], 'group modes')
        require(q['artifacts'] == p['artifacts'] and q['build_id'] == p['build_id'], 'Q/P image differs')
        require(a['runner_sha256'] == q['runner_sha256'] == p['runner_sha256'], 'host differs')
    for key, receipt in reg['qualifications'].items():
        path = root.parent/'captures'/('qualification-'+key)/'manifest.json'
        require(sha(path) == receipt['sha256'], 'reused qualification changed')
        m = json.loads(path.read_text())
        require(m['complete'] and m['qualification_only'] and m['mode'] == 'smoke' and
                m['runner_sha256'] == builds[key]['runner_sha256'] and
                m['device_bin_sha256'] == builds[key]['artifacts']['zephyr/zephyr.bin']['sha256'],
                'qualification identity/status')
        verify_files(path.parent, m['artifacts'])
    sidecar = [json.loads(line) for line in (root/'host-state.jsonl').read_text().splitlines()]
    captures = []
    for entry in reg['order']:
        directory = root/'captures'/entry['name']
        m = json.loads((directory/'manifest.json').read_text())
        b = builds[f"{entry['group']}-o{entry['observer']}"]
        require(m['complete'] and not m.get('error') and not m['qualification_only'] and
                not m['formal_valid'] and not m['full_valid'] and m['mode'] == 'screen', 'capture status')
        require((m['group'], m['observer']) == (entry['group'], entry['observer']), 'capture group')
        require(m['registration_sha256'] == sha(reg_path) and m['sampling_mode'] == b['sampling_mode'], 'capture registration/mode')
        require(m['device_bin_sha256'] == b['artifacts']['zephyr/zephyr.bin']['sha256'] and
                m['runner_sha256'] == b['runner_sha256'] and m['build_id'] == b['build_id'], 'capture identity')
        verify_files(directory, m['artifacts'])
        env_file = root/f"final-environment-{entry['group']}-o{entry['observer']}.json"
        require(sha(env_file) == reg['environment'][f"{entry['group']}-o{entry['observer']}"], 'environment file')
        env = json.loads(env_file.read_text())
        require(m['environment'] == env, 'environment drift')
        threads = [json.loads(line) for line in (directory/'threads.jsonl').read_text().splitlines()]
        require(bool(threads) and threads[0]['stage'] == 'pre_warmup', 'thread timeline missing')
        require(all(signature(t['threads']) == env['runner_threads'] for t in threads), 'native worker drift')
        rows = [json.loads(line) for line in (directory/'host.jsonl').read_text().splitlines()]
        x = validate_screen_rows(rows, 'full', dict(build_id=b['build_id'], library='wirelink',
            observer=bool(entry['observer']), wirelink_usb=dict(variant='native', envelope='native_packet',
            integrity='none', read_max=2048, rx_backend='unit_queue', unit_size=2048, unit_slots=2,
            rx_data_bytes=4096, stream_fifo=0, read_depth=1, tx_inflight=1)))
        windows = probe_windows(rows, b['sampling_mode'])
        require(windows == m['probe_windows'] and x == m['metrics'], 'derived capture metrics differ')
        cycles = [v for row in rows if row['event'] == 'sample_api' for v in row['device']['cycles']]
        v = dict(rpc_p50_us=x['rpc']['p50_ns']/1000, rpc_p99_us=x['rpc']['p99_ns']/1000,
                 host_rpc_cpu_us=x['rpc']['host_cpu_ns']/10000/1000,
                 host_sample_cpu_us=x['phases']['sample_arrival']['host_cpu_ns']/1000/1000,
                 host_idle_percent=x['phases']['idle']['host_cpu_ns']/x['phases']['idle']['host_wall_ns']*100,
                 sample_api_p50_us=x['sample_api']['p50_cycles']/240,
                 sample_api_p99_us=x['sample_api']['p99_cycles']/240,
                 sample_api_mean_us=statistics.mean(cycles)/240)
        if entry['observer']:
            for phase, den, key in [('rpc', 10000, 'rpc_active_us'), ('sample_arrival', 1000, 'sample_active_us')]:
                cpu = x['phases'][phase]['device_cpu']
                v[key] = cpu['active_cycles']/den/(cpu['frequency_hz']/1e6)
            cpu = x['phases']['idle']['device_cpu']
            v['mcu_idle_percent'] = cpu['active_cycles']/cpu['wall_cycles']*100
        for stream in x['streams']:
            for bit, key in [(1, 'h2d'), (2, 'd2h')]:
                if stream['direction'] & bit:
                    v[f"stream32-{stream['direction']}-{key}"] = stream[key+'_bytes_s']
        captures.append(dict(**entry, values=v, metrics=x, cache_windows=windows,
            sample_distribution=dict(histogram=dict(collections.Counter(cycles)), first32=cycles[:32],
                adjacent_changes=sum(a != b for a, b in zip(cycles, cycles[1:])))))
    pairs = {}
    for pair in sorted({e['pair'] for e in reg['order']}):
        state_file = json.loads((root/(pair+'-state.json')).read_text())
        entries = [c for c in captures if c['pair'] == pair]
        bounds = []
        for c in entries:
            t = [json.loads(line) for line in
                 (root/'captures'/c['name']/'threads.jsonl').read_text().splitlines()]
            bounds.append(dict(name=c['name'], start=t[0]['monotonic_ns'], end=t[-1]['monotonic_ns']))
        require(state_file['windows'] == bounds, 'pair window metadata')
        valid, reason = True, None
        try:
            target = state_file['state']
            require(type(target['locked']) is bool and target['monitors'] is not None,
                    'unknown pair graphical state')
            w = window_state(sidecar, bounds[0]['start'], bounds[-1]['end'], target['locked'])
            require(all({k: row[k] for k in ['locked', 'monitors', 'boot_id']} == target
                        for row in w), 'between-capture graphical transition')
            require(len({json.dumps(row['power'], sort_keys=True) for row in w}) == 1, 'power transition')
        except ValueError as error:
            valid, reason = False, str(error)
        require(state_file['valid'] == valid, 'pair eligibility differs')
        pairs[pair] = dict(valid=valid, reason=reason, state=state_file['state'])
    comparisons = {}
    for o in [0, 1]:
        eligible = [c for c in captures if c['observer'] == o and pairs[c['pair']]['valid']]
        states = {json.dumps(pairs[c['pair']]['state'], sort_keys=True) for c in eligible}
        for state in sorted(states):
            series = {g: [c['values'] for c in eligible if c['group'] == g and
                          json.dumps(pairs[c['pair']]['state'], sort_keys=True) == state]
                      for g in ['P', 'Q']}
            count = len(series['P'])
            require(count == len(series['Q']), 'unbalanced eligible pairs')
            for key in series['P'][0]:
                n, d = [[v[key] for v in series[g]] for g in ['P', 'Q']]
                idle, goodput = key.endswith('percent'), key.startswith('stream32')
                span = lambda a: max(a)-min(a) if idle else (max(a)/min(a)-1)*100 if min(a) else None
                stable = count == 3 and all(span(a) is not None and span(a) <= (.5 if idle else 5) for a in [n, d])
                ratios = [a/b if b else None for a, b in zip(n, d)]
                bad = [(a-b > .5 if idle else a < b*.95 if goodput else a > b*1.05) for a, b in zip(n, d)]
                comparisons[f'o{o}:P/Q:{state}:{key}'] = dict(
                    observer=o, state=json.loads(state), metric=key, eligible_pairs=count,
                    numerator=n, denominator=d, numerator_span=span(n), denominator_span=span(d),
                    paired_ratios=ratios, median_ratio=statistics.median(ratios) if None not in ratios else None,
                    stable=stable, repeated_guard_failure=stable and sum(bad) >= 2,
                    target_2percent=(stable and statistics.median(ratios) <= 1.02)
                    if key in ['rpc_active_us', 'sample_active_us'] and None not in ratios else None)
    return dict(format='cache-phase-QP-result-v1', captures=captures, pairs=pairs, comparisons=comparisons,
                host_state_sha256=sha(root/'host-state.jsonl'), registration_sha256=sha(reg_path),
                scope='Phase-wide IBUS accesses/misses include all contexts and control boundaries; stream counters also include preparation/drain. Not API-exclusive or retired instructions. Prior Q/A qualification showed structural perturbation; this block measures P/Q boundary reads within one image. Comparisons exclude an entire transition-crossing pair and never pool different graphical states. No cache flush, padding/order search, IRAM migration or product optimization.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    if a.out.exists():
        raise ValueError('refusing to overwrite replay output')
    result = replay(a.evidence.resolve())
    with a.out.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Replayed', len(result['captures']), 'screens')
