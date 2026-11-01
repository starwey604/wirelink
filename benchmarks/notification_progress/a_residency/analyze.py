# SPDX-License-Identifier: Apache-2.0
"""Inventory the retained A ELF, not a rebuilt or instrumented firmware."""
import argparse
import collections
import difflib
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
from elftools.elf.elffile import ELFFile

API = ['send_message$constprop$0', 'ub_backend_send', 'usb_kernel_send_sample_view',
       'wl_kernel_send', 'encode_frame', 'usb_kernel_encode_sample_view', 'sample_encode',
       'wlc_scalar_shape_1_encode', 'wlc_scalar_shape_1_wlc_detail_measure', 'wlc_put32',
       'wl_kernel_native_none_seal']
# Reviewed against the actual A assembly and frozen benchmark/generated sources:
# kind=Sample, four presence flags true, valid pointers/capacity, free ordinary
# snapshot, successful encode/seal. This is an annotated path, not a CPU emulator.
SUCCESS_BRANCHES = {
    'send_message$constprop$0': {0xe: True},
    'ub_backend_send': {0x5: False, 0x8b: True},
    'usb_kernel_send_sample_view': {0xd: False, 0x14: False},
    'wl_kernel_send': {o:False for o in [0x1b,0x21,0x2d,0x3b,0x47]},
    'encode_frame': {0x9: True, 0x23: False, 0x2c: False, 0x40: False},
    'usb_kernel_encode_sample_view': {}, 'sample_encode': {}, 'wlc_put32': {},
    'wlc_scalar_shape_1_encode': {o:False for o in [0x11,0x14,0x19,0x1b,0x1e,0x26,0x3f,0x5b,0x77]},
    'wlc_scalar_shape_1_wlc_detail_measure': {o:False for o in [0xb,0x12,0x19,0x1e,0x23,0x28]},
    'wl_kernel_native_none_seal': {o:False for o in [0xe,0x15,0x1f,0x26]},
}
GROUPS = {
    'owner_progress': ['ub_backend_service', 'usb_kernel_step', 'wl_kernel_step',
        'wl_kernel_ready', 'ub_backend_wait', 'wl_kernel_usb_bulk_service',
        'wl_kernel_usb_bulk_ready', 'wl_kernel_usb_bulk_submit', 'wl_packet_tx_publish',
        'wl_packet_tx_submit', 'wl_packet_tx_take', 'wl_packet_tx_ready',
        'wl_packet_tx_active', 'wl_packet_take_activity', 'wl_packet_rx_borrow',
        'wl_packet_rx_release', 'bulk_request', 'bulk_request_impl', 'record_cycles',
        'cycle_count', 'atomic_test_bit', 'atomic_set_bit@wl_kernel_usb_bulk_service', 'atomic_clear_bit',
        'atomic_set$isra$0', 'atomic_inc$isra$0', 'notify', 'wl_kernel_usb_bulk_notify'],
    'usb_dispatch': ['usbd_thread', 'usbd_ep_enqueue', 'udc_ep_enqueue',
        'udc_submit_ep_event', 'udc_dwc2_ep_enqueue', 'udc_dwc2_thread_0',
        'dwc2_tx_fifo_write', 'dwc2_handle_xfer_next', 'dwc2_prep_rx$isra$0',
        'udc_dwc2_isr_handler', 'net_buf_alloc_with_data'],
    'observer_periodic': ['sys_trace_idle', 'sys_trace_idle_exit', 'sys_trace_isr_enter',
        'close_idle$part$0', 'ub_cpu_usb_irq_enter', 'ub_cpu_usb_irq_exit',
        '__wrap_usbd_ep_enqueue'],
    'observer_boundary': ['thread_snapshot', 'snapshot', 'ub_cpu_start', 'ub_cpu_stop',
                          'ub_cpu_json', 'ub_observe_json'],
}
INSN = re.compile(r'^([0-9a-f]+):\s+([0-9a-f]{2,16})\s+([\w.]+)(?:\s+(.*))?$')
HEADER = re.compile(r'^([0-9a-f]+) <([^>]+)>:$')
TARGET = re.compile(r'([0-9a-f]{8}) <([^>]+)>')


def require(value, message):
    if not value:
        raise ValueError(message)


