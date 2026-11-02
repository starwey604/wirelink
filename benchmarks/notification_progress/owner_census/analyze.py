# SPDX-License-Identifier: Apache-2.0
"""Replay diagnostic phase counts without retaining any diagnostic binaries."""
import argparse
import importlib.util
import json
from pathlib import Path
import statistics
from functools import partial
import sys

from counter_rows import counter_windows, require


def summarize(a, b):
    require(bool(a) and len(a) == len(b), 'unbalanced counter pairs')

    def span(values):
        if max(values) == 0:
            return 0
        return (max(values) / min(values) - 1) * 100 if min(values) else None

    stable = len(a) == 3 and all(span(v) is not None and span(v) <= 5 for v in [a, b])
    ratios = [y / x if x else None for x, y in zip(a, b)]
    increased = [y > x * 1.05 for x, y in zip(a, b)]
    return dict(A=a, B=b, A_span_percent=span(a), B_span_percent=span(b),
                paired_ratios=ratios,
                median_ratio=statistics.median(ratios) if None not in ratios else None,
                stable=stable, repeated_increase=stable and sum(increased) >= 2,
                new_nonzero=all(v == 0 for v in a) and all(v > 0 for v in b))


def read_capture(root, builds, reg, ledger, name, group, observer, mode, qualification):
    from usb_evidence import sha, verify_files, validate_rows, validate_screen_rows
    from environment import signature
    path = root / 'captures' / name
    m = json.loads((path / 'manifest.json').read_text())
    b = builds[f'{group}-o{observer}']
    formats = {'owner-census-control-v1': 'owner-census-product-control-v1',
               'owner-census-AB-v1': 'owner-census-capture-v1',
               'owner-local-AB-v1': 'owner-local-capture-v1'}
    require(m['format'] == formats[reg['format']], 'capture format')
    require(m['complete'] and not m.get('error') and m['mode'] == mode
            and m['qualification_only'] is qualification and not m['formal_valid']
            and not m['full_valid'] and not m['performance_measurement'], 'capture status')
    require((m['group'], m['observer'], m['build_id'], m['diagnostic']) ==
            (group, observer, b['build_id'], b['diagnostic']), 'capture identity/diagnostic')
    require(m['runner_sha256'] == b['runner_sha256'] and
            m['device_bin_sha256'] == b['artifacts']['zephyr/zephyr.bin']['sha256'], 'capture artifacts')
    verify_files(path, m['artifacts'])
    text = (path / 'host.jsonl').read_text()
    rows = [json.loads(line) for line in text.splitlines()]
    minimal = dict(build_id=b['build_id'], library='wirelink', observer=bool(observer),
                   wirelink_usb=dict(variant='native', envelope='native_packet', integrity='none',
                       read_max=2048, rx_backend='unit_queue', unit_size=2048, unit_slots=2,
                       rx_data_bytes=4096, stream_fifo=0, read_depth=1, tx_inflight=1))
    identity = next(r['device'] for r in rows if r['event'] == 'identity')
    require(all(identity[k] == v for k, v in dict(kernel=1, profile=2, role='both', replies=4).items()),
            'endpoint contract')
    metrics = (validate_screen_rows(rows, 'full', minimal) if mode == 'screen'
               else validate_rows(rows, mode, minimal))
    require(metrics == m['metrics'], 'raw metrics differ')
    raw = (path / 'console.bin').read_bytes()
    require(raw.count(b'usb_fs_v1,ready,') == raw.count(b'usb_fs_v1,final,') == 1 and
            not any(x in raw for x in [b'ASSERTION FAIL', b'FATAL ERROR', b'cpu_error=',
                                       b'init_error=', b'service_error=']), 'console health/reset')
    local = reg['format'] == 'owner-local-AB-v1'
    if local:
        from local_rows import local_windows
    reader = local_windows if local else counter_windows
    windows = reader(raw, rows) if b['diagnostic'] and mode != 'boundary' else []
    require(not windows or windows == m['counter_windows'], 'raw counter derivation differs')
    if windows and mode == 'screen':
        for row in rows:
            if row['event'] in ['stream_device', 'stream_cpu']:
                require(json.dumps(row, separators=(',', ':')) in text, 'raw response JSON spelling')
            if row['event'] == 'host_health' and row['stage'] == 'stream_begin':
                begin = row
            elif row['event'] == 'stream_device':
                device = row
            elif row['event'] == 'stream_cpu':
                cpu = row
            elif row['event'] == 'host_health' and row['stage'] == 'stream_end':
                end = row
            elif row['event'] == 'stream':
                wire = ledger.successful_progress(begin, end, row, device, cpu)
                window = next(w for w in windows if w['name'] == f"32-{row['direction']}")
                admitted = (window['successful']['progress'] if local else window['counts']['progress_ok'])
                require(admitted == wire['normal_frames']['device'] + 1,
                        'Progress admission/wire ledger')
                window['wire_progress'] = wire
    if qualification:
        return dict(metrics=metrics, windows=windows)
    require(m['registration_sha256'] == sha(root / 'final-registration.json'), 'capture registration')
    env_path = root / f'final-environment-{group}-o{observer}.json'
    require(sha(env_path) == reg['environment'][f'{group}-o{observer}'], 'environment identity')
    env = json.loads(env_path.read_text())
    require(env == m['environment'], 'environment drift')
    threads = [json.loads(line) for line in (path / 'threads.jsonl').read_text().splitlines()]
    require(threads[0]['stage'] == 'pre_warmup' and
            all(signature(t['threads']) == env['runner_threads'] for t in threads), 'worker drift')
    return dict(name=name, metrics=metrics, windows=windows,
                window=dict(name=name, start=threads[0]['monotonic_ns'], end=threads[-1]['monotonic_ns']))


