# SPDX-License-Identifier: Apache-2.0
import copy
import json
import tempfile
import unittest
from pathlib import Path

import compare


def case(loss_ppm, **overrides):
    base = {"loss_ppm": loss_ppm, "completed": 1998, "failed": 2, "mean_ms": 60.0,
            "p50_ms": 42, "p95_ms": 202, "p99_ms": 322, "max_ms": 382,
            "attempts_per_success": 4.0, "data_drops": 1085, "ack_drops": 511,
            "duplicate_rx": 509, "goodput_bytes_per_s": 966.0}
    base.update(overrides)
    return base


def report(cases=None, **config):
    settings = {"payload": 64, "delay_ms": 1, "ack_timeout_ms": 20,
                "max_retries": 20, "samples": 2000, "warmup": 32, "seed": 1}
    settings.update(config)
    return {"schema": compare.SCHEMA, "config": settings,
            "cases": cases if cases is not None else [case(0), case(500000)]}


class CompareTest(unittest.TestCase):
    def test_identical_reports_have_no_regression(self):
        before = report()
        rows = compare.compare(before, copy.deepcopy(before))
        self.assertEqual(compare.exceeding(rows, 0), [])

    def test_latency_growth_is_a_regression(self):
        after = report(cases=[case(500000, mean_ms=90.0)])
        before = report(cases=[case(500000, mean_ms=60.0)])
        rows = compare.compare(before, after)
        hits = compare.exceeding(rows, 10)
        self.assertIn("mean_ms", {row.item for row in hits})
        self.assertEqual(compare.regression_percent(
            next(row for row in rows if row.item == "mean_ms")), 50.0)

    def test_goodput_drop_is_a_regression(self):
        before = report(cases=[case(0, goodput_bytes_per_s=30000.0)])
        after = report(cases=[case(0, goodput_bytes_per_s=15000.0)])
        hits = compare.exceeding(compare.compare(before, after), 10)
        self.assertIn("goodput_bytes_per_s", {row.item for row in hits})

    def test_failure_growth_is_a_regression(self):
        before = report(cases=[case(500000, failed=2)])
        after = report(cases=[case(500000, failed=9)])
        hits = compare.exceeding(compare.compare(before, after), 10)
        self.assertIn("failed", {row.item for row in hits})

    def test_config_mismatch_is_rejected(self):
        with self.assertRaises(ValueError):
            compare.compare(report(seed=1), report(seed=2))

    def test_loss_set_mismatch_is_rejected(self):
        with self.assertRaises(ValueError):
            compare.compare(report(cases=[case(0)]),
                            report(cases=[case(10000)]))

    def test_load_enforces_schema(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.json"
            path.write_text(json.dumps({"schema": "other", "cases": [1]}))
            with self.assertRaises(ValueError):
                compare.load(path)


if __name__ == "__main__":
    unittest.main()
