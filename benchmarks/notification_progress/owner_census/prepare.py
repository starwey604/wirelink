# SPDX-License-Identifier: Apache-2.0
"""Add identical phase counters to independent frozen source copies."""
import argparse
import difflib
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
EVENTS = ('owner_pass service service_active step step_work step_empty rx_release '
          'tx_submit tx_busy tx_take tx_publish tx_drained wake '
          'wait_query wait_ready wait_take wait_timeout '
          'chunk_attempt chunk_ok chunk_block progress_attempt progress_ok progress_block '
          'send_error').split()


def replace(source, old, new, count=1):
    if source.count(old) != count:
        raise ValueError(f'anchor drift: {old!r}: {source.count(old)} != {count}')
    return source.replace(old, new)


def prepare(frozen, out):
    if out.exists():
        raise ValueError('refusing to overwrite source snapshot')
    shutil.copytree(frozen, out)
    research = out / 'research'
    research.mkdir()
    # Reuse the established lock-free relaxed atomic/snapshot/export engine.
    shutil.copyfile(HERE.parent / 'hardware/diagnostic.c', research / 'diagnostic.c')
    header = (HERE.parent / 'hardware/diagnostic.h').read_text()
    start = header.index('#define DIAG_EVENTS(X)')
    end = header.index('#define DIAG_ENUM', start)
    header = header[:start] + '#define DIAG_EVENTS(X) ' + ' '.join('X(' + x + ')' for x in EVENTS) + '\n' + header[end:]
    (research / 'diagnostic.h').write_text(header)
    changes = []

    def edit(relative, transform):
        path = out / relative
        old = path.read_text()
        new = transform(old)
        path.write_text(new)
        changes.append(''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                                   fromfile=relative, tofile=relative)))

    def backend(s):
        s = replace(s, '#include "backend.h"', '#include "backend.h"\n#include "diagnostic.h"')
        s = replace(s, 'static void notify(void *context) { (void)context; k_sem_give(&wake); }',
                    'static void notify(void *context) { D(wake); (void)context; k_sem_give(&wake); }')
        s = replace(s, 'int ub_backend_service(void) {', 'int ub_backend_service(void) {\n  D(owner_pass);')
        for kind, suffix in [('CHUNK', 'chunk'), ('PROGRESS', 'progress')]:
            s = replace(s, f'  }} else if (message->kind == UB_{kind}) {{',
                        f'  }} else if (message->kind == UB_{kind}) {{\n    D({suffix}_attempt);')
            old = f'    error = usb_kernel_send_{"stream_chunk" if suffix == "chunk" else suffix}_view(&endpoint, &value);'
            s = replace(s, old, old + f'\n    if (error == WL_OK) D({suffix}_ok);\n    else if (error == WL_ERR_WOULD_BLOCK) D({suffix}_block);\n    else D(send_error);')
        s = replace(s, 'void ub_backend_wait(uint32_t timeout_ms) {',
                    'void ub_backend_wait(uint32_t timeout_ms) {\n  D(wait_query);')
        s = replace(s, 'wl_kernel_ready(&endpoint.core, now, &has_deadline, &deadline)) return;',
                    'wl_kernel_ready(&endpoint.core, now, &has_deadline, &deadline)) { D(wait_ready); return; }')
        s = replace(s, '  (void)k_sem_take(&wake, K_MSEC(timeout_ms));',
                    '  D(wait_take);\n  if (k_sem_take(&wake, K_MSEC(timeout_ms)) != 0) D(wait_timeout);')
        return s

    edit('wirelink-cross-benchmark/usb/static_kernel/device/backend.c', backend)

    def kernel(s):
        s = replace(s, '#include "wirelink/kernel.h"', '#include "wirelink/kernel.h"\n#include "diagnostic.h"')
        s = replace(s, '  kernel->in_step = 1U;', '  D(step);\n  kernel->in_step = 1U;')
        return replace(s, '  kernel->in_step = 0U;\n  return result;',
                       '  diag_add(D_step_work, count);\n  if (count == 0U) D(step_empty);\n  kernel->in_step = 0U;\n  return result;')

    edit('wirelink/src/kernel.c', kernel)

    def packet(s):
        s = replace(s, '#include "wirelink/packet_port.h"', '#include "wirelink/packet_port.h"\n#include "diagnostic.h"')
        s = replace(s, '  atomic_store_explicit(&port->read_cursor, read + 1U, memory_order_release);',
                    '  D(rx_release);\n  atomic_store_explicit(&port->read_cursor, read + 1U, memory_order_release);')
        s = replace(s, '  wl_packet_submit_t result = port->config.submit',
                    '  D(tx_submit);\n  wl_packet_submit_t result = port->config.submit')
        s = replace(s, '  if (result == WL_PACKET_BUSY || result == WL_PACKET_FAILED) {',
                    '  if (result == WL_PACKET_BUSY) D(tx_busy);\n  if (result == WL_PACKET_BUSY || result == WL_PACKET_FAILED) {')
        s = replace(s, '  wl_packet_activity(port);\n  return WL_OK;\n}\n\nunsigned wl_packet_tx_take',
                    '  D(tx_publish);\n  if (flags & WL_PACKET_DRAINED) D(tx_drained);\n  wl_packet_activity(port);\n  return WL_OK;\n}\n\nunsigned wl_packet_tx_take')
        return replace(s, '  return flags;', '  D(tx_take);\n  return flags;')

    edit('wirelink/src/packet_port.c', packet)

    def adapter(s):
        s = replace(s, '#include "wirelink/zephyr/kernel_usb_bulk.h"',
                    '#include "wirelink/zephyr/kernel_usb_bulk.h"\n#include "diagnostic.h"')
        s = replace(s, '  started = cycle_count(adapter);\n  active =',
                    '  D(service);\n  started = cycle_count(adapter);\n  active =')
        return replace(s, '    atomic_inc(&adapter->active_service_calls);',
                       '    D(service_active);\n    atomic_inc(&adapter->active_service_calls);')

    edit('wirelink/adapters/zephyr/usb_bulk/src/kernel_usb_bulk.c', adapter)

    def main(s):
        s = replace(s, '#include "backend.h"', '#include "backend.h"\n#include "diagnostic.h"')
        s = replace(s, '    ub_cpu_start();\n    break;', '    ub_cpu_start();\n    diag_start();\n    break;')
        s = replace(s, '  case UB_STOP:\n', '  case UB_STOP:\n    diag_stop();\n')
        anchor = '    break;\n  case UB_IO_STATS:'
        export = '''    for (unsigned page = 0U; page < (D_COUNT + 7U) / 8U; ++page) {
      char record[512];
      size_t length = diag_json(record, sizeof(record), page);
      __ASSERT(length != 0U, "census export capacity");
      printk("owner_census_v1,phase=%u,direction=%u,size=%u,%s\\n",
             phase, direction, chunk_size, record);
    }
'''
        return replace(s, anchor, export + anchor)

    edit('wirelink-cross-benchmark/usb/device/main.c', main)
    edit('wirelink/zephyr/CMakeLists.txt', lambda s: s +
         f'\nzephyr_library_include_directories("{research}")\nzephyr_library_sources("{research}/diagnostic.c")\n')
    edit('wirelink-cross-benchmark/usb/static_kernel/device/CMakeLists.txt', lambda s: s +
         f'\ntarget_include_directories(app PRIVATE "{research}")\n')
    (out / 'diagnostic.patch').write_text('\n'.join(changes))
    receipt = dict(format='owner-census-source-v1', frozen=str(frozen), events=EVENTS,
                   engine=str(HERE.parent / 'hardware/diagnostic.c'),
                   event_window='START after CPU snapshot through STOP handler entry; includes prep/drain and control boundary traffic',
                   export='UART only, after STOP CPU snapshot and reply formatting; no host/wire changes',
                   product_optimization=False, sdk_edits=False)
    (out / 'diagnostic-source.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frozen', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.frozen.resolve(), args.out.resolve())
