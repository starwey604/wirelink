#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Instrument independent source snapshots; never edit the product or SDK tree."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

HERE = Path(__file__).resolve().parent

def replace(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError(f'anchor drift: expected {count}, got {text.count(old)}: {old!r}')
    return text.replace(old, new)


def prepare(baseline, out, host_main):
    if out.exists():
        raise ValueError('refusing to overwrite source snapshots')
    out.mkdir(parents=True)
    for name, dest in [('wirelink', 'wirelink'), ('benchmark', 'wirelink-cross-benchmark'), ('astrial', 'astrial')]:
        with tarfile.open(baseline / (name + '.tar.gz')) as archive:
            archive.extractall(out / dest, filter='data')
    research = out / 'research'
    research.mkdir()
    for name in ['diagnostic.h', 'diagnostic.c', 'sem_trace.h', 'host_diagnostic.hpp']:
        shutil.copyfile(HERE / name, research / name)
    changes = {}
    def edit(relative, function):
        path = out / relative
        old = path.read_text()
        new = function(old)
        path.write_text(new)
        changes[relative] = ''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True), fromfile=relative, tofile=relative))
    def port(s):
        s = replace(s, '#include "wirelink/packet_port.h"', '#include "wirelink/packet_port.h"\n#ifdef __ZEPHYR__\n#include "diagnostic.h"\n#else\n#define D(name) ((void)0)\n#define diag_add(event, amount) ((void)0)\n#endif')
        s = replace(s, '  atomic_store_explicit(&port->activity, true, memory_order_release);', '  D(activity);\n  atomic_store_explicit(&port->activity, true, memory_order_release);')
        s = replace(s, '  if (port->config.notify != NULL) port->config.notify(port->config.user);', '  if (port->config.notify != NULL) { D(port_notify); port->config.notify(port->config.user); }')
        s = replace(s, '  atomic_store_explicit(&port->write_cursor, write + 1U, memory_order_release);', '  D(rx_commit);\n  atomic_store_explicit(&port->write_cursor, write + 1U, memory_order_release);')
        s = replace(s, '  atomic_store_explicit(&port->read_cursor, read + 1U, memory_order_release);', '  D(rx_release);\n  atomic_store_explicit(&port->read_cursor, read + 1U, memory_order_release);')
        s = replace(s, '  if (!claim_matches(port, claim)) return WL_ERR_NOT_FOUND;\n  atomic_store_explicit', '  if (!claim_matches(port, claim)) return WL_ERR_NOT_FOUND;\n  D(rx_abort);\n  atomic_store_explicit')
        s = replace(s, '  wl_packet_activity(port);\n  return WL_OK;\n}\n\nunsigned wl_packet_tx_take', '  D(tx_publish);\n  if (flags & WL_PACKET_CONSUMED) D(tx_consumed);\n  if (flags & WL_PACKET_DRAINED) D(tx_drained);\n  if (flags & WL_PACKET_TX_ERROR) D(tx_error);\n  wl_packet_activity(port);\n  return WL_OK;\n}\n\nunsigned wl_packet_tx_take')
        return s
    edit('wirelink/src/packet_port.c', port)
    def kernel(s):
        s = replace(s, '#include "wirelink/kernel.h"', '#include "wirelink/kernel.h"\n#ifdef __ZEPHYR__\n#include "diagnostic.h"\n#else\n#define D(name) ((void)0)\n#define diag_add(event, amount) ((void)0)\n#endif')
        s = replace(s, '  for (unsigned n = 0U; n < WL_KERNEL_CALLS; ++n) {', '  D(terminal_scan);\n  for (unsigned n = 0U; n < WL_KERNEL_CALLS; ++n) {')
        s = replace(s, '    finish_call(kernel, slot, result, NULL, 0U);\n    return 1;', '    diag_add(D_terminal_slot, n + 1U);\n    D(step_terminal);\n    finish_call(kernel, slot, result, NULL, 0U);\n    return 1;')
        s = replace(s, '  return 0;\n}\n\nstatic int submit_tx', '  diag_add(D_terminal_slot, WL_KERNEL_CALLS);\n  return 0;\n}\n\nstatic int submit_tx')
        s = replace(s, '    return 1;\n  }\n  return 0;\n}\n\nstatic int receive', '    D(step_tx);\n    return 1;\n  }\n  return 0;\n}\n\nstatic int receive')
        s = replace(s, '  kernel->rx_blocked = 0U;\n  kernel->rx_held = 0U;', '  D(step_rx);\n  kernel->rx_blocked = 0U;\n  kernel->rx_held = 0U;')
        s = replace(s, '  kernel->in_step = 1U;', '  D(step);\n  if (!(kernel->calls[0].state | kernel->calls[1].state | kernel->calls[2].state | kernel->calls[3].state)) D(step_no_calls);\n  diag_add(D_generation_slot, WL_KERNEL_CALLS);\n  kernel->in_step = 1U;')
        s = replace(s, 'if (wl_packet_tx_ready(kernel->port)) { error = complete_tx(kernel); ++count; }', 'if (wl_packet_tx_ready(kernel->port)) { D(step_completion); error = complete_tx(kernel); ++count; }')
        s = replace(s, '  kernel->in_step = 0U;\n  return result;', '  diag_add(D_step_work, count);\n  if (count == 0U) D(step_empty);\n  kernel->in_step = 0U;\n  return result;')
        s = replace(s, '  *has_deadline = 0;', '  D(ready);\n  diag_add(D_ready_call_slot, WL_KERNEL_CALLS);\n  *has_deadline = 0;')
        s = replace(s, '      (!kernel->tx_blocked || wl_packet_activity_pending(kernel->port)))\n    for', '      (!kernel->tx_blocked || wl_packet_activity_pending(kernel->port))) {\n    diag_add(D_ready_tx_slot, WL_KERNEL_TX_COUNT);\n    for')
        s = replace(s, 'if (kernel->tx[i].state == READY) ready = 1;\n  return ready;', 'if (kernel->tx[i].state == READY) ready = 1;\n  }\n  return ready;')
        return s
    edit('wirelink/src/kernel.c', kernel)
    def adapter(s):
        s = replace(s, '#include "wirelink/zephyr/kernel_usb_bulk.h"', '#include "wirelink/zephyr/kernel_usb_bulk.h"\n#include "diagnostic.h"')
        s = replace(s, '  if (adapter->wake_consumer != NULL) adapter->wake_consumer(adapter->wake_user_data);', '  D(adapter_notify);\n  if (adapter->wake_consumer != NULL) adapter->wake_consumer(adapter->wake_user_data);')
        s = replace(s, '  if (wake != NULL) wake(user);', '  D(callback_exit);\n  if (wake != NULL) wake(user);')
        s = replace(s, '    atomic_inc(&adapter->tx_completions);', '    D(callback_tx);\n    atomic_inc(&adapter->tx_completions);')
        s = replace(s, '  atomic_inc(&adapter->rx_completions);', '  D(callback_rx);\n  atomic_inc(&adapter->rx_completions);')
        s = replace(s, '    atomic_inc(&adapter->rx_pauses);', '    D(rx_pause);\n    atomic_inc(&adapter->rx_pauses);')
        s = replace(s, '  atomic_inc(&adapter->rx_claims);', '  D(rx_enqueue);\n  atomic_inc(&adapter->rx_claims);')
        s = replace(s, '  started = cycle_count(adapter);\n  active =', '  D(service);\n  started = cycle_count(adapter);\n  active =')
        s = replace(s, '    atomic_inc(&adapter->active_service_calls);', '    D(service_active);\n    atomic_inc(&adapter->active_service_calls);')
        # Count submit attempts/results at the binding without changing returns.
        s = replace(s, 'wl_packet_submit_t wl_kernel_usb_bulk_submit(', 'static wl_packet_submit_t diag_usb_submit_impl(')
        s += '\nwl_packet_submit_t wl_kernel_usb_bulk_submit(void *context, const uint8_t *data, size_t length, wl_packet_token_t token) {\n  D(tx_submit);\n  wl_packet_submit_t result = diag_usb_submit_impl(context, data, length, token);\n  if (result == WL_PACKET_BUSY) D(tx_busy);\n  return result;\n}\n'
        return s
    edit('wirelink/adapters/zephyr/usb_bulk/src/kernel_usb_bulk.c', adapter)
    def backend(s):
        s = replace(s, '#include "backend.h"', '#include "backend.h"\n#include "diagnostic.h"')
        s = replace(s, 'K_SEM_DEFINE(wake, 0, 1);', 'K_SEM_DEFINE(wake, 0, 1);\nstruct k_sem *diag_owner_sem = &wake;')
        s = replace(s, 'int ub_backend_service(void) {', 'int ub_backend_service(void) {\n  D(owner_pass);')
        s = replace(s, '  caller(&request, &reply); /* Existing benchmark business handler. */', '  D(handler);\n  caller(&request, &reply); /* Existing benchmark business handler. */')
        s = replace(s, 'static void on_sample(void *context, const sample_t *r) {', 'static void on_sample(void *context, const sample_t *r) {\n  D(sample);')
        s = replace(s, 'void ub_backend_wait(uint32_t timeout_ms) {', 'void ub_backend_wait(uint32_t timeout_ms) {\n  D(wait_query);')
        s = replace(s, 'wl_kernel_ready(&endpoint.core, now, &has_deadline, &deadline)) return;', 'wl_kernel_ready(&endpoint.core, now, &has_deadline, &deadline)) { D(wait_ready); return; }')
        s = replace(s, '  (void)k_sem_take(&wake, K_MSEC(timeout_ms));', '  if (k_sem_take(&wake, K_MSEC(timeout_ms)) != 0) D(sem_timeout);')
        return s
    edit('wirelink-cross-benchmark/usb/static_kernel/device/backend.c', backend)
    def main(s):
        s = replace(s, '#include "backend.h"', '#include "backend.h"\n#include "diagnostic.h"')
        s = replace(s, '    ub_cpu_start();\n    break;', '    ub_cpu_start();\n    diag_start();\n    break;')
        s = replace(s, '  case UB_STOP:\n', '  case 0x7f01:\n    v->length = (uint32_t)diag_json((char *)v->data, sizeof(v->data), r->v[1]);\n    if (!v->length) v->v[0] = 3;\n    break;\n  case UB_STOP:\n    diag_stop();\n')
        return s
    edit('wirelink-cross-benchmark/usb/device/main.c', main)
    edit('wirelink/zephyr/CMakeLists.txt', lambda s: s + f'\nzephyr_library_include_directories("{research}")\nzephyr_library_sources("{research}/diagnostic.c")\n')
    edit('wirelink-cross-benchmark/usb/static_kernel/device/CMakeLists.txt', lambda s: s + f'\ntarget_include_directories(app PRIVATE "{research}")\nset_property(SOURCE ${{ZEPHYR_BASE}}/kernel/sem.c DIRECTORY ${{ZEPHYR_BASE}}/kernel APPEND PROPERTY COMPILE_OPTIONS -include "{research}/sem_trace.h")\n')
    def event(s):
        s = replace(s, '#include <atomic>', f'#include <atomic>\n#include "{research}/host_diagnostic.hpp"')
        s = replace(s, '    if (!pending_.exchange(true, std::memory_order_acq_rel)) ready_.release();', '    diag_host::add(diag_host::notify);\n    if (!pending_.exchange(true, std::memory_order_acq_rel)) ready_.release();\n    else diag_host::add(diag_host::merged);')
        s = replace(s, '    if (!ready_.try_acquire_for(timeout)) return false;', '    diag_host::add(diag_host::wait);\n    if (!ready_.try_acquire_for(timeout)) return false;\n    diag_host::add(diag_host::wait_success);')
        s = replace(s, '  void consumed() noexcept {', '  void consumed() noexcept {\n    diag_host::add(diag_host::consumed);')
        return s
    edit('wirelink/include/wirelink/detail/coalescing_event.hpp', event)
    def host_adapter(s):
        s = replace(s, 'wl_err_t PacketUsbAdapter::service() {', 'wl_err_t PacketUsbAdapter::service() {\n  diag_host::add(diag_host::service);')
        s = replace(s, 'bool PacketUsbAdapter::ready() const {', 'bool PacketUsbAdapter::ready() const {\n  diag_host::add(diag_host::ready);')
        return s
    edit('wirelink/adapters/astrial_usb/src/packet_usb.cpp', host_adapter)
    # The diagnostic workload preserves the gated, full-size screen contract.
    source = host_main.read_text()
    def host(s):
        s = f'#include "{research}/host_diagnostic.hpp"\n' + s
        definition = '''static void diagnostic_counts() {
  diag_host::stop();
  for (unsigned page = 0; page < 16; ++page) {
    auto result = control(0x7f01, page);
    emit_control("device_diagnostic", result);
    std::string json(reinterpret_cast<const char *>(result.data), result.length);
    if (json.find("\\\"done\\\":true") != std::string::npos) return;
  }
  throw std::runtime_error("diagnostic page bound");
}
'''
        s = replace(s, 'static void output_samples(', definition + 'static void output_samples(')
        for phase in ['RPC', 'IDLE', 'LOW', 'SAMPLE']:
            anchor = f'      control(UB_START, UB_PHASE_{phase}, 0, 0, 0);'
            s = replace(s, anchor, anchor + '\n      diag_host::start();')
        s = replace(s, '        control(UB_START, UB_PHASE_STREAM, direction, size, duration);', '        control(UB_START, UB_PHASE_STREAM, direction, size, duration);\n        diag_host::start();')
        for phase in ['rpc', 'idle', 'low', 'sample', 'stream']:
            anchor = f'emit_control("{phase}_cpu", control(UB_CPU_STATS));'
            s = replace(s, anchor, anchor + '\n      diagnostic_counts();')
        return s
    new = host(source)
    (out / 'host-main.cpp').write_text(new)
    changes['host-main.cpp'] = ''.join(difflib.unified_diff(source.splitlines(True), new.splitlines(True), fromfile=str(host_main), tofile='host-main.cpp'))
    (out / 'diagnostic.patch').write_text('\n'.join(changes.values()))
    files = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in out.rglob('*') if p.is_file()}
    receipt = dict(format='wirelink-cpu-diagnostic-source-v1', product_optimization=False,
                   baseline=str(baseline), files=files, patched=list(changes),
                   event_window='device: after START cpu snapshot to beginning of STOP handler; includes control boundary traffic',
                   counters='relaxed atomic uint32; single-core IRQ-locked snapshots, modulo deltas; windows must stay below 2^32 events',
                   clocks_in_core=False)
    (out / 'diagnostic-source.json').write_text(json.dumps(receipt, indent=2)+'\n')
    return receipt

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--host-main', type=Path, required=True)
    a = p.parse_args()
    prepare(a.baseline.resolve(), a.out.resolve(), a.host_main.resolve())