def line_addresses(start, size):
    return set(range(start // 32 * 32, (start + size + 31) // 32 * 32, 32)) if size else set()


def flash(address):
    return 0x42000000 <= address < 0x44000000


def parse_disassembly(text):
    functions, current = {}, None
    for line in text.splitlines():
        h = HEADER.match(line)
        if h:
            current = int(h[1], 16)
            functions[current] = []
        m = INSN.match(line)
        if m and current is not None:
            functions[current].append(dict(address=int(m[1], 16), size=len(m[2]) // 2,
                encoding=m[2], op=m[3], args=m[4] or ''))
    return functions


def walk_success(code, choices):
    by_address = {i['address']:i for i in code}
    start, pc, visited, result = code[0]['address'], code[0]['address'], set(), []
    while True:
        require(pc not in visited and pc in by_address, 'path loops or leaves function')
        visited.add(pc)
        i = by_address[pc]
        require(i['op'] not in ['.byte', 'ill'], 'path entered data/padding')
        result.append(i)
        if i['op'].startswith('retw'):
            return result
        branch = i['op'].startswith('b')
        if branch:
            require(pc-start in choices, 'unannotated conditional branch at '+hex(pc-start))
        if i['op'] == 'j' or (branch and choices[pc-start]):
            target = TARGET.search(i['args'])
            require(target is not None, 'unresolved path branch')
            pc = int(target[1], 16)
        else:
            pc += i['size']


class Image:
    def __init__(self, root, objdump):
        self.root = root
        self.receipt = json.loads((root/'build.json').read_text())
        require(self.receipt['complete'] and self.receipt['library'] == 'wirelink', 'A receipt')
        # Bind this machine-code analysis to the ELF already used by the A measurements.
        # This digest checks artifact identity, not ordinary source-file changes.
        payload = (root/'device/zephyr/zephyr.elf').read_bytes()
        expected = self.receipt['artifacts']['artifacts/firmware.elf']
        require(len(payload) == expected['size'] and hashlib.sha256(payload).hexdigest() ==
                expected['sha256'], 'ELF does not match retained A build receipt')
        elf = ELFFile(io.BytesIO(payload))
        self.sections = [dict(name=s.name, address=s['sh_addr'], size=s.data_size, data=s.data())
                         for s in elf.iter_sections() if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS']
        self.symbols = collections.defaultdict(list)
        self.exact = collections.defaultdict(list)
        for s in elf.get_section_by_name('.symtab').iter_symbols():
            if s['st_shndx'] != 'SHN_UNDEF' and s.name:
                self.symbols[s.name].append(dict(address=s['st_value'], size=s['st_size'],
                    type=s['st_info']['type']))
                self.exact[s['st_value']].append(s.name)
        self.asm = subprocess.check_output([str(objdump), '-d', '-w',
                       str(root/'device/zephyr/zephyr.elf')], text=True)
        self.instructions = parse_disassembly(self.asm)
        cfg = (root/'artifacts/device.config').read_text()
        for s in ['CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE=0x4000',
                  'CONFIG_ESP32S3_ICACHE_ASSOCIATED_WAYS=8',
                  'CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE=32',
                  'CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=240000000', '# CONFIG_SMP is not set',
                  '# CONFIG_ESP_SPIRAM is not set']:
            require(s in cfg, 'A configuration differs: '+s)

    def symbol(self, name):
        name, separator, caller = name.partition('@')
        symbols = self.symbols[name]
        if separator:
            targets = {c['target'] for c in self.calls(self.code(caller)) if name in c['symbols']}
            symbols = [s for s in symbols if s['address'] in targets]
        require(len(symbols) == 1, 'missing or ambiguous symbol: '+name)
        return symbols[0]

    def read_word(self, address):
        s = next(s for s in self.sections if s['address'] <= address and
                 address + 4 <= s['address'] + s['size'])
        return int.from_bytes(s['data'][address-s['address']:address-s['address']+4], 'little')

    def code(self, name):
        symbol = self.symbol(name)
        return [i for i in self.instructions[symbol['address']] if
                not symbol['size'] or i['address'] < symbol['address'] + symbol['size']]

    def literal(self, i):
        if i['op'] != 'l32r':
            return None
        target = TARGET.search(i['args'])
        require(target is not None, 'unresolved L32R slot')
        slot = int(target[1], 16)
        value = self.read_word(slot)
        return dict(slot=slot, value=value, symbols=sorted(self.exact.get(value, [])))

    def calls(self, code):
        result = []
        for n, i in enumerate(code):
            if not i['op'].startswith('call'):
                continue
            target = TARGET.search(i['args'])
            address, method = (int(target[1], 16), 'direct') if target else (None, 'unresolved register')
            if target is None and n and code[n-1]['op'] == 'l32r' and code[n-1]['args'].split(',')[0] == i['args']:
                address, method = self.literal(code[n-1])['value'], 'immediately preceding L32R'
            result.append(dict(pc=i['address'], op=i['op'], target=address,
                symbols=sorted(self.exact.get(address, [])), resolution=method))
        return result

    def canonical(self, code):
        result = []
        for i in code:
            literal = self.literal(i)
            if literal:
                value = '|'.join(literal['symbols']) if literal['symbols'] else hex(literal['value'])
                args = i['args'].split(',')[0]+', literal='+value
            else:
                args = TARGET.sub(lambda m: '<'+m[2]+'>', i['args'])
            result.append(i['op']+' '+args)
        return result

    def inventory(self, name):
        symbol, code = self.symbol(name), self.code(name)
        slots = [self.literal(i) for i in code if i['op'] == 'l32r']
        return dict(name=name, **symbol, instruction_count=len(code), calls=self.calls(code),
            literal_reads=slots, body_lines=sorted(line_addresses(symbol['address'], symbol['size'])),
            literal_lines=sorted({s['slot']//32*32 for s in slots}),
            frame_bytes=int(code[0]['args'].split(',')[1]) if code[0]['op'] == 'entry' else None)

    def path_inventory(self, name):
        code = walk_success(self.code(name), SUCCESS_BRANCHES[name])
        return dict(name=name, instructions=code, calls=self.calls(code),
            literal_reads=[self.literal(i) for i in code if i['op']=='l32r'],
            body_lines=sorted({a for i in code for a in line_addresses(i['address'], i['size'])}),
            literal_lines=sorted({self.literal(i)['slot']//32*32 for i in code if i['op']=='l32r'}))

    def bracket(self):
        code = self.code('main')
        calls = self.calls(code)
        send = next(c for c in calls if 'send_message$constprop$0' in c['symbols'])
        clocks = [c for c in calls if 'sys_clock_cycle_get_32' in c['symbols']]
        first = max(c['pc'] for c in clocks if c['pc'] < send['pc'])
        last = min(c['pc'] for c in clocks if c['pc'] > send['pc'])
        # Include the first clock's L32R as a conservative boundary envelope.
        start = next(n for n, i in enumerate(code) if i['address'] == first)-1
        selected = [i for i in code[start:] if i['address'] <= last]
        require(len(self.calls(selected)) == 3, 'unexpected Sample clock/send bracket')
        return selected


def footprint(items):
    body = {a for i in items for a in i['body_lines'] if flash(a)}
    literal = {a for i in items for a in i['literal_lines'] if flash(a)}
    lines = body | literal
    sets = collections.Counter((a//32) % 64 for a in lines)
    return dict(body_lines=sorted(body), literal_lines=sorted(literal), lines=sorted(lines),
                total_lines=len(lines), set_counts=dict(sorted(sets.items())), max_set_lines=max(sets.values(), default=0))


def analyze(baseline, objdump, out):
    images, result = {}, {}
    for mode in ['off', 'on']:
        image = images[mode] = Image(baseline/f'A-{mode}', objdump)
        inv = {n:image.inventory(n) for n in API}
        paths = {n:image.path_inventory(n) for n in API}
        bracket = image.bracket()
        bracket_item = dict(name='main-clock-bracket', body_lines=sorted({a for i in bracket
            for a in line_addresses(i['address'], i['size'])}), literal_lines=sorted({image.literal(i)['slot']//32*32
            for i in bracket if i['op'] == 'l32r'}))
        groups = {'api_envelope': footprint([*inv.values(), bracket_item]),
                  'api_success': footprint([*paths.values(), bracket_item])}
        for group, names in GROUPS.items():
            # Exclude off stubs from the observer-only traffic groups.
            members = names if mode == 'on' or not group.startswith('observer_') else []
            inv.update({n:image.inventory(n) for n in members})
            groups[group] = footprint([inv[n] for n in members])
        candidates = []
        for address in groups['api_envelope']['lines']:
            index = (address//32) % 64
            occupants = sorted({a for g in ['api_envelope','owner_progress','usb_dispatch','observer_periodic']
                                for a in groups[g]['lines'] if (a//32) % 64 == index})
            candidates.append(dict(address=address, conventional_set=index, distinct_collected_lines=len(occupants),
                functions=[dict(name=n, relation='same line' if address in i['body_lines']+i['literal_lines'] else 'other tag',
                    lines=[a for a in i['body_lines']+i['literal_lines'] if flash(a) and (a//32)%64 == index])
                    for n,i in {**inv, 'main-clock-bracket':bracket_item}.items() if n not in GROUPS['observer_boundary'] and
                    any(flash(a) and (a//32)%64 == index for a in i['body_lines']+i['literal_lines'])]))
        windows = {}
        for n in ['_WindowOverflow4','_WindowUnderflow4','_WindowOverflow8','_WindowUnderflow8',
                  '_WindowOverflow12','_WindowUnderflow12','sys_clock_cycle_get_32']:
            code = image.code(n)
            if n.startswith('_Window'):
                end = next(k for k,i in enumerate(code) if i['op'] in ['rfwo','rfwu'])
                code = code[:end+1]
            section = next(s for s in image.sections if s['address'] <= code[0]['address'] < s['address']+s['size'])
            require(section['name'].startswith('.iram0.') and all(
                section['address'] <= i['address'] < section['address']+section['size'] for i in code),
                'window/clock unexpectedly outside IRAM sections')
            windows[n] = dict(address=code[0]['address'], section=section['name'], instructions=code)
        result[mode] = dict(build_id=image.receipt['build_id'], elf_receipt=image.receipt['artifacts']['artifacts/firmware.elf'],
            groups=groups, functions=inv, success_paths=paths, main_bracket=bracket, main_bracket_calls=image.calls(bracket),
            business_instructions=sum(len(p['instructions'])*(4 if n=='wlc_put32' else 1) for n,p in paths.items()),
            business_calls=1+sum(len(p['calls']) for p in paths.values()),
            api_candidates=sorted(candidates,key=lambda c:(-c['distinct_collected_lines'],c['address'])),
            iram_windows_and_clock=windows)
        (out/f'A-{mode}.asm').write_text(image.asm)
    comparisons = {}
    for name in [*API, 'main']:
        code = {m:images[m].canonical(images[m].code(name)) for m in images}
        comparisons[name] = dict(normalized_equal=code['off']==code['on'],
            changes=list(difflib.unified_diff(code['off'],code['on'],fromfile='off',tofile='on',lineterm='')))
    comparisons['main-clock-bracket'] = dict(normalized_equal=
        images['off'].canonical(images['off'].bracket()) == images['on'].canonical(images['on'].bracket()))
    iram_equal = {n:result['off']['iram_windows_and_clock'][n]['instructions'] ==
                     result['on']['iram_windows_and_clock'][n]['instructions']
                  for n in result['off']['iram_windows_and_clock'] if n.startswith('_Window')}
    callback_bindings = {}
    for mode,image in images.items():
        loads = result[mode]['success_paths']['usb_kernel_send_sample_view']['literal_reads']
        require(len(loads)==1 and loads[0]['value']==image.symbol('usb_kernel_encode_sample_view')['address'],
                'Sample callback binding changed')
        callback_bindings[mode] = dict(literal=loads[0],
            argument_path='generated send a13 -> wl_kernel_send a5/a14 -> encode_frame a6 -> callx8 a6',
            basis='Reviewed A assembly argument moves and windowed CALL8 ABI, not generic indirect-call resolution')
    return dict(format='wirelink-A-static-residency-v1', images=result, code_comparisons=comparisons,
        window_vectors_exact_equal=iram_equal, callback_bindings=callback_bindings,
        model='32B line envelopes, all branches within selected function bodies, plus referenced L32R slots. Curated contexts, not a transitive or dynamic trace. Conventional low-bit indexing only: 16384/(8*32)=64 sets, set=(VA//32)%64, same-set period2048. TRM consulted does not specify index-bit selection/replacement policy; pressure counts are conditional candidate screening, not hardware hit/miss predictions. No ROM/IRAM or DROM data counted as ICache lines. Boundary-only observers excluded from periodic candidate union.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--objdump', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    result = analyze(args.baseline, args.objdump, args.out)
    (args.out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print('Inventoried retained A-off/on; no firmware built or flashed')
