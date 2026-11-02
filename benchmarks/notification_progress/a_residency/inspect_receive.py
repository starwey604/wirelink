# SPDX-License-Identifier: Apache-2.0
"""Annotated original A/B H2D decode paths; no target execution or rebuild."""
import argparse
import collections
import json
from pathlib import Path
import re
import subprocess
import tempfile

from analyze import TARGET, line_addresses, require
from inspect_stream import compare_images, read_image


# Instruction ordinals exclude assembler padding. Every visited conditional
# has an explicit outcome per visit; these are reviewed paths, not emulation.
PLANS = {
    'stream_chunk_decode': ({}, {}),
    'wlc_decode': ({
        4: [False], 7: [False], 8: [False], 10: [False], 18: [False],
        31: [False, False], 41: [False, False], 43: [False, False],
        45: [False, False], 48: [False, False], 50: [True, True],
        65: [False, False], 68: [False, False], 72: [False, False],
        76: [False, False], 83: [False, False], 85: [False, False],
        99: [False, False, True], 108: [False, False],
        112: [False, False], 114: [True, True, False]}, {}),
    'wlc_clear': ({12: [True, True], 21: [False, False], 22: [False, True],
                   23: [True], 37: [True, True, False]}, {}),
    'wlc_getv': ({15: [True], 38: [True], 49: [False], 51: [False],
                  53: [False], 58: [False], 59: [False]}, {}),
    'wlc_read_value:F32': ({4: [False], 6: [True], 11: [False], 16: [True],
                           32: [False]}, {20: 4}),
    'wlc_read_value:BYTES': ({4: [True], 8: [True], 44: [False], 50: [False],
                             52: [False], 53: [False], 54: [False],
                             57: [False], 58: [False], 59: [False]}, {}),
}
RECEIVE_CHAIN = [
    'receive#2', 'wl_kernel_native_none_read', 'usb_kernel_route',
    'stream_chunk_decode', 'wlc_decode', 'wlc_clear', 'wlc_getv',
    'wlc_read_value', 'wlc_find_field', 'wlc_skip', 'on_chunk',
    'receive#1', 'ub_verify_chunk',
]


def walk_annotated(code, choices, loops, limit=2000):
    """Bounded finite paths, including a counted Xtensa LOOP (exclusive end).

    Calls are counted as instructions, not followed. A caller must compose its
    reviewed callee paths explicitly. Nested/re-entered hardware loops and
    unconsumed annotations are rejected rather than guessed.
    """
    executable = [i for i in code if i['op'] != '.byte']
    by_address = {i['address']: (n, i) for n, i in enumerate(executable)}
    pc, result, visits, used_loops, active = executable[0]['address'], [], collections.Counter(), set(), None
    for _ in range(limit):
        if active and pc == active[1]:
            begin, end, remaining = active
            active = (begin, end, remaining - 1) if remaining > 1 else None
            if active:
                pc = begin
        require(pc in by_address, 'path leaves instructions or enters padding')
        n, i = by_address[pc]
        require(i['op'] not in ['ill', '.word'], 'path enters data')
        result.append(i)
        op = i['op']
        if op.startswith('retw'):
            require(active is None, 'return inside hardware loop')
            require(all(visits[k] == len(v) for k, v in choices.items()),
                    'unused branch annotations')
            require(used_loops == set(loops), 'unused loop annotations')
            return result
        taken = op == 'j'
        if op.startswith('b'):
            require(n in choices and visits[n] < len(choices[n]),
                    f'unannotated branch visit at instruction {n}')
            taken = choices[n][visits[n]]
            require(type(taken) is bool, 'branch outcome must be boolean')
            visits[n] += 1
        if taken or op.startswith('loop'):
            match = TARGET.search(i['args'])
            require(match is not None, 'unresolved branch/loop')
            target = int(match[1], 16)
            require(target in by_address, 'branch/loop end enters padding or leaves function')
            if op.startswith('loop'):
                require(op == 'loop' and n in loops and n not in used_loops and active is None,
                        'unreviewed/nested/re-entered hardware loop')
                count = loops[n]
                require(type(count) is int and count > 0 and target > pc + i['size'],
                        'invalid hardware loop annotation')
                used_loops.add(n)
                active = (pc + i['size'], target, count)
            else:
                pc = target
                continue
        pc += i['size']
    raise ValueError('path exceeded instruction limit')


