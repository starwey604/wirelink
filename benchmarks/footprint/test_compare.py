# SPDX-License-Identifier: Apache-2.0
import copy
import unittest

import compare


def report(**overrides):
    base = {
        "schema": compare.SCHEMA,
        "mode": "objects",
        "compiler": {"cc": "cc", "flags": ["-Os"]},
        "target": None,
        "modules": {"src/cobs.c": {"text": 100, "rodata": 4, "data": 0,
                                   "bss": 0, "other": 0}},
        "sections": None,
        "totals": {"text": 100, "rodata": 4, "data": 0, "bss": 0, "other": 0},
        "types": {"wl_ctx_t": 896},
        "storage": [
            {"envelope": 0, "integrity": 2, "max_payload": 2048, "mtu": 2088,
             "tx_payload": 2048, "tx_unit": 2088, "control": 8, "rx_fifo": 4096,
             "rx_fallback": 2087},
        ],
    }
    base.update(overrides)
    return base


class CompareTest(unittest.TestCase):
    def rows_by_key(self, rows):
        return {(row.group, row.item): row for row in rows}

    def test_identical_reports_have_no_delta(self):
        before = report()
        rows = compare.compare(before, copy.deepcopy(before))
        self.assertTrue(rows)
        self.assertTrue(all(row.before == row.after for row in rows))
        self.assertEqual(compare.exceeding(rows, 10), [])

    def test_module_growth_is_detected(self):
        before = report()
        after = report(
            modules={"src/cobs.c": {"text": 130, "rodata": 4, "data": 0,
                                    "bss": 0, "other": 0}},
            totals={"text": 130, "rodata": 4, "data": 0, "bss": 0, "other": 0},
        )
        rows = self.rows_by_key(compare.compare(before, after))
        self.assertEqual(rows[("src/cobs.c", "text")].after, 130)
        hits = compare.exceeding(compare.compare(before, after), 10)
        self.assertIn(("src/cobs.c", "text"), {(r.group, r.item) for r in hits})

    def test_storage_growth_is_detected(self):
        before = report()
        entry = dict(before["storage"][0])
        entry["rx_fifo"] = 8192
        after = report(storage=[entry])
        rows = self.rows_by_key(compare.compare(before, after))
        self.assertEqual(rows[("storage env0/int2/p2048", "rx_fifo")].after, 8192)
        self.assertEqual(
            compare.exceeding(compare.compare(before, after), 10)[0].item,
            "rx_fifo")

    def test_elf_sections_compare(self):
        before = report(mode="elf", modules=None,
                        sections={".text": 1000, ".dram0.bss": 200})
        after = report(mode="elf", modules=None,
                       sections={".text": 1100, ".dram0.bss": 200})
        rows = self.rows_by_key(compare.compare(before, after))
        self.assertEqual(rows[("section", ".text")].after, 1100)
        self.assertEqual(rows[("section", ".dram0.bss")].after, 200)

    def test_new_symbol_percent_is_infinite(self):
        self.assertEqual(compare.percent(0, 8), float("inf"))
        self.assertEqual(compare.percent(0, 0), 0.0)

    def test_schema_is_enforced(self):
        with self.assertRaises(ValueError):
            compare.load(__file__)


if __name__ == "__main__":
    unittest.main()
