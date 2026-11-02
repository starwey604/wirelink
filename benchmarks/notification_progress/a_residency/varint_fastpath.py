# SPDX-License-Identifier: Apache-2.0
"""Check a source-copy varint prototype without changing WLC or firmware."""
import argparse
import difflib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile

from analyze import parse_disassembly, require
from inspect_receive import PLANS, walk_annotated
from inspect_stream import instruction_model, read_image

FAST = '''  if (*at < length) {
    uint8_t first = in[*at];
    if (first < 128U) {
      ++*at;
      *out = first;
      return WL_CODEC_OK;
    }
  }
'''


def definition(source, name):
    pattern = r'^static (?:inline )?[^;{}]+\b' + re.escape(name) + r'\([^;{}]*\)\s*\{.*?^\}'
    matches = re.findall(pattern, source, re.MULTILINE | re.DOTALL)
    require(len(matches) == 1, 'missing/ambiguous frozen function: ' + name)
    return matches[0]


def prototype(source):
    original = definition(source, 'wlc_getv')
    anchor = '  size_t start = *at, n = 0U;'
    require(original.count(anchor) == 1, 'frozen reader boundary changed')
    candidate = original.replace(anchor, FAST + anchor)
    require(source.count(original) == 1, 'reader definition not unique')
    return source.replace(original, candidate)


def reference(source):
    # Clone only functions that consume getv. Shared clear/lookup/fixed reads
    # are unchanged and retain their original implementation/descriptor types.
    names = ['wlc_getv', 'wlc_skip', 'wlc_read_value', 'wlc_decode']
    text = '\n\n'.join(definition(source, name) for name in names)
    for name in names:
        text = re.sub(r'\b' + name + r'\b', name.replace('wlc_', 'reference_'), text)
    return text + '\n'


def command(args, **kwargs):
    result = subprocess.run(args, capture_output=True, text=True, **kwargs)
    require(result.returncode == 0, f'command failed: {shlex.join(args)}\n{result.stdout}\n{result.stderr}')
    return result.stdout


def normalized(code):
    # Compare operation/CFG independently of unresolved object literal slots.
    # The object's ROM-helper relocation is checked separately below.
    code = [dict(i, args=i['args'].split(',')[0] + ', 00000000 <slot> (0)')
            if i['op'] == 'l32r' else i for i in code]
    return instruction_model(code)[0]


def object_code(compiler_entry, source, obj, generated, out, label):
    args = shlex.split(compiler_entry['command'])
    args[args.index('-c') + 1] = str(source)
    args[args.index('-o') + 1] = str(obj)
    args.insert(1, '-I' + str(generated))
    command(args, cwd=compiler_entry['directory'])
    objdump = str(Path(args[0]).with_name(Path(args[0]).name.replace('-gcc', '-objdump')))
    assembly = command([objdump, '--adjust-vma=0x42000000', '-d', '-j', '.text.wlc_getv', str(obj)])
    relocations = command([objdump, '--adjust-vma=0x42000000', '-dr', '-j', '.text.wlc_getv', str(obj)])
    (out / (label + '-getv.asm')).write_text(assembly)
    (out / (label + '-getv-relocations.txt')).write_text(relocations)
    code = parse_disassembly(assembly)[0x42000000]
    with obj.open('rb') as f:
        elf = ELFFile(f)
        sections = {s.name: s.data() for s in elf.iter_sections()
                    if s.name.startswith(('.text.', '.literal.'))}
        body = elf.get_section_by_name('.text.wlc_getv').data_size
    require(re.findall(r'R_XTENSA_ASM_EXPAND\s+(\S+)', relocations) == ['__ashldi3'],
            'unexpected reader helper relocation')
    return dict(command=args, code=code, function_bytes=body,
                frame_bytes=int(code[0]['args'].split(',')[1]),
                sections=sections)


def path_row(code, choices):
    path = walk_annotated(code, choices, {})
    return dict(instructions=len(path), calls=sum(i['op'].startswith('call') for i in path), path=path)


def canonical_plan(bytes_):
    require(2 <= bytes_ <= 10, 'canonical survey width outside 2..10')
    # Encoding: 80 repeated (n-1) times, then 01, i.e. 1 << (7*(n-1)).
    # vsize's high word remains nonzero for max(n-5, 0) right shifts.
    high = max(bytes_ - 5, 0)
    choices = {15: [True] * min(bytes_, 9) + ([False] if bytes_ == 10 else []),
               38: [False] * (bytes_ - 1) + [True],
               49: [True] * high + [False] * (bytes_ - high),
               51: [True] * (bytes_ - high - 1) + [False],
               53: [False], 58: [False] * bytes_, 59: [False] * bytes_}
    if bytes_ == 10:
        choices[16] = [True]
    return choices


