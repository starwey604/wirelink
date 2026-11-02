# SPDX-License-Identifier: Apache-2.0
"""Prepare five owner-only counters, with no hot-path helper or atomic calls."""
import argparse
import difflib
import json
from pathlib import Path
import shutil

from prepare import replace

EVENTS = ['owner_pass', 'chunk_attempt', 'chunk_block', 'progress_attempt', 'progress_block']
HEADER = '''/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OWNER_LOCAL_H
#define OWNER_LOCAL_H
#include <stdint.h>
typedef struct {
  uint32_t owner_pass, chunk_attempt, chunk_block, progress_attempt, progress_block;
} owner_local_counts_t;
/* All writers and both snapshots execute on the one benchmark owner. */
void owner_local_start(void);
void owner_local_snapshot(owner_local_counts_t *out);
#endif
'''
DEFINITIONS = '''
static owner_local_counts_t owner_local_current, owner_local_first;
void owner_local_start(void) { owner_local_first = owner_local_current; }
void owner_local_snapshot(owner_local_counts_t *out) {
  out->owner_pass = owner_local_current.owner_pass - owner_local_first.owner_pass;
  out->chunk_attempt = owner_local_current.chunk_attempt - owner_local_first.chunk_attempt;
  out->chunk_block = owner_local_current.chunk_block - owner_local_first.chunk_block;
  out->progress_attempt = owner_local_current.progress_attempt - owner_local_first.progress_attempt;
  out->progress_block = owner_local_current.progress_block - owner_local_first.progress_block;
}
'''


def prepare(frozen, out):
    if out.exists():
        raise ValueError('refusing to overwrite source snapshot')
    shutil.copytree(frozen, out)
    base = out / 'wirelink-cross-benchmark/usb'
    (base / 'common/owner_local.h').write_text(HEADER)
    changes = []

    def edit(path, transform):
        old = path.read_text()
        new = transform(old)
        path.write_text(new)
        relative = str(path.relative_to(out))
        changes.append(''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                                   fromfile=relative, tofile=relative)))

    def backend(s):
        s = replace(s, '#include "backend.h"', '#include "backend.h"\n#include "owner_local.h"')
        s = replace(s, 'static usb_kernel_t endpoint;', DEFINITIONS + '\nstatic usb_kernel_t endpoint;')
        s = replace(s, 'int ub_backend_service(void) {',
                    'int ub_backend_service(void) {\n  ++owner_local_current.owner_pass;')
        for kind, field, codec in [('CHUNK', 'chunk', 'stream_chunk'), ('PROGRESS', 'progress', 'progress')]:
            s = replace(s, f'  }} else if (message->kind == UB_{kind}) {{',
                        f'  }} else if (message->kind == UB_{kind}) {{\n    ++owner_local_current.{field}_attempt;')
            anchor = f'    error = usb_kernel_send_{codec}_view(&endpoint, &value);'
            s = replace(s, anchor, anchor +
                        f'\n    if (error == WL_ERR_WOULD_BLOCK) ++owner_local_current.{field}_block;')
        return s

    edit(base / 'static_kernel/device/backend.c', backend)

    def main(s):
        s = replace(s, '#include "backend.h"', '#include "backend.h"\n#include "owner_local.h"')
        s = replace(s, '  uint32_t transport_errors = 0;',
                    '  owner_local_counts_t owner_local_delta;\n  uint32_t transport_errors = 0;')
        s = replace(s, '    ub_cpu_start();\n    break;',
                    '    ub_cpu_start();\n    owner_local_start();\n    break;')
        s = replace(s, '  case UB_STOP:\n',
                    '  case UB_STOP:\n    owner_local_snapshot(&owner_local_delta);\n')
        export = '''    printk("owner_local_v1,phase=%u,direction=%u,size=%u,"
           "{\\"owner_pass\\":%u,\\"chunk_attempt\\":%u,\\"chunk_block\\":%u,"
           "\\"progress_attempt\\":%u,\\"progress_block\\":%u}\\n",
           phase, direction, chunk_size, owner_local_delta.owner_pass,
           owner_local_delta.chunk_attempt, owner_local_delta.chunk_block,
           owner_local_delta.progress_attempt, owner_local_delta.progress_block);
'''
        return replace(s, '    break;\n  case UB_IO_STATS:', export + '    break;\n  case UB_IO_STATS:')

    edit(base / 'device/main.c', main)
    (out / 'owner-local.patch').write_text('\n'.join(changes))
    (out / 'owner-local-source.json').write_text(json.dumps(dict(
        format='owner-local-source-v1', events=EVENTS, frozen=str(frozen),
        changes=['usb/common/owner_local.h', 'usb/static_kernel/device/backend.c', 'usb/device/main.c'],
        hot_path='Five direct unsigned increments, single writer; no atomics, IRQ readers or added helper calls.',
        window='After START CPU snapshot through STOP entry; includes preparation/drain.',
        export='Single UART record after original CPU stop/reply formatting; no wire or host changes.',
        product_optimization=False, restore_original_on_completion=False), indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frozen', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.frozen.resolve(), args.out.resolve())
