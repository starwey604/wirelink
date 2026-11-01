# SPDX-License-Identifier: Apache-2.0
"""Validate phase-end UART counter pages against the unchanged host workload."""
import json
import re
from prepare import EVENTS

LINE = re.compile(rb'owner_census_v1,phase=(\d+),direction=(\d+),size=(\d+),(\{[^\r\n]*\})')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def counter_windows(console, rows):
    expected = []
    for row in rows:
        event = row['event']
        if event in ['rpc_device', 'idle_device', 'low_device', 'sample_device']:
            phase = ['rpc_device', 'idle_device', 'low_device', 'sample_device'].index(event) + 1
            expected.append(dict(name=event.removesuffix('_device'), phase=phase,
                                 direction=0, size=0, device=row['device']))
        elif event == 'stream_device':
            stream_device = row['device']
        elif event == 'stream':
            expected.append(dict(name=f"{row['size']}-{row['direction']}", phase=5,
                                 direction=row['direction'], size=row['size'], device=stream_device))
    pages = list(LINE.finditer(console))
    width = (len(EVENTS) + 7) // 8
    require(len(pages) == len(expected) * width, 'missing/extra census pages')
    windows = []
    for n, phase in enumerate(expected):
        counts = {}
        for page, match in enumerate(pages[n * width:(n + 1) * width]):
            require(tuple(int(v) for v in match.groups()[:3]) ==
                    tuple(phase[k] for k in ['phase', 'direction', 'size']), 'census phase order')
            data = json.loads(match[4])
            require(data['page'] == page and data['done'] is (page == width - 1), 'census page order/end')
            require(list(data['counts']) == EVENTS[page * 8:(page + 1) * 8], 'census fields/order')
            require(all(type(v) is int and 0 <= v < 2**32 for v in data['counts'].values()),
                    'census uint32 domain')
            counts.update(data['counts'])
        require(counts['send_error'] == 0, 'census send error')
        for kind in ['chunk', 'progress']:
            require(counts[kind + '_attempt'] == counts[kind + '_ok'] + counts[kind + '_block'],
                    'send result partition')
        require(counts['step'] == counts['owner_pass'] and counts['service'] == 2 * counts['owner_pass'],
                'owner/step/two-service accounting')
        require(counts['service_active'] <= counts['service'] and counts['step_empty'] <= counts['step'],
                'active/empty accounting')
        require(counts['wait_query'] == counts['wait_ready'] + counts['wait_take'] and
                counts['wait_timeout'] <= counts['wait_take'], 'wait partition')
        if phase['phase'] == 5:
            require(counts['chunk_ok'] == phase['device']['sent'], 'successful Chunk ledger')
        else:
            require(counts['chunk_attempt'] == counts['progress_attempt'] == 0, 'non-stream sends')
        frames = phase['device']['received'] + phase['device']['sent']
        windows.append(dict(**phase, counts=counts, data_frames=frames,
                            per_data_frame={k: v / frames for k, v in counts.items()} if frames else None))
    return windows
