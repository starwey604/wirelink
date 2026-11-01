# SPDX-License-Identifier: Apache-2.0
import unittest

from analyze import parse_disassembly
from inspect_stream import (control_request_size, control_response_size,
                            instruction_model, successful_progress, varuint_size)


class StreamInspectionTests(unittest.TestCase):
    def test_alignment_bytes_and_relocated_branch_keep_same_cfg(self):
        a = parse_disassembly('''42000000 <example>:
42000000: 004136 entry a1, 32
42000003: 000046 j 42000007 <example+7>
42000006: 00 .byte 00
42000007: f01d retw.n
''')[0x42000000]
        b = parse_disassembly('''42000100 <example>:
42000100: 004136 entry a1, 32
42000103: 000046 j 42000106 <example+6>
42000106: f01d retw.n
''')[0x42000100]
        self.assertEqual(instruction_model(a), instruction_model(b))
        a[1]['args'] = '42000006 <example+6>'
        with self.assertRaisesRegex(ValueError, 'padding'):
            instruction_model(a)

    def test_nearest_symbol_for_integer_is_not_pointer_identity(self):
        a = parse_disassembly('''42000000 <example>:
42000000: 000081 l32r a8, 42000800 <pool> (7fffffff <old+0x123>)
''')[0x42000000]
        b = [dict(a[0], args='a8, 42000900 <pool> (7fffffff <new+0x456>)')]
        self.assertEqual(instruction_model(a), instruction_model(b))

    def test_literal_relocation_and_changed_binding_are_separate(self):
        a = parse_disassembly('''42000000 <example>:
42000000: 000081 l32r a8, 42000800 <pool> (3fc90000 <endpoint>)
''')[0x42000000]
        b = [dict(a[0], args='a8, 42000900 <pool> (3fc90100 <other>)')]
        x, y = instruction_model(a), instruction_model(b)
        self.assertEqual(x[0], y[0])
        self.assertNotEqual(x[1][0]['referent'], y[1][0]['referent'])

    def test_varuint_boundaries_and_zero_fields_are_counted(self):
        self.assertEqual([varuint_size(x) for x in [0, 127, 128, 16383, 16384]],
                         [1, 1, 2, 2, 3])
        self.assertEqual(control_request_size([7, 0, 0, 0, 0]), 31)
        self.assertEqual(control_request_size([2, 5, 1, 32, 5000]), 32)

    def ledger(self):
        begin = dict(usb_tx=100, usb_rx=200)
        stream = dict(size=32, window_ns=5000000000, direction=3, sent=640, received=320)
        device = dict(device={'x': 1})
        cpu = dict(device={'available': False})
        requests = (control_request_size([2, 5, 3, 32, 5000])
                    + control_request_size([3, 640, 320, 0, 0])
                    + control_request_size([7, 0, 0, 0, 0]))
        responses = 25 + control_response_size(device) + control_response_size(cpu)
        end = dict(usb_tx=100 + requests + 640 * 43 + 23 + 10 * 23,
                   usb_rx=200 + responses + 320 * 43 + 23 + 20 * 23)
        return begin, end, stream, device, cpu

    def test_successful_progress_closes_both_directions_and_excludes_markers(self):
        result = successful_progress(*self.ledger())
        self.assertEqual(result['normal_frames'], dict(host=10, device=20))
        self.assertEqual(result['host_chunks_per_normal_progress'], 32)
        self.assertEqual(result['device_chunks_per_normal_progress'], 32)

    def test_partial_frame_negative_residual_and_smoke_rejected(self):
        args = self.ledger()
        for value in [args[1]['usb_tx'] + 1, 0]:
            with self.assertRaisesRegex(ValueError, 'whole Progress'):
                successful_progress(args[0], dict(args[1], usb_tx=value), *args[2:])
        with self.assertRaisesRegex(ValueError, '32B/5s'):
            successful_progress(args[0], args[1], dict(args[2], window_ns=250000000), *args[3:])


if __name__ == '__main__':
    unittest.main()