def path_calls(path):
    calls = []
    for n, i in enumerate(path):
        if not i['op'].startswith('call'):
            continue
        match = TARGET.search(i['args']) if i['op'] != 'callx8' else None
        if match:
            name, address = match[2], int(match[1], 16)
        else:
            require(n > 0 and path[n - 1]['op'] == 'l32r' and
                    path[n - 1]['args'].split(',')[0] == i['args'],
                    'unresolved path call')
            literal = re.search(r'\(([0-9a-f]+) <([^>]+)>\)', path[n - 1]['args'])
            require(literal is not None, 'unresolved indirect call literal')
            address, name = int(literal[1], 16), literal[2]
        calls.append(dict(pc=i['address'], name=name, address=address, op=i['op']))
    return calls


def receive_work(image):
    paths = {}
    for name, (branches, loops) in PLANS.items():
        function = name.split(':')[0]
        path = walk_annotated(image[function]['code'], branches, loops)
        lines = set().union(*(line_addresses(i['address'], i['size']) for i in path))
        slots = []
        for i in path:
            if i['op'] == 'l32r':
                match = TARGET.search(i['args'])
                require(match is not None, 'unresolved literal slot')
                slots.append(int(match[1], 16))
        paths[name] = dict(instructions=len(path), calls=path_calls(path), path=path,
                           instruction_lines=sorted(lines), literal_slots=slots)
    multiplicity = dict(stream_chunk_decode=1, wlc_decode=1, wlc_clear=1, wlc_getv=3,
                        **{'wlc_read_value:F32': 1, 'wlc_read_value:BYTES': 1})
    call_counts = collections.Counter()
    for name, count in multiplicity.items():
        call_counts.update({k: v * count for k, v in
                            collections.Counter(c['name'] for c in paths[name]['calls']).items()})
    require(call_counts == collections.Counter(wlc_decode=1, wlc_clear=1,
            wlc_getv=3, wlc_read_value=2, __ashldi3=3), 'unexpected decode call inventory')
    instruction_lines = set().union(*(set(p['instruction_lines']) for p in paths.values()))
    literal_lines = {slot // 32 * 32 for p in paths.values() for slot in p['literal_slots']}
    return dict(paths=paths, multiplicity=multiplicity, calls=dict(call_counts),
                instructions_excluding_rom=sum(paths[k]['instructions'] * v for k, v in multiplicity.items()),
                collected_lines=dict(instructions=sorted(instruction_lines), literals=sorted(literal_lines),
                                     union_count=len(instruction_lines | literal_lines)),
                scope='One successful canonical 39-byte StreamChunk body (32-byte payload). '
                'Includes wrapper/callee CALL and RETURN instructions, three ROM call sites; '
                'excludes ROM bodies, route/backend/core/IRQ/window work. Not cycles or a trace.')


def source_census(generated, include, compilers):
    source = (generated / 'usb.c').read_text()
    fields = ['clear', 'getv', 'read_value', 'find_field', 'skip']
    for name in fields:
        pattern = r'(static (?:inline )?[^;{}]+\bwlc_' + name + r'\([^;{}]*\)\s*\{)'
        source, count = re.subn(pattern, r'\1\n  ++census_' + name + ';', source)
        require(count == 1, 'source instrumentation boundary: ' + name)
    marker = 'uint8_t b = in[(*at)++];'
    require(source.count(marker) == 1, 'varint byte instrumentation boundary')
    source = source.replace(marker, marker + '\n    ++census_varint_bytes;')
    source = 'static unsigned ' + ', '.join('census_' + k for k in fields + ['varint_bytes']) + ';\n' + source
    runs = []
    with tempfile.TemporaryDirectory(prefix='wirelink-receive-census-') as folder:
        root = Path(folder)
        (root / 'instrumented.c').write_text(source)
        harness = Path(__file__).with_name('receive_census.c')
        for compiler in compilers:
            command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                       '-I' + str(root), '-I' + str(generated), '-I' + str(include),
                       str(harness), '-o', str(root / 'census')]
            subprocess.run(command, check=True, capture_output=True, text=True)
            output = subprocess.check_output([str(root / 'census')], text=True)
            rows = [json.loads(line) for line in output.splitlines()]
            require([r['case'] for r in rows] == ['canonical32', 'reordered32', 'unknown32', 'canonical128'],
                    'source census incomplete')
            expected = [(3, 0, 0, 3), (3, 2, 0, 3), (5, 1, 1, 5), (3, 0, 0, 4)]
            for row, values in zip(rows, expected):
                require(row['clear'] == 1 and row['read_value'] == 2 and
                        tuple(row[k] for k in ['getv', 'find_field', 'skip', 'varint_bytes']) == values,
                        'source census differs: ' + row['case'])
            runs.append(dict(compiler=compiler, command=command, rows=rows))
    return runs


