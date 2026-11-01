# SPDX-License-Identifier: Apache-2.0
"""Inspect retained required-fixed A/B stream code and raw accounting offline."""
import argparse
import collections
import difflib
import importlib.util
import json
from pathlib import Path
import re

from analyze import HEADER, TARGET, parse_disassembly, require, walk_success


SYMBOL = re.compile(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+[tT]\s+(.+)$')
LITERAL = re.compile(r'\(([0-9a-f]+)(?: <([^>]+)>)?\)')


def instruction_model(code):
    """Keep op/register/immediate/CFG; record L32R referents separately.

    Unexecuted assembler alignment bytes do not become business instructions.
    Branch destinations use instruction ordinals so padding changes stay visible
    as layout changes rather than invented changes to the branch's meaning.
    """
    executable = [i for i in code if i['op'] != '.byte']
    index = {i['address']: n for n, i in enumerate(executable)}
    result, literals = [], []
    for i in executable:
        args = i['args']
        if i['op'] == 'l32r':
            match = LITERAL.search(args)
            require(match is not None, 'missing literal annotation')
            value = int(match[1], 16)
            # Objdump's nearest-symbol annotation for an integer is not a
            # pointer identity (e.g. 0x7fffffff gets an unrelated symbol+offset).
            memory = 0x3c000000 <= value < 0x44000000
            referent = match[2] if memory and match[2] else hex(value)
            literals.append(dict(instruction=len(result), value=value, referent=referent))
            args = args.split(',')[0] + ', <literal>'
        elif i['op'] == 'j' or i['op'].startswith(('b', 'loop')):
            match = TARGET.search(args)
            if match:
                target = int(match[1], 16)
                if code[0]['address'] <= target <= code[-1]['address']:
                    require(target in index, 'branch enters padding or missing instruction')
                    args = TARGET.sub('<instruction ' + str(index[target]) + '>', args)
                else:
                    args = TARGET.sub(lambda m: '<' + m[2] + '>', args)
        else:
            args = TARGET.sub(lambda m: '<' + m[2] + '>', args)
        result.append(i['op'] + ' ' + args)
    return result, literals


def read_image(assembly, symbols):
    text = assembly.read_text()
    functions = parse_disassembly(text)
    sizes = {}
    for line in symbols.read_text().splitlines():
        match = SYMBOL.match(line)
        if match:
            sizes[int(match[1], 16)] = int(match[2], 16)
    names = collections.defaultdict(list)
    for line in text.splitlines():
        match = HEADER.match(line)
        if match and int(match[1], 16) in sizes:
            names[match[2]].append(int(match[1], 16))
    result = {}
    for name, addresses in names.items():
        for n, address in enumerate(sorted(addresses), 1):
            key = name if len(addresses) == 1 else f'{name}#{n}'
            size = sizes[address]
            code = [i for i in functions[address] if i['address'] < address + size]
            model, literals = instruction_model(code)
            result[key] = dict(address=address, bytes=size, code=code, model=model,
                               literals=literals,
                               padding_bytes=sum(i['size'] for i in code if i['op'] == '.byte'))
    return result


def compare_images(a, b):
    comparisons = {}
    for name in sorted(a.keys() & b.keys()):
        x, y = a[name], b[name]
        references = [[(v['instruction'], v['referent']) for v in image['literals']]
                      for image in [x, y]]
        comparisons[name] = dict(
            operations_and_cfg_equal=x['model'] == y['model'],
            literal_referents_equal=references[0] == references[1],
            A={k: x[k] for k in ['address', 'bytes', 'padding_bytes', 'literals']},
            B={k: y[k] for k in ['address', 'bytes', 'padding_bytes', 'literals']},
            operation_diff=list(difflib.unified_diff(
                x['model'], y['model'], fromfile='A', tofile='B', lineterm='')))
    return dict(functions=comparisons, only_A=sorted(a.keys() - b.keys()),
                only_B=sorted(b.keys() - a.keys()))


def progress_work(image):
    shape = image['wlc_scalar_shape_2_encode']['code']
    choices = {i['address'] - shape[0]['address']: False
               for i in shape if i['op'].startswith('b')}
    path = walk_success(shape, choices)
    instructions, calls = len(path), []
    for i in path:
        if i['op'].startswith('call'):
            match = TARGET.search(i['args'])
            require(match is not None, 'unresolved Progress helper')
            name = match[2]
            require(name in ['wlc_scalar_shape_2_wlc_detail_measure', 'wlc_put32'],
                    'unreviewed Progress helper: ' + name)
            code = image[name]['code']
            success = walk_success(code, {j['address'] - code[0]['address']: False
                                         for j in code if j['op'].startswith('b')})
            require(not any(j['op'].startswith('call') for j in success), 'nested helper')
            instructions += len(success)
            calls.append(dict(name=name, instructions=len(success), path=success))
    return dict(shape_bytes=image['wlc_scalar_shape_2_encode']['bytes'],
                shape_success_instructions=len(path), instructions_with_helpers=instructions,
                calls=len(calls), path=path, helpers=calls,
                annotation='Required sequence/bytes/flags, valid pointers, capacity >=19; '
                'conditional error/absent branches untaken. Not IRQ/window execution.')


def varuint_size(value):
    return max(1, (value.bit_length() + 6) // 7)


def control_request_size(arguments):
    # Frozen managed RPC: native prefix4 + session8 + operation3. Operations
    # exceed8191 after1000 warmup/10000 RPC and stay below1048576 in this screen.
    # All six tagged uint32 fields are present, including zero arguments. The
    # command ID starts at1e9 and remains five varuint bytes throughout.
    return 15 + sum(1 + varuint_size(v) for v in [1000000000, *arguments])


def control_response_size(row):
    # Prefix/metadata15, request_id6, status2, bytes-key1, varlength and raw JSON.
    payload = json.dumps(row['device'], separators=(',', ':')).encode()
    return 24 + varuint_size(len(payload)) + len(payload)


def successful_progress(begin, end, stream, device, cpu):
    """Solve completed USB byte ledgers for successful Progress frame counts.

    Scope is start+1s preparation+5s window+drain+STOP/CPU export, NOT the CPU
    window alone. Counts exclude the single host DRAIN and device END markers.
    No failed admission attempts/service/wake counts can be recovered this way.
    """
    require(stream['size'] == 32 and stream['window_ns'] == 5000000000,
            'only registered 32B/5s screen supported')
    requests = (control_request_size([2, 5, stream['direction'], 32, 5000])
                + control_request_size([3, stream['sent'], stream['received'], 0, 0])
                + control_request_size([7, 0, 0, 0, 0]))
    responses = 25 + control_response_size(device) + control_response_size(cpu)
    residuals = dict(
        host=end['usb_tx'] - begin['usb_tx'] - requests - stream['sent'] * 43 - 23,
        device=end['usb_rx'] - begin['usb_rx'] - responses - stream['received'] * 43 - 23)
    require(all(n >= 0 and n % 23 == 0 for n in residuals.values()),
            'USB bytes do not close into whole Progress frames')
    counts = {k: v // 23 for k, v in residuals.items()}
    return dict(normal_frames=counts, control_request_bytes=requests,
                control_response_bytes=responses,
                host_chunks_per_normal_progress=stream['received'] / counts['host']
                if counts['host'] else None,
                device_chunks_per_normal_progress=stream['sent'] / counts['device']
                if counts['device'] else None)


def stream_components(metrics):
    rate = (metrics['h2d_bytes_s'] + metrics['d2h_bytes_s']) / metrics['size']
    result = dict(payload_frames_s=rate,
                  host_cpu_us_per_equivalent_frame=metrics['host_window_cpu_ns']
                  / metrics['actual_host_window_ns'] * 1e6 / rate)
    cpu = metrics['device_cpu']
    if cpu['available']:
        wall = cpu['wall_cycles'] / cpu['frequency_hz']
        equivalent_frames = rate * wall
        fields = ['active_cycles', 'owner_cycles_inclusive_irq', 'usbd_cycles_inclusive_irq',
                  'udc_cycles_inclusive_irq', 'usb_irq_span_cycles_inclusive']
        result.update(
            cpu_window_seconds=wall,
            mcu_active_percent=100 * cpu['active_cycles'] / cpu['wall_cycles'],
            usb_irqs=cpu['usb_irqs'], usb_irqs_per_equivalent_frame=cpu['usb_irqs'] / equivalent_frames,
            us_per_equivalent_frame={k: cpu[k] / cpu['frequency_hz'] * 1e6 / equivalent_frames
                                     for k in fields})
    return result


def inspect(evidence, baseline, a_disassembly):
    # Reuse the mature raw/qualification/state replay; no firmware is needed.
    path = Path(__file__).parents[1] / 'fixed_encode/analyze.py'
    spec = importlib.util.spec_from_file_location('required_fixed_replay', path)
    replay = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(replay)
    screen = replay.replay(evidence)
    machine = {}
    for mode in ['off', 'on']:
        a = read_image(a_disassembly / f'A-{mode}.asm',
                       baseline / f'A-{mode}/artifacts/device-symbols.txt')
        b = read_image(evidence / f'B-{mode}.asm',
                       evidence / f'text-build-evidence/B-{mode}/symbols.txt')
        machine[mode] = compare_images(a, b)
        machine[mode]['progress'] = {g: progress_work(image) for g, image in [('A', a), ('B', b)]}
    observations = []
    for capture in screen['captures']:
        rows = []
        raw = (evidence / 'captures' / capture['name'] / 'host.jsonl').read_text()
        for line in raw.splitlines():
            row = json.loads(line)
            if row['event'] in ['stream_device', 'stream_cpu']:
                require(json.dumps(row, separators=(',', ':')) == line,
                        'response JSON spelling differs from byte-accounting model')
            rows.append(row)
        streams = iter(capture['metrics']['streams'])
        for row in rows:
            event = row['event']
            if event == 'host_health' and row['stage'] == 'stream_begin':
                begin = row
            elif event == 'stream_device':
                device = row
            elif event == 'stream_cpu':
                cpu = row
            elif event == 'host_health' and row['stage'] == 'stream_end':
                end = row
            elif event == 'stream':
                metrics = next(streams)
                observations.append(dict(
                    capture=capture['name'], group=capture['group'], observer=capture['observer'],
                    round=capture['round'], pair=capture['pair'],
                    eligible=screen['pairs'][capture['pair']]['valid'], direction=row['direction'],
                    successful_progress=successful_progress(begin, end, row, device, cpu),
                    components=stream_components(metrics)))
    require(len(observations) == 36, 'stream inventory incomplete')
    return dict(format='required-fixed-stream-inspection-v1', machine_code=machine,
                observations=observations, pairs=screen['pairs'],
                inputs=dict(evidence=str(evidence), baseline=str(baseline),
                            a_disassembly=str(a_disassembly)),
                accounting='Inclusive owner/thread/IRQ spans overlap; never add or subtract them '
                'into exclusive costs. Window CPU divided by concurrent goodput gives equivalent '
                'frame cost, not per-frame tracing. Successful Progress counts cover whole phases, '
                'not window-only attempts. Rejected state pairs remain descriptive only.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--a-disassembly', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    require(not args.out.exists(), 'refusing to overwrite inspection')
    result = inspect(args.evidence.resolve(), args.baseline.resolve(), args.a_disassembly.resolve())
    with args.out.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    for mode, machine in result['machine_code'].items():
        changed = [n for n, v in machine['functions'].items() if not v['operations_and_cfg_equal']]
        print(mode, 'operation/CFG changes:', ', '.join(changed))
    print('Inspected', len(result['observations']), 'stream observations; no hardware access')
