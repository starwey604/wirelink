# SPDX-License-Identifier: Apache-2.0
import copy
import unittest
from compare import EXPECTED, validate


def fixture():
    rows = []
    for name, envelope, receiver in sorted(EXPECTED):
        loss = name in ("single_ack", "drop_ack", "drop_ack_backpressure", "saturated_wrap")
        pressure = name in ("backpressure", "drop_ack_backpressure", "saturated_wrap")
        rows.append(dict(format=1, clock="simulated_ms", duration_ms=2400,
                         scenario=name, envelope=envelope, receiver=receiver,
                         accepted=480, received=480, max_age_ms=8, max_arrival_age_ms=2,
                         max_gap_ms=7, mean_age_ms=4.0, rpc_completed=0 if name == "telemetry_only" else 10,
                         max_rpc_ms=0 if name == "telemetry_only" else 5,
                         ack_drops=1 if loss else 0, busy=1 if pressure else 0))
    return rows


class CaptureTests(unittest.TestCase):
    def test_complete(self):
        self.assertEqual(set(validate(fixture())), EXPECTED)

    def test_missing_or_duplicate(self):
        rows = fixture()
        for broken in (rows[:-1], rows + [rows[0]], rows[:-1] + [rows[0]]):
            with self.assertRaises(ValueError):
                validate(broken)

    def test_invalid_metrics_and_context(self):
        rows = fixture()
        for field, value in (("clock", "wall_ms"), ("duration_ms", 2000),
                             ("received", 481), ("received", 0), ("received", True),
                             ("max_age_ms", -1), ("max_arrival_age_ms", 9),
                             ("mean_age_ms", float("nan")), ("mean_age_ms", 100),
                             ("rpc_completed", 9), ("max_rpc_ms", 1500)):
            broken = copy.deepcopy(rows)
            broken[0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate(broken)

    def test_injection_was_exercised(self):
        for row in fixture():
            for field in ("ack_drops", "busy"):
                broken = fixture()
                target = next(item for item in broken if item["scenario"] == row["scenario"])
                target[field] = 0 if row[field] else 1
                with self.subTest(scenario=row["scenario"], field=field), self.assertRaises(ValueError):
                    validate(broken)


if __name__ == "__main__":
    unittest.main()
