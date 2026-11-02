# SPDX-License-Identifier: Apache-2.0
"""Replay the bounded five-counter gate and no-reflash handoff."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

from analyze import read_capture, summarize
from counter_rows import require
from local_gate import contrast, reference_ready, representative
from prepare_local import EVENTS


def replay(root):
    sys.path.insert(0, str(root / 'tools'))
    from usb_evidence import sha
    from host_state import window_state

    reg = json.loads((root / 'final-registration.json').read_text())
    status = json.loads((root / 'status.json').read_text())
    require(reg['format'] == 'owner-local-AB-v1', 'registration format')
    require(status['stage'] == 'complete' and status['restore_original'] is False, 'incomplete/local handoff')
    require(sha(root / 'final-builds.json') == reg['build_registry_sha256'], 'build registry changed')
    require(sha(root / 'source-audit.json') == reg['source_receipt'], 'source audit changed')
    for name, digest in reg['tools'].items():
        require(sha(root / 'tools' / name) == digest, 'tool changed: ' + name)
    builds = json.loads((root / 'final-builds.json').read_text())
    require(len({b['runner_sha256'] for b in builds.values()}) == 1, 'shared host differs')
    previous = Path('/home/ww/codings/wirelink-required-fixed-20261101')
    original = json.loads((previous / 'final-builds.json').read_text())
    old_reg = json.loads((previous / 'final-registration.json').read_text())
    require(sha(previous / 'final-builds.json') == old_reg['build_registry_sha256'], 'original image registry')
    for g in ['A', 'B']:
        require(builds[f'P{g}-o1']['artifacts']['zephyr/zephyr.bin'] ==
                original[f'{g}-o1']['artifacts']['zephyr/zephyr.bin'], 'not original product image')
    path = Path(__file__).parents[1] / 'a_residency/stream_ledger.py'
    spec = importlib.util.spec_from_file_location('local_stream_ledger', path)
    ledger = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ledger)
    expected = [('P' + g, 'control-on', 0) for g in ['A', 'B']]
    expected += [('L' + g, f'r{r}-on', r) for r, order in enumerate(['AB', 'BA', 'AB'], 1) for g in order]
    require([(e['group'], e['pair'], e['round']) for e in reg['order']] == expected, 'conditional schedule changed')
    want = {(g, 'smoke') for g in ['PA', 'PB', 'LA', 'LB']} | {(g, 'boundary') for g in ['LA', 'LB']}
    require(len(reg['qualifications']) == 6 and
            {(q['group'], q['mode']) for q in reg['qualifications']} == want, 'missing/duplicate qualifications')
    names = []
    for q in reg['qualifications']:
        require(sha(root / 'captures' / q['name'] / 'manifest.json') == q['sha256'], 'qualification changed')
        read_capture(root, builds, reg, ledger, q['name'], q['group'], 1, q['mode'], True)
        names.append(q['name'])
    sidecar = [json.loads(line) for line in (root / 'host-state.jsonl').read_text().splitlines()]
    captures, pairs, local_contrasts = [], {}, {}

    def read_pair(pair_name):
        entries = [e for e in reg['order'] if e['pair'] == pair_name]
        readings = []
        for e in entries:
            c = read_capture(root, builds, reg, ledger, e['name'], e['group'], 1, 'screen', False)
            captures.append(dict(**e, **{k: v for k, v in c.items() if k != 'name'}))
            names.append(e['name'])
            readings.append(c)
        saved = json.loads((root / (pair_name + '-state.json')).read_text())
        require(saved['windows'] == [c['window'] for c in readings], 'pair time bounds')
        valid, reason = True, None
        try:
            rows = window_state(sidecar, readings[0]['window']['start'], readings[-1]['window']['end'],
                                saved['state']['locked'])
            require(all({k: row[k] for k in saved['state']} == saved['state'] for row in rows),
                    'graphical/power transition')
        except ValueError as error:
            valid, reason = False, str(error)
        require(valid == saved['valid'], 'pair eligibility differs')
        pairs[pair_name] = dict(valid=valid, reason=reason, state=saved['state'])
        bygroup = {e['group'][-1]: c['metrics'] for e, c in zip(entries, readings)}
        return contrast(bygroup['A'], bygroup['B'], pairs[pair_name])

    reference = read_pair('control-on')
    gate = reference_ready(reference)
    if gate['pass_gate']:
        pilot = read_pair('r1-on')
        local_contrasts['r1-on'] = pilot
        gate = representative(reference, pilot)
        if gate['pass_gate']:
            local_contrasts['r2-on'] = read_pair('r2-on')
            local_contrasts['r3-on'] = read_pair('r3-on')
    saved_gate = json.loads((root / 'gate.json').read_text())
    require(all(saved_gate[k] == gate[k] for k in ['pass_gate', 'reason']), 'gate decision differs')
    require(status['gate'] == gate, 'final gate differs')
    require(status['outcome'] == ('count_matrix_complete' if gate['pass_gate'] else 'gate_stopped'),
            'conditional stop/completion differs')
    require({p.name for p in (root / 'captures').iterdir() if p.is_dir()} == set(names),
            'extra/missing capture or unregistered restore')
    last = captures[-1]
    b = builds[last['group'] + '-o1']
    require(status['last_healthy'] == dict(name=last['name'], group=last['group'], observer=1,
                build_id=b['build_id'], diagnostic=b['diagnostic'],
                bin_sha256=b['artifacts']['zephyr/zephyr.bin']['sha256']), 'last healthy firmware differs')
    require(status['last_attempt'] == dict(name=last['name'], group=last['group'], observer=1),
            'last attempt differs from healthy handoff')
    comparisons = []
    local = [c for c in captures if c['kind'] == 'local' and pairs[c['pair']]['valid']]
    states = {json.dumps(pairs[c['pair']]['state'], sort_keys=True) for c in local}
    for state in sorted(states):
        groups = {g: [c for c in local if c['group'] == 'L' + g and
                      json.dumps(pairs[c['pair']]['state'], sort_keys=True) == state] for g in ['A', 'B']}
        for phase in ['32-1', '32-2', '32-3']:
            windows = {g: [next(w for w in c['windows'] if w['name'] == phase) for c in groups[g]] for g in ['A', 'B']}
            for event in EVENTS:
                a, b = [[w['per_data_frame'][event] for w in windows[g]] for g in ['A', 'B']]
                summary = summarize(a, b)
                boundary = phase == '32-2' and event.startswith('progress_')
                comparisons.append(dict(state=json.loads(state), phase=phase, counter=event, **summary,
                    event_scope='phase_end_marker' if boundary else 'owner_or_stream_admission',
                    usable_for_frequency_inference=gate['pass_gate'] and summary['stable'] and not boundary))
    return dict(format='owner-local-result-v1', reference=reference, gate=gate, captures=captures,
                pairs=pairs, local_contrasts=local_contrasts, comparisons=comparisons, handoff=status['last_healthy'],
                restore_original=False,
                scope='CPU-on five-field representativeness gate, not product acceptance. Whole-phase '
                'counts include prep/drain; do not divide window CPU by them. Failed gate stops '
                'software count method; no extra pairs/configurations. Leave last healthy firmware installed.')


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
    print('Replayed', len(result['captures']), 'screens; gate:', result['gate']['reason'])
