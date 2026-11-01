# SPDX-License-Identifier: Apache-2.0
import unittest
from analyze import parse_disassembly
from tag_mapping import analyze, decode_tag, interpret_virtual_address, legacy_tag_address, locate


class TagMappingTests(unittest.TestCase):
    def test_candidate_group_and_address_boundaries(self):
        for address,tag in [(0x4200c560,0x18),(0x4200ad60,0x15),(0x42001560,2)]:
            self.assertEqual(locate(address),dict(line=address,set_index=43,tag=tag,byte_offset=0))
        self.assertEqual(locate(0x4200ad5f)['set_index'],42)
        self.assertEqual(locate(0x42000000)['tag'],0)
        self.assertEqual(locate(0x43ffffff),dict(line=0x43ffffe0,set_index=63,tag=0x3fff,byte_offset=31))
        for address in [0x3c000000,0x44000000,True]:
            with self.assertRaises(ValueError):locate(address)
        with self.assertRaisesRegex(ValueError,'inventory format'):analyze({},'')
        with self.assertRaisesRegex(ValueError,'groups/candidates'):
            analyze(dict(format='wirelink-A-static-residency-v1',images={}),'')

    def test_legacy_layout_and_domains(self):
        slots = {legacy_tag_address(s,w) for s in range(64) for w in range(8)}
        self.assertEqual((len(slots),min(slots),max(slots)),(512,0x600c6000,0x600c67fc))
        self.assertEqual([legacy_tag_address(43,w) for w in range(8)],
                         [0x600c60ac+w*0x100 for w in range(8)])
        for s,w in [(64,0),(0,8),(-1,0),(True,0)]:
            with self.assertRaises(ValueError):legacy_tag_address(s,w)

    def test_raw_fields_invalid_way_does_not_claim_a_miss(self):
        raw = (0x18<<9)|(5<<6)|(10<<2)|3
        self.assertEqual(decode_tag(raw,43),dict(valid=True,locked=True,attributes=10,
                         fifo_field=5,tag=0x18,line_address=0x4200c560))
        self.assertIsNone(decode_tag(raw&~1,43)['line_address'])
        self.assertEqual(decode_tag(0xffffffff,63)['line_address'],0x43ffffe0)
        self.assertEqual(decode_tag(raw|(0x1ff<<23),43),decode_tag(raw,43))
        for raw in [-1,2**32,True]:
            with self.assertRaises(ValueError):decode_tag(raw,0)

    def test_bounded_interpreter_load_arithmetic_and_rejection(self):
        # Synthetic helper, not a second copy of the published ROM fixture.
        code = parse_disassembly('''4004fd90 <synthetic>:
4004fd90: 004136 entry a1, 32
4004fd93: 060292 l8ui a9, a2, 6
4004fd96: 0288 l32i.n a8, a2, 0
4004fd98: c28890 quou a8, a8, a9
4004fd9b: 823830 mull a3, a8, a3
4004fd9e: d64121 l32r a2, 400456a8 <pool> (42000000 <base>)
4004fda1: 232a add.n a2, a3, a2
4004fda3: f01d retw.n
''')[0x4004fd90]
        self.assertEqual(interpret_virtual_address(code,24,0),0x4200c000)
        code[1]['args']='a9, a2, 99'
        with self.assertRaisesRegex(ValueError,'mode load'):interpret_virtual_address(code,24,0)
        code[1]['op']='s32i'
        with self.assertRaisesRegex(ValueError,'unsupported'):interpret_virtual_address(code,24,0)


if __name__ == '__main__':
    unittest.main()
