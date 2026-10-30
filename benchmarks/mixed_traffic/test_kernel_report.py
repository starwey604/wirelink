# SPDX-License-Identifier: Apache-2.0
import copy
import json
from pathlib import Path
import subprocess
import sys
import unittest
import compare_kernel


class Report(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        capture = subprocess.check_output([sys.argv[1], "--report"], text=True)
        cls.rows = [json.loads(line) for line in capture.splitlines()]

    def test_real_capture(self):
        self.assertEqual(len(compare_kernel.validate(self.rows)), 42)

    def test_corrupt_missing_duplicate_old_and_nonfinite(self):
        variants = [self.rows[1:], self.rows + [self.rows[0]]]
        for field, value in (("format", 1), ("mean_age_ms", float("nan")),
                             ("rpc_handled", 1), ("dropped_responses", 1),
                             ("received", 3000), ("accepted", True)):
            rows = copy.deepcopy(self.rows)
            rows[0][field] = value
            variants.append(rows)
        for rows in variants:
            with self.assertRaises(ValueError):
                compare_kernel.validate(rows)

    def test_response_loss_requires_actual_duplicate_execution(self):
        rows = copy.deepcopy(self.rows)
        row = next(row for row in rows if row["scenario"] == "drop_response")
        row["rpc_handled"] = row["rpc_completed"]
        with self.assertRaises(ValueError):
            compare_kernel.validate(rows)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
