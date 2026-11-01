# SPDX-License-Identifier: Apache-2.0
"""Decode the S3 ROM's configured 16KiB/8-way/32B mapping offline; no target I/O."""
import argparse
import json
from pathlib import Path
import re
from analyze import parse_disassembly, require

BASE = 0x42000000
SIZE = 16384
WAYS = 8
LINE = 32
SETS = SIZE // WAYS // LINE
SPAN = SIZE // WAYS
TAG_BASE = 0x600c6000


def locate(address):
    require(type(address) is int and BASE <= address < BASE + 0x2000000, 'not an IROM address')
    offset = address - BASE
    return dict(line=address//LINE*LINE, set_index=(offset % SPAN)//LINE,
                tag=offset//SPAN, byte_offset=offset % LINE)


def legacy_tag_address(set_index, way):
    require(type(set_index) is int and 0 <= set_index < SETS, 'set index')
    require(type(way) is int and 0 <= way < WAYS, 'way')
    # Cache_Travel_Tag_Memory, 16KiB ICache, eight-way branch, one bank:
    # 0x4004fa1e..27 and 0x4004fa08..16; 64 entries/way, 4B per entry.
    return TAG_BASE + (way*SETS + set_index)*4


def decode_tag(raw, set_index):
    require(type(raw) is int and 0 <= raw < 2**32, 'raw tag word')
    legacy_tag_address(set_index, 0)
    # SDK esp32s3/rom/cache.h icache_tag_item layout, not a replacement-policy model.
    valid = bool(raw & 1)
    tag = (raw >> 9) & 0x3fff
    return dict(valid=valid, locked=bool(raw & 2), attributes=(raw >> 2) & 15,
                fifo_field=(raw >> 6) & 7, tag=tag,
                line_address=BASE + tag*SPAN + set_index*LINE if valid else None)


def interpret_virtual_address(code, tag, offset):
    """Execute just the published ROM address helper's integer/data-load subset.

    Inputs represent post-ENTRY ABI arguments. No calls, cache access, target
    execution or general Xtensa emulation. Unexpected instructions are rejected.
    """
    mode_address = 0x1000
    mode = SIZE.to_bytes(4,'little') + LINE.to_bytes(2,'little') + bytes([WAYS,1])
    regs = {f'a{i}':0 for i in range(16)}
    regs.update(a2=mode_address, a3=tag, a4=offset)
    instructions = {i['address']:i for i in code}
    pc, visited = code[0]['address'], set()
    while True:
        require(pc in instructions and pc not in visited, 'ROM helper flow')
        visited.add(pc)
        i = instructions[pc]
        op = i['op']
        args = [v.strip() for v in i['args'].split(',')]
        next_pc = pc + i['size']
        if op == 'entry':
            pass
        elif op in ['l8ui','l32i.n']:
            count = 1 if op == 'l8ui' else 4
            address = regs[args[1]] + int(args[2],0) - mode_address
            require(0 <= address and address+count <= len(mode), 'ROM helper mode load')
            regs[args[0]] = int.from_bytes(mode[address:address+count],'little')
        elif op == 'l32r':
            value = re.search(r'\(([0-9a-f]+) <',i['args'])
            require(value is not None, 'ROM literal referent')
            regs[args[0]] = int(value[1],16)
        elif op in ['quou','mull','and','add.n']:
            a,b = regs[args[1]],regs[args[2]]
            if op == 'quou':value = a//b
            elif op == 'mull':value = a*b
            elif op == 'and':value = a&b
            else:value = a+b
            regs[args[0]] = value & 0xffffffff
        elif op == 'beqz.n':
            target = re.search(r'([0-9a-f]{8}) <',args[1])
            require(target is not None, 'ROM branch target')
            if regs[args[0]] == 0:next_pc = int(target[1],16)
        elif op == 'retw.n':
            return regs['a2']
        else:
            raise ValueError('unsupported ROM helper instruction: '+op)
        pc = next_pc


def analyze(inventory, rom_text):
    require(inventory.get('format') == 'wirelink-A-static-residency-v1', 'A inventory format')
    require(set(inventory['images']) == {'off','on'} and
            all(image['api_candidates'] for image in inventory['images'].values()), 'A inventory groups/candidates')
    functions = parse_disassembly(rom_text)
    require(list(functions) == [0x4004fd90], 'expected published S3 Cache_Get_Virtual_Addr body')
    code = functions[0x4004fd90]
    # Every set, offset endpoints, and low/high address tags. This validates the
    # inversion against ROM instructions, separately from the Python formula.
    checked = 0
    for tag in [0,1,0x18,0x3fff]:
        for index in range(SETS):
            for byte in [0,31]:
                offset = index*LINE+byte
                actual = interpret_virtual_address(code,tag,offset)
                require(actual == BASE+tag*SPAN+offset, 'ROM reconstruction mismatch')
                require(locate(actual) == dict(line=actual//LINE*LINE,set_index=index,tag=tag,byte_offset=byte),
                        'ROM mapping inversion mismatch')
                checked += 1
    candidates = {}
    for mode,image in inventory['images'].items():
        rows = []
        for c in image['api_candidates']:
            loc = locate(c['address'])
            require(loc['set_index'] == c['conventional_set'], 'previous candidate bucket differs from ROM mapping')
            rows.append(dict(address=c['address'], **loc,
                legacy_tag_addresses=[legacy_tag_address(loc['set_index'],w) for w in range(WAYS)],
                distinct_collected_lines=c['distinct_collected_lines']))
        candidates[mode] = rows
    return dict(format='wirelink-S3-ROM-tag-mapping-v1', configured_mode=dict(size=SIZE,ways=WAYS,line_size=LINE,sets=SETS),
        rom_helper_cases=checked, candidates=candidates, all_prior_buckets_match=True,
        basis='Official esp32s3_rev0_rom.elf release20260528, Get_Virtual_Addr and eight-way/16KiB Travel_Tag_Memory path; SDK icache_tag_item fields.',
        scope='Offline ROM/SDK mapping audit, not observed target tags, PC samples, cache contents, replacement-policy validation or miss measurements. Legacy aperture addresses are derived, not a live read command. Both ROM tag traversal APIs suspend cache; no target function was invoked.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inventory', type=Path, required=True)
    p.add_argument('--rom-asm', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    require(not a.out.exists(), 'refusing to overwrite output')
    result = analyze(json.loads(a.inventory.read_text()),a.rom_asm.read_text())
    with a.out.open('x') as stream:
        json.dump(result,stream,indent=2);stream.write('\n')
    print('Matched ROM mapping for all prior candidate lines;',result['rom_helper_cases'],'helper cases checked; no target access')