def descriptor_bindings(baseline, evidence):
    generated = baseline / 'A-on/artifacts/device'
    candidate = evidence / 'generated-candidate'
    definitions = {}
    for name in ['stream_chunk_fields', 'stream_chunk_desc']:
        pattern = r'static const [^;{}]+\b' + name + r'(?:\[\])? = \{.*?\n?\};'
        matches = [re.findall(pattern, (p / 'usb.c').read_text(), re.DOTALL)
                   for p in [generated, candidate]]
        require(len(matches[0]) == 1 and matches[0] == matches[1], 'descriptor source changed')
        definitions[name] = matches[0][0]
    require(all((generated / name).read_text() == (candidate / name).read_text()
                for name in ['usb.h', 'usb_values.h']), 'generated value layout changed')
    symbols = {}
    for mode in ['off', 'on']:
        for group, path in [('A', baseline / f'A-{mode}/artifacts/device-symbols.txt'),
                            ('B', evidence / f'text-build-evidence/B-{mode}/symbols.txt')]:
            found = {}
            for line in path.read_text().splitlines():
                parts = line.split()
                if len(parts) == 4 and parts[3] in definitions:
                    found[parts[3]] = dict(address=int(parts[0], 16), bytes=int(parts[1], 16),
                                           type=parts[2])
            require(set(found) == set(definitions), 'descriptor symbols incomplete')
            symbols[f'{group}-{mode}'] = found
    require(all(s == symbols['A-on'] for s in symbols.values()), 'descriptor placement changed')
    return dict(definitions=definitions, symbols=symbols,
                evidence='Generated definitions and value headers match; retained nm tables '
                'show identical addresses/sizes. Deleted B ELF data bytes are not reread.')


def inspect(baseline, evidence, assembly, include, compilers):
    machine = {}
    for mode in ['off', 'on']:
        a = read_image(assembly / f'A-{mode}.asm', baseline / f'A-{mode}/artifacts/device-symbols.txt')
        b = read_image(evidence / f'B-{mode}.asm', evidence / f'text-build-evidence/B-{mode}/symbols.txt')
        comparisons = compare_images({k: a[k] for k in RECEIVE_CHAIN}, {k: b[k] for k in RECEIVE_CHAIN})
        require(all(f['operations_and_cfg_equal'] and f['literal_referents_equal']
                    for f in comparisons['functions'].values()), 'receive chain changed')
        machine[mode] = dict(comparisons=comparisons, A=receive_work(a), B=receive_work(b))
    return dict(format='h2d-receive-inspection-v1', machine=machine,
                descriptors=descriptor_bindings(baseline, evidence),
                source_census=source_census(baseline / 'A-on/artifacts/device', include, compilers),
                inputs=dict(baseline=str(baseline), required_fixed=str(evidence), assembly=str(assembly)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--required-fixed', type=Path, required=True)
    parser.add_argument('--a-assembly', type=Path, required=True)
    parser.add_argument('--include', type=Path, default=Path(__file__).resolve().parents[3] / 'include')
    parser.add_argument('--cc', action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.baseline, args.required_fixed, args.a_assembly, args.include, args.cc)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
