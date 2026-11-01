# SPDX-License-Identifier: Apache-2.0
import unittest
from analyze import Image, footprint, line_addresses, parse_disassembly, walk_success


class AnalysisTests(unittest.TestCase):
    def test_line_edges_and_deduplication(self):
        self.assertEqual(line_addresses(0x4200001f, 2), {0x42000000, 0x42000020})
        self.assertEqual(line_addresses(0x42000020, 32), {0x42000020})
        self.assertEqual(line_addresses(0x42000020, 0), set())
        item = dict(body_lines=[0x42000000, 0x42000800, 0x40374080],
                    literal_lines=[0x42000000, 0x3c010000])
        f = footprint([item, item])
        self.assertEqual((f['total_lines'], f['max_set_lines']), (2, 2))
        self.assertEqual(f['set_counts'], {0: 2})

    def code(self):
        return parse_disassembly('''42000000 <example>:
42000000: 004136 entry a1, 32
42000003: 001256 bnez a2, 4200000b <example+0xb>
42000006: 000046 j 4200000e <example+0xe>
42000009: 00 .byte 00
4200000b: 000046 j 4200000e <example+0xe>
4200000e: f01d retw.n
''')[0x42000000]

    def test_annotated_path_and_unknown_branch_rejection(self):
        code = self.code()
        self.assertEqual([i['address'] for i in walk_success(code, {3:False})],
                         [0x42000000,0x42000003,0x42000006,0x4200000e])
        self.assertEqual([i['address'] for i in walk_success(code, {3:True})],
                         [0x42000000,0x42000003,0x4200000b,0x4200000e])
        with self.assertRaisesRegex(ValueError, 'unannotated'):
            walk_success(code, {})
        code[2]['args'] = '42000003 <example+3>'
        with self.assertRaisesRegex(ValueError, 'loops'):
            walk_success(code, {3:False})
        code = self.code()
        code[2]['args'] = '42000009 <example+9>'
        with self.assertRaisesRegex(ValueError, 'data/padding'):
            walk_success(code, {3:False})

    def image(self):
        image = object.__new__(Image)
        image.sections = [dict(address=0x42000800, size=4, data=bytes.fromhex('00000040'))]
        image.exact = {0x40000000: ['rom_helper']}
        return image

    def test_literal_slot_is_not_referent_and_call_resolution_is_bounded(self):
        image = self.image()
        code = parse_disassembly('''42000000 <example>:
42000000: 000081 l32r a8, 42000800 <pool>
42000003: 0008e0 callx8 a8
42000006: 0009e0 callx8 a9
''')[0x42000000]
        self.assertEqual(image.literal(code[0]),
                         dict(slot=0x42000800,value=0x40000000,symbols=['rom_helper']))
        calls = image.calls(code)
        self.assertEqual(calls[0]['target'],0x40000000)
        self.assertIsNone(calls[1]['target'])
        self.assertIn('literal=rom_helper',image.canonical(code)[0])

    def test_duplicate_local_symbol_requires_a_caller(self):
        image = self.image()
        image.symbols = {'helper':[dict(address=0x42000000,size=3),dict(address=0x42000100,size=3)],
                         'caller':[dict(address=0x42000200,size=3)]}
        image.exact[0x42000100]=['helper']
        image.instructions = parse_disassembly('''42000200 <caller>:
42000200: 000025 call8 42000100 <helper>
''')
        with self.assertRaisesRegex(ValueError,'ambiguous'):
            image.symbol('helper')
        self.assertEqual(image.symbol('helper@caller')['address'],0x42000100)


if __name__ == '__main__':
    unittest.main()