def replay_control(root, ledger):
    """Read the one exact-image control pair; never pool it with any matrix."""
    from usb_evidence import sha
    from host_state import window_state

    reg = json.loads((root / 'final-registration.json').read_text())
    require(reg['format'] == 'owner-census-control-v1', 'control registration format')
    require(json.loads((root / 'status.json').read_text())['stage'] == 'complete', 'control/return incomplete')
    require(sha(root / 'final-builds.json') == reg['build_registry_sha256'], 'control build registry')
    for name, digest in reg['tools'].items():
        require(sha(root / 'tools' / name) == digest, 'control tool changed: ' + name)
    builds = json.loads((root / 'final-builds.json').read_text())
    require(builds['A-o1']['runner_sha256'] == builds['B-o1']['runner_sha256'], 'control host differs')
    # The prior product receipt is the identity authority, not a newly assigned ID.
    previous = Path('/home/ww/codings/wirelink-required-fixed-20261101')
    old_reg = json.loads((previous / 'final-registration.json').read_text())
    require(sha(previous / 'final-builds.json') == old_reg['build_registry_sha256'], 'prior product registry')
    old = json.loads((previous / 'final-builds.json').read_text())
    for group in ['A', 'B']:
        key = group + '-o1'
        require(builds[key]['artifacts']['zephyr/zephyr.bin'] == old[key]['artifacts']['zephyr/zephyr.bin'],
                'control is not the original product image')
    require([(e['group'], e['observer']) for e in reg['order']] == [('A', 1), ('B', 1)], 'control order')
    require(len(reg['qualifications']) == 2 and {q['group'] for q in reg['qualifications']} == {'A', 'B'},
            'control qualifications')
    for q in reg['qualifications']:
        require(sha(root / 'captures' / q['name'] / 'manifest.json') == q['sha256'], 'control qualification changed')
        read_capture(root, builds, reg, ledger, q['name'], q['group'], 1, 'smoke', True)
    read_capture(root, builds, reg, ledger, 'return-A-on', 'A', 1, 'smoke', True)
    captures = [read_capture(root, builds, reg, ledger, e['name'], e['group'], 1, 'screen', False)
                for e in reg['order']]
    saved = json.loads((root / 'control-state.json').read_text())
    require(saved['windows'] == [c['window'] for c in captures], 'control time bounds')
    sidecar = [json.loads(line) for line in (root / 'host-state.jsonl').read_text().splitlines()]
    valid, reason = True, None
    try:
        rows = window_state(sidecar, captures[0]['window']['start'], captures[-1]['window']['end'], saved['state']['locked'])
        require(all({k: row[k] for k in saved['state']} == saved['state'] for row in rows), 'control state/power transition')
    except ValueError as error:
        valid, reason = False, str(error)
    require(valid == saved['valid'], 'control eligibility differs')
    context = []
    for a, b in zip(captures[0]['metrics']['streams'], captures[1]['metrics']['streams']):
        for bit, key in [(1, 'h2d_bytes_s'), (2, 'd2h_bytes_s')]:
            if a['direction'] & bit:
                context.append(dict(direction=a['direction'], metric=key, A=a[key], B=b[key], ratio=b[key]/a[key]))
    return dict(captures=captures, valid=valid, reason=reason, state=saved['state'], context=context,
                scope='One same-current-state AB control, original firmware bytes. Not an acceptance extension or stable estimate.')


