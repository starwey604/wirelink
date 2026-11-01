# SPDX-License-Identifier: Apache-2.0
"""Replay calibrated same-image PMU probes; retain the scope of every counter."""
import argparse
import collections
import importlib.util
import json
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('cache_phase_replay', HERE.parent/'cache_phase/analyze.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


def selected(index):
    return index % 64 == ((index // 64)*17+17) % 64


def summarize(records):
    groups = collections.defaultdict(list)
    for r in records:
        # Same event counts and same observed register-window state, not a guessed cache mode.
        key = (r[3], r[10], r[5], *r[6:10])
        groups[key].append(r)
    out = []
    for key, rows in sorted(groups.items()):
        cycles, stalls = [[r[i] for r in rows] for i in [2, 4]]
        out.append(dict(inner_windows=key[0], outer_windows=key[1], ibus_misses=key[2],
            register_state=list(key[3:]), samples=len(rows), cycles=dict(min=min(cycles),
            max=max(cycles), mean=statistics.mean(cycles)), stalls=dict(min=min(stalls),
            max=max(stalls), mean=statistics.mean(stalls)), indices=[r[0] for r in rows]))
    return out


def replay(root):
    result = base.replay(root, registration_format='pmu-window-QP-pairs-v1', qualification_root=root)
    pooled = {0: [], 1: []}
    for c in result['captures']:
        rows = [json.loads(line) for line in (root/'captures'/c['name']/'host.jsonl').read_text().splitlines()]
        c['calibration'] = next(r['device'] for r in rows if r['event'] == 'pmu_probe_config')
        c['pmu_windows'] = c.pop('cache_windows')
        sparse = next(w['records'] for w in c['pmu_windows'] if w['cpu_event'] == 'sample_cpu')
        c['sparse_groups'] = summarize(sparse)
        samples = [v for r in rows if r['event'] == 'sample_api' for v in r['device']['cycles']]
        unselected = [v for i, v in enumerate(samples) if not selected(i)]
        c['unselected'] = dict(count=len(unselected), mean_cycles=statistics.mean(unselected),
                               histogram=dict(collections.Counter(unselected)))
        c['eligible_pair'] = result['pairs'][c['pair']]['valid']
        if c['eligible_pair']:
            pooled[c['observer']].extend(sparse)
    for comparison in result['comparisons'].values():
        # This is a mechanism experiment, not a preregistered product/2%-CPU acceptance.
        comparison.pop('target_2percent', None)
        comparison.pop('repeated_guard_failure', None)
    result.update(format='pmu-window-QP-result-v1', sparse_by_observer={
        str(o): summarize(records) for o, records in pooled.items()},
        scope='Diagnostic image only. Inner window/stall/miss snapshots are sequential, include sampling instructions and possible IRQ/preemption, and exclude timestamp helpers. Outer window counts include both timestamp helpers. Equal event counts do not imply equal exception latency. I_STALL_ALL is instruction-related and other global stalls, not a cache-only counter. Empty-bracket calibration is not subtracted. All successful Sample observations and attempt/busy ledgers are retained. Pooled event groups describe eligible pairs only; performance comparisons remain stratified by graphical state. No extrapolation to uninstrumented A or exclusive core costs.')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        raise ValueError('refusing to overwrite replay output')
    result = replay(args.evidence.resolve())
    with args.out.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Replayed', len(result['captures']), 'PMU screens')
