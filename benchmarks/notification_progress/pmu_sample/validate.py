# SPDX-License-Identifier: Apache-2.0
"""Validate PMU calibration and sparse records against original Sample exports."""
CPU = ('rpc_cpu', 'idle_cpu', 'low_cpu', 'sample_cpu', 'stream_cpu')
WINDOW_CONTROL = (15 << 4) | (5 << 8) | (0x20 << 16)
STALL_CONTROL = (15 << 4) | (4 << 8) | (0x1ff << 16)


def require(value, message):
    if not value:
        raise ValueError(message)


def u32(value):
    return type(value) is int and 0 <= value < 2**32


def selected(index):
    return index % 64 == ((index // 64)*17+17) % 64


def calibration(data, mode):
    require(data['mode'] == mode and data['calibrated'] is True and
            data['error'] == data['prior_pgm'] == 0, 'PMU calibration/ownership failed')
    require(all(u32(v) for k, v in data.items() if k != 'calibrated'), 'calibration domain')
    require(data['ctrl0'] == WINDOW_CONTROL and data['ctrl1'] == STALL_CONTROL, 'PMU controls')
    require(data['clock'] >= 1000 and data['clock']-data['clock']//5 <= data['cycles'] <=
            data['clock']+256, 'PMU cycles versus CCOUNT calibration')
    require(data['deep'] > data['shallow']+32 and data['divide_stall'] > 0,
            'window/division calibration inactive')
    require(data['empty_windows'] == 0, 'snapshot bracket adds window events')


def probe_windows(rows, mode, screen=True):
    require(type(mode) is int and mode in (-1, 0, 1), 'unknown PMU mode')
    configs = [r['device'] for r in rows if r['event'] == 'pmu_probe_config']
    if mode == -1:
        require(not configs and not any(r['event'] == 'pmu_probe' for r in rows),
                'baseline used PMU commands')
        return []
    require(len(configs) == 1, 'missing/duplicate calibration')
    calibration(configs[0], mode)
    cycles = [v for r in rows if r['event'] == 'sample_api' for v in r['device']['cycles']]
    starts = [v for r in rows if r['event'] == 'sample_api' for v in r['device']['starts']]
    require(len(cycles) == len(starts) == 1000, 'original Sample export missing')
    windows, current = [], None
    for row in rows:
        if row['event'] in CPU:
            require(current is None, 'incomplete PMU window')
            current = dict(cpu_event=row['event'], pages=0, metadata=None, records=[])
        elif row['event'] == 'pmu_probe':
            require(current is not None, 'unscoped/duplicate PMU page')
            d = row['device']
            require(u32(d['page']) and d['page'] == current['pages'], 'PMU page order')
            require(type(d['done']) is bool, 'PMU done domain')
            if d['page'] == 0:
                require(set(d) == {'page', 'epoch', 'mode', 'count', 'attempts', 'busy', 'errors',
                                   'windows', 'stalls', 'access', 'miss', 'done'}, 'PMU metadata fields')
                require(all(u32(v) for k, v in d.items() if k != 'done'), 'PMU metadata domain')
                require(d['epoch'] == len(windows)+1 and d['mode'] == mode and
                        d['errors'] == 0, 'PMU epoch/mode/error')
                sample = current['cpu_event'] == 'sample_cpu'
                expected = 16 if sample and mode == 1 else 0
                require(d['count'] == expected, 'sparse record count')
                require(d['attempts'] == (1000+d['busy'] if sample else 0) and
                        (sample or d['busy'] == 0), 'Sample attempt ledger')
                require(d['access'] > 0, 'IBUS counter inactive')
                current['metadata'] = d
            else:
                require(set(d) == {'page', 'records', 'done'} and len(d['records']) == 4,
                        'PMU record page shape')
                for r in d['records']:
                    require(len(r) == 11 and all(u32(v) for v in r), 'PMU record domain')
                    current['records'].append(r)
            current['pages'] += 1
            require(d['done'] == (len(current['records']) == current['metadata']['count']),
                    'early/false PMU done')
            if d['done']:
                expected = [i for i in range(1000) if selected(i)] if current['metadata']['count'] else []
                require([r[0] for r in current['records']] == expected, 'stratified sample schedule')
                for index, start, elapsed, window, stall, miss, b0, s0, b1, s1, outer in current['records']:
                    require(start == starts[index] and elapsed == cycles[index], 'original Sample clock mismatch')
                    require(window <= elapsed and stall <= elapsed, 'PMU subset exceeds elapsed scope')
                    require(outer >= window, 'outer window count excludes inner interval')
                    require(b0 < 16 and b1 < 16 and s0 < 65536 and s1 < 65536, 'window register domain')
                windows.append(current)
                current = None
    require(current is None and len(windows) == sum(r['event'] in CPU for r in rows), 'missing PMU phases')
    if screen:
        require([w['cpu_event'] for w in windows] ==
                ['rpc_cpu', 'idle_cpu', 'low_cpu', 'sample_cpu']+['stream_cpu']*3, 'PMU phase order')
    return windows