def replay(root):
    sys.path.insert(0, str(root / 'tools'))
    from usb_evidence import sha
    from host_state import window_state

    ledger_path = Path(__file__).parents[1] / 'a_residency/stream_ledger.py'
    spec = importlib.util.spec_from_file_location('owner_stream_ledger', ledger_path)
    ledger = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ledger)

    reg = json.loads((root / 'final-registration.json').read_text())
    require(reg['format'] == 'owner-census-AB-v1', 'registration format')
    require(json.loads((root / 'status.json').read_text())['stage'] == 'complete', 'matrix/return incomplete')
    require(sha(root / 'final-builds.json') == reg['build_registry_sha256'], 'build registry changed')
    require(sha(root / 'source-audit.json') == reg['source_receipt'], 'source audit changed')
    for name, digest in reg['tools'].items():
        require(sha(root / 'tools' / name) == digest, 'frozen tool changed: ' + name)
    expected = [(r, g, o) for r, order in enumerate(['AB', 'BA', 'AB'], 1)
                for o in [0, 1] for g in order]
    require([(e['round'], e['group'], e['observer']) for e in reg['order']] == expected, 'matrix order')
    builds = json.loads((root / 'final-builds.json').read_text())
    for o in [0, 1]:
        require(builds[f'A-o{o}']['runner_sha256'] == builds[f'B-o{o}']['runner_sha256'], 'host differs')

    capture = partial(read_capture, root, builds, reg, ledger)

    want = {(g, o, 'smoke') for g in ['A', 'B'] for o in [0, 1]} | {
        ('R', 1, 'smoke'), ('A', 0, 'boundary'), ('B', 0, 'boundary')}
    require(len(reg['qualifications']) == 7 and
            {(q['group'], q['observer'], q['mode']) for q in reg['qualifications']} == want, 'qualifications')
    for q in reg['qualifications']:
        require(sha(root / 'captures' / q['name'] / 'manifest.json') == q['sha256'], 'qualification changed')
        capture(q['name'], q['group'], q['observer'], q['mode'], True)
    capture('return-A-on', 'R', 1, 'smoke', True)
    captures = [dict(**entry, **{k: v for k, v in capture(entry['name'], entry['group'], entry['observer'],
                    'screen', False).items() if k != 'name'}) for entry in reg['order']]
    sidecar = [json.loads(line) for line in (root / 'host-state.jsonl').read_text().splitlines()]
    pairs = {}
    for pair in sorted({c['pair'] for c in captures}):
        saved = json.loads((root / (pair + '-state.json')).read_text())
        entries = [c for c in captures if c['pair'] == pair]
        require(saved['windows'] == [c['window'] for c in entries], 'pair time bounds')
        valid, reason, power = True, None, None
        try:
            target = saved['state']
            rows = window_state(sidecar, entries[0]['window']['start'], entries[-1]['window']['end'], target['locked'])
            require(all({k: r[k] for k in ['locked', 'monitors', 'boot_id']} == target for r in rows),
                    'graphical transition in pair')
            require(len({json.dumps(r['power'], sort_keys=True) for r in rows}) == 1, 'power transition')
            power = rows[0]['power']
        except ValueError as error:
            valid, reason = False, str(error)
        require(valid == saved['valid'], 'pair eligibility differs')
        pairs[pair] = dict(valid=valid, reason=reason, state=dict(**saved['state'], power=power))
    comparisons, context = [], []
    for observer in [0, 1]:
        eligible = [c for c in captures if c['observer'] == observer and pairs[c['pair']]['valid']]
        states = {json.dumps(pairs[c['pair']]['state'], sort_keys=True) for c in eligible}
        for state in sorted(states):
            groups = {g: [c for c in eligible if c['group'] == g and
                          json.dumps(pairs[c['pair']]['state'], sort_keys=True) == state] for g in ['A', 'B']}
            for phase in ['32-1', '32-2', '32-3']:
                windows = {g: [next(w for w in c['windows'] if w['name'] == phase) for c in groups[g]]
                           for g in ['A', 'B']}
                for event in windows['A'][0]['counts']:
                    a, b = [[w['per_data_frame'][event] for w in windows[g]] for g in ['A', 'B']]
                    comparisons.append(dict(observer=observer, state=json.loads(state), phase=phase,
                                            counter=event, **summarize(a, b)))
                direction = int(phase.split('-')[1])
                streams = {g: [next(s for s in c['metrics']['streams'] if s['direction'] == direction)
                               for c in groups[g]] for g in ['A', 'B']}
                for bit, metric in [(1, 'h2d_bytes_s'), (2, 'd2h_bytes_s')]:
                    if not direction & bit:
                        continue
                    a, b = [[s[metric] for s in streams[g]] for g in ['A', 'B']]
                    summary = summarize(a, b)
                    context.append(dict(observer=observer, state=json.loads(state), phase=phase,
                        metric=metric, A=a, B=b, paired_ratios=summary['paired_ratios'],
                        median_ratio=summary['median_ratio'], stable=summary['stable'],
                        repeated_decrease=summary['stable'] and sum(y < x * .95 for x, y in zip(a, b)) >= 2))
    control = replay_control(root / 'control', ledger) if (root / 'control/final-registration.json').exists() else None
    return dict(format='owner-census-result-v1', captures=captures, pairs=pairs, comparisons=comparisons,
                diagnostic_throughput_context=context,
                product_control=control,
                scope='Diagnostic A/B only; counters cover whole START/STOP phase including prep/drain. '
                'Do not divide window CPU by these full-phase counts or transfer diagnostic timing '
                'to product A/B. State/power stratified, no replacement of excluded pairs. '
                'Original A-on recovered and smoke replayed; no images needed for replay.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    require(not args.out.exists(), 'refusing to overwrite replay')
    result = replay(args.evidence.resolve())
    with args.out.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Replayed', len(result['captures']), 'diagnostic captures')
