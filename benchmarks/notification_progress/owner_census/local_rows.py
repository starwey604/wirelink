# SPDX-License-Identifier: Apache-2.0
"""Five owner-local counters; successful admissions are derived, not probed."""
import json
import re

from counter_rows import expected_windows, require
from prepare_local import EVENTS

LINE = re.compile(rb'owner_local_v1,phase=(\d+),direction=(\d+),size=(\d+),(\{[^\r\n]*\})')


def local_windows(console, rows):
    expected = expected_windows(rows)
    records = list(LINE.finditer(console))
    require(len(records) == len(expected), 'missing/extra owner-local records')
    windows = []
    for phase, match in zip(expected, records):
        require(tuple(int(v) for v in match.groups()[:3]) ==
                tuple(phase[k] for k in ['phase', 'direction', 'size']), 'owner-local phase order')
        counts = json.loads(match[4])
        require(list(counts) == EVENTS and
                all(type(v) is int and 0 <= v < 2**32 for v in counts.values()), 'owner-local fields/domain')
        successes = {}
        for kind in ['chunk', 'progress']:
            require(counts[kind + '_block'] <= counts[kind + '_attempt'], 'owner-local block/attempt partition')
            successes[kind] = counts[kind + '_attempt'] - counts[kind + '_block']
        # Negative send errors are independently rejected by raw business/console health.
        if phase['phase'] == 5:
            require(successes['chunk'] == phase['device']['sent'], 'owner-local Chunk success ledger')
        else:
            require(counts['chunk_attempt'] == counts['progress_attempt'] == 0, 'owner-local non-stream sends')
        frames = phase['device']['received'] + phase['device']['sent']
        windows.append(dict(**phase, counts=counts, successful=successes, data_frames=frames,
                            per_data_frame={k: v / frames for k, v in counts.items()} if frames else None))
    return windows