def candidate_fallback(choices, first_available=True):
    indices = {15: 20, 16: 21, 38: 41, 49: 53, 51: 55, 53: 57, 58: 67, 59: 68, 61: 70}
    result = {indices[k]: v for k, v in choices.items()}
    result[5] = [first_available]
    if first_available:
        result[10] = [True]  # First byte has its continuation bit set.
    return result


def path_survey(objects):
    paths = dict(single=dict(A=path_row(objects['A']['code'], PLANS['wlc_getv'][0]),
                             V=path_row(objects['V']['code'], {5: [True], 10: [False]}),
                             input='00..7f, at < length'))
    cases = {f'canonical_{n}': (canonical_plan(n), True, '80' * (n - 1) + '01')
             for n in range(2, 11)}
    cases.update(empty=({58: [True], 61: [False]}, False, 'at == length'),
                 beyond_end=({58: [True], 61: [True]}, False, 'at > length'),
                 truncated=({15: [True], 38: [False], 58: [False, True],
                             59: [False], 61: [False]}, True, '80, length=1'))
    for name, (choices, available, condition) in cases.items():
        paths[name] = dict(input=condition, A=path_row(objects['A']['code'], choices),
                           V=path_row(objects['V']['code'], candidate_fallback(choices, available)))
    return paths


def check(baseline, assembly, include, compilers, out):
    require(not out.exists(), 'refusing to overwrite evidence directory')
    out.mkdir(parents=True)
    generated = baseline / 'artifacts/device'
    source = (generated / 'usb.c').read_text()
    candidate = prototype(source)
    (out / 'reader.patch').write_text(''.join(difflib.unified_diff(
        source.splitlines(True), candidate.splitlines(True), fromfile='A/usb.c', tofile='V/usb.c')))
    result = dict(format='single-byte-varint-feasibility-v1', baseline=str(baseline),
                  prototype=FAST, host=[], status='incomplete')
    with tempfile.TemporaryDirectory(prefix='wirelink-varint-check-') as folder:
        root = Path(folder)
        (root / 'candidate.c').write_text(candidate)
        (root / 'reference.inc').write_text(reference(source))
        for compiler in compilers:
            args = [compiler, '-std=c11', '-Os', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
                    '-I' + str(root), '-I' + str(generated), '-I' + str(include),
                    str(Path(__file__).with_name('varint_contract.c')), '-o', str(root / 'contract')]
            command(args)
            result['host'].append(dict(compiler=compiler, command=args,
                                        counts=json.loads(command([str(root / 'contract')]))))
        require(all(run['counts'] == result['host'][0]['counts'] for run in result['host']),
                'compilers did not execute the same differential corpus')
        entries = json.loads((baseline / 'device/compile_commands.json').read_text())
        entry = next(e for e in entries if e['file'].endswith('/usb.c'))
        (root / 'original.c').write_text(source)
        objects = {label: object_code(entry, root / filename, root / (label + '.o'),
                                     generated, out, label)
                   for label, filename in [('A', 'original.c'), ('V', 'candidate.c')]}
        retained = read_image(assembly, baseline / 'artifacts/device-symbols.txt')['wlc_getv']
        require(normalized(objects['A']['code']) == normalized(retained['code']) and
                objects['A']['function_bytes'] == retained['bytes'],
                'fresh original reader differs from frozen product machine code')
        sections = sorted(objects['A']['sections'].keys() | objects['V']['sections'].keys())
        changes = [dict(name=name, A=len(objects['A']['sections'].get(name, b'')),
                        V=len(objects['V']['sections'].get(name, b''))) for name in sections
                   if objects['A']['sections'].get(name) != objects['V']['sections'].get(name)]
        result['object_sections'] = dict(compared=len(sections), changes=changes,
                                        text_delta=sum(c['V'] - c['A'] for c in changes if c['name'].startswith('.text.')))
        result['objects'] = {label: {k: v for k, v in obj.items() if k != 'sections'}
                             for label, obj in objects.items()}
        result['paths'] = path_survey(objects)
    result.update(status='complete', payloads_deleted=True, firmware_changed=False,
                  scope='Source-copy prototype, host differential tests, unlinked Xtensa objects. '
                  'No WLC edit, firmware link/flash, whole-image resource or runtime gain claim.')
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', type=Path, required=True, help='Original A-on directory')
    p.add_argument('--a-assembly', type=Path, required=True)
    p.add_argument('--include', type=Path, default=Path(__file__).resolve().parents[3] / 'include')
    p.add_argument('--cc', action='append', required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    check(a.baseline.resolve(), a.a_assembly.resolve(), a.include.resolve(), a.cc, a.out.resolve())
