# SPDX-License-Identifier: Apache-2.0
import unittest

from analyze import parse_disassembly
from inspect_receive import path_calls, walk_annotated


def code(text):
    return parse_disassembly('42000000 <example>:\n' + text)[0x42000000]


class ReceiveInspectionTests(unittest.TestCase):
    def test_repeated_branch_requires_each_outcome(self):
        instructions = code('''42000000: 004136 entry a1, 32
42000003: 000056 bnez a2, 42000003 <example+3>
42000006: f01d retw.n
''')
        self.assertEqual(len(walk_annotated(instructions, {1: [True, True, False]}, {})), 5)
        with self.assertRaisesRegex(ValueError, 'unannotated branch visit'):
            walk_annotated(instructions, {1: [True]}, {})
        with self.assertRaisesRegex(ValueError, 'unused branch'):
            walk_annotated(instructions, {1: [False, False]}, {})

    def test_hardware_loop_has_exclusive_end_and_one_setup(self):
        instructions = code('''42000000: 004136 entry a1, 32
42000003: 000076 loop a2, 42000008 <example+8>
42000006: 881b addi.n a8, a8, 1
42000008: f01d retw.n
''')
        path = walk_annotated(instructions, {}, {1: 4})
        self.assertEqual([i['op'] for i in path],
                         ['entry', 'loop'] + ['addi.n'] * 4 + ['retw.n'])
        with self.assertRaisesRegex(ValueError, 'invalid hardware'):
            walk_annotated(instructions, {}, {1: 0})
        with self.assertRaisesRegex(ValueError, 'unreviewed'):
            walk_annotated(instructions, {}, {})

    def test_padding_and_runaway_rejected(self):
        instructions = code('''42000000: 000046 j 42000003 <example+3>
42000003: 00 .byte 00
42000004: f01d retw.n
''')
        with self.assertRaisesRegex(ValueError, 'padding'):
            walk_annotated(instructions, {}, {})
        instructions = code('42000000: 000046 j 42000000 <example>\n')
        with self.assertRaisesRegex(ValueError, 'instruction limit'):
            walk_annotated(instructions, {}, {}, limit=10)

    def test_indirect_call_resolves_only_immediate_literal(self):
        instructions = code('''42000000: 000081 l32r a8, 42000800 <pool> (400021b4 <__ashldi3>)
42000003: 0000e0 callx8 a8
''')
        self.assertEqual(path_calls(instructions)[0]['name'], '__ashldi3')
        instructions[0]['args'] = instructions[0]['args'].replace('a8,', 'a9,')
        with self.assertRaisesRegex(ValueError, 'unresolved path call'):
            path_calls(instructions)


if __name__ == '__main__':
    unittest.main()
