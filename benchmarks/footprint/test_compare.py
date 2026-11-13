# SPDX-License-Identifier: Apache-2.0
import copy
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path, PureWindowsPath, PurePosixPath

import compare
import footprint


def report(**overrides):
    base = {
        "schema": compare.LEGACY_SCHEMA,
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

    def test_current_kernel_storage_rows(self):
        before = report(schema=compare.SCHEMA, types={"wl_kernel_t": 1000}, storage=[
            {"deployment": "demo_usb_native_none", "endpoint": 1500, "packet_port": 160,
             "rx_units": 48, "stream_object": 0, "stream_tx": 0,
             "byte_fifo_object": 0, "byte_fifo_storage": 0}])
        after = copy.deepcopy(before)
        after["storage"][0]["rx_units"] += 16
        rows = self.rows_by_key(compare.compare(before, after))
        self.assertEqual(rows[("storage demo_usb_native_none", "rx_units")].after, 64)
        self.assertEqual(rows[("type", "wl_kernel_t")].after, 1000)

    def test_current_load_rejects_incomplete_negative_and_duplicate_budgets(self):
        storage = [{"deployment": name, **{key: 1 for key in compare.STORAGE_METRICS}}
                   for name in ("demo_usb_native_none", "demo_serial_cobs_crc16", "demo_length16_none")]
        current = report(schema=compare.SCHEMA, probe_enabled=True,
                         types={"wl_kernel_t": 616}, storage=storage)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "current.json"
            path.write_text(json.dumps(current))
            self.assertEqual(compare.load(path)["schema"], compare.SCHEMA)
            for damage in ("missing", "duplicate", "negative", "boolean"):
                broken = copy.deepcopy(current)
                if damage == "missing":
                    broken["storage"].pop()
                elif damage == "duplicate":
                    broken["storage"][1] = broken["storage"][0]
                else:
                    broken["storage"][0]["endpoint"] = -1 if damage == "negative" else True
                path.write_text(json.dumps(broken))
                with self.subTest(damage=damage), self.assertRaises(ValueError):
                    compare.load(path)

    def test_cross_schema_and_unqualified_elf_are_incomparable(self):
        with tempfile.TemporaryDirectory() as directory:
            old = Path(directory) / "old.json"
            new = Path(directory) / "new.json"
            old.write_text(json.dumps(report()))
            new.write_text(json.dumps(report(schema=compare.SCHEMA, probe_enabled=False,
                                             types={}, storage=[])))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main([str(old), str(new)]), 2)
            new.write_text(json.dumps(report(schema=compare.SCHEMA, probe_enabled=False,
                                             mode="elf", types={}, storage=[])))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main([str(new), str(new)]), 2)

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

    def test_recorded_flags_make_tree_paths_relative(self):
        # Isolated pair worktrees must record the same toolchain identity.
        self.assertEqual(
            ["-Os", "-I", "include", "-Isrc"],
            footprint.recorded_flags(
                ["-Os", "-I", "/tree/include", "-I/tree/src"], Path("/tree")))
        self.assertEqual(
            ["-O2", "-I", "/elsewhere/include"],
            footprint.recorded_flags(
                ["-O2", "-I", "/elsewhere/include"], Path("/tree")))

    def test_recorded_flags_respect_path_components_and_windows_spelling(self):
        for root in (PurePosixPath("/tree"), PureWindowsPath("/tree")):
            self.assertEqual(["-I/treehouse/include", "include", "-Isrc"],
                             footprint.recorded_flags(
                                 ["-I/treehouse/include", "/tree/include", r"-I\tree\src"], root))
        self.assertEqual(["-Iinclude", "src", r"-ID:\other\include"],
                         footprint.recorded_flags(
                             [r"-IC:\tree\include", "C:/tree/src", r"-ID:\other\include"],
                             PureWindowsPath("C:/tree")))


if __name__ == "__main__":
    unittest.main()
