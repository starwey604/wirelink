# SPDX-License-Identifier: Apache-2.0
"""Unit tests for the shared benchmark report library."""
import json
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from report import gate, junit, markdown, schema  # noqa: E402


def row(**overrides):
    base = dict(suite="framing", case="retry3/b512", metric="cpu_ns",
                unit="ns", before=100.0, after=100.0, direction="lower",
                rel=0.10, absolute=5.0, severity="fail")
    base.update(overrides)
    return gate.evaluate(**base)


class Gate(unittest.TestCase):
    def test_identical_passes(self):
        self.assertEqual(row()["status"], "pass")
        self.assertEqual(gate.exit_code([row()]), gate.EXIT_OK)

    def test_timing_needs_both_thresholds(self):
        # 100% regression but only +1 ns: absolute threshold not exceeded.
        self.assertEqual(row(before=1.0, after=2.0)["status"], "pass")
        # +20% and +20 ns: both exceeded -> fail.
        self.assertEqual(row(after=120.0)["status"], "fail")
        # +20% but +2 ns: below the 5 ns floor -> pass.
        self.assertEqual(row(before=10.0, after=12.0)["status"], "pass")

    def test_relative_only_when_absolute_missing(self):
        result = row(absolute=None, before=1.0, after=1.5)
        self.assertEqual(result["status"], "fail")
        self.assertEqual(result["threshold"]["abs"], None)

    def test_higher_direction(self):
        good = row(direction="higher", before=100.0, after=90.0)
        bad = row(direction="higher", before=100.0, after=80.0)
        self.assertEqual((good["status"], bad["status"]), ("pass", "fail"))
        # delta_rel is signed as after vs before, independent of direction.
        self.assertAlmostEqual(bad["delta_rel"], -0.20)

    def test_exact_flags_any_change(self):
        self.assertEqual(row(direction="exact", rel=None, absolute=None)["status"],
                         "pass")
        self.assertEqual(row(direction="exact", rel=None, absolute=None,
                             after=101.0)["status"], "fail")

    def test_severity_controls_blocking(self):
        warn = row(severity="warn", after=200.0)
        info = row(severity="info", after=200.0)
        self.assertEqual(warn["status"], "warn")
        self.assertEqual(gate.exit_code([warn]), gate.EXIT_OK)
        # info is report-only: still annotated as warn, never blocking; the
        # severity field keeps the distinction for consumers.
        self.assertEqual(info["status"], "warn")
        self.assertEqual(info["severity"], "info")
        self.assertEqual(gate.exit_code([info]), gate.EXIT_OK)

    def test_invalid_thresholds_rejected(self):
        with self.assertRaises(ValueError):
            row(rel=-1)
        with self.assertRaises(ValueError):
            row(absolute=math.nan)

    def test_zero_baseline_relative_is_infinite(self):
        result = row(before=0.0, after=3.0, rel=None)
        self.assertTrue(math.isinf(result["delta_rel"]))

    def test_exit_codes(self):
        self.assertEqual(gate.exit_code([row(), row(after=200.0)]),
                         gate.EXIT_REGRESSION)
        bad = gate.incomparable("s", "c", "m", "ns", "hash changed")
        self.assertEqual(gate.exit_code([row(), bad]), gate.EXIT_INVALID)
        self.assertEqual(gate.verdict([row(), bad])["incomparable"], 1)


class Schema(unittest.TestCase):
    def doc(self, **overrides):
        base = {
            "testbed": "github-ubuntu-x64",
            "revision": "abc",
            "environment": {"benchmark_source_hash": "h1"},
            "measurements": [schema.measurement("framing", "b512", "cpu_ns",
                                                 "ns", "lower", [100.0, 101.0])],
        }
        base.update(overrides)
        return schema.document(**base)

    def test_measurement_median_and_validation(self):
        m = schema.measurement("s", "c", "cpu_ns", "ns", "lower",
                               [3.0, 1.0, 2.0])
        self.assertEqual(m["median"], 2.0)
        with self.assertRaises(ValueError):
            schema.measurement("s", "c", "m", "ns", "lower", [])
        with self.assertRaises(ValueError):
            schema.measurement("s", "c", "m", "ns", "sideways", [1.0])
        with self.assertRaises(ValueError):
            schema.measurement("s", "c", "m", "ns", "lower", [math.inf])

    def test_document_requires_testbed_and_unique_keys(self):
        with self.assertRaises(ValueError):
            schema.document("", "abc", {}, [schema.measurement(
                "s", "c", "m", "ns", "lower", [1.0])])
        m = schema.measurement("s", "c", "m", "ns", "lower", [1.0])
        with self.assertRaises(ValueError):
            schema.document("t", "abc", {}, [m, m])

    def test_load_rejects_other_schemas(self):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump({"schema": "other"}, f)
        with self.assertRaises(ValueError):
            schema.load(f.name)

    def test_source_hash_mismatch_is_incomparable(self):
        base = self.doc()
        candidate = self.doc(environment={"benchmark_source_hash": "h2"})
        rows, reasons = schema.compare(base, candidate)
        self.assertTrue(reasons)
        self.assertTrue(all(r["status"] == "incomparable" for r in rows))
        self.assertEqual(gate.exit_code(rows), gate.EXIT_INVALID)

    def test_testbed_mismatch_is_incomparable(self):
        rows, reasons = schema.compare(self.doc(), self.doc(testbed="other"))
        self.assertTrue(any("testbed" in r for r in reasons))

    def test_added_or_removed_measurements_are_incomparable(self):
        candidate = self.doc()
        candidate["measurements"].append(schema.measurement(
            "framing", "new", "cpu_ns", "ns", "lower", [1.0]))
        _, reasons = schema.compare(self.doc(), candidate)
        self.assertTrue(any("added" in r for r in reasons))

    def test_repetition_change_is_incomparable(self):
        candidate = self.doc()
        candidate["measurements"][0]["samples"].append(102.0)
        rows, reasons = schema.compare(self.doc(), candidate)
        self.assertEqual(reasons, [])
        self.assertEqual(rows[0]["status"], "incomparable")

    def test_gated_compare(self):
        candidate = self.doc()
        candidate["measurements"][0]["median"] = 150.0
        candidate["measurements"][0]["samples"] = [150.0, 150.0]
        rows, reasons = schema.compare(self.doc(), candidate, rel=0.10,
                                       absolute=5.0, severity="fail")
        self.assertEqual(reasons, [])
        self.assertEqual(rows[0]["status"], "fail")

    def test_workload_hash_stable_and_sensitive(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            a = Path(d) / "workload.c"
            a.write_text("int x;")
            first = schema.workload_hash([a])
            self.assertEqual(first, schema.workload_hash([a]))
            a.write_text("int y;")
            self.assertNotEqual(first, schema.workload_hash([a]))

    def test_comparison_document_shape(self):
        doc = schema.comparison_document([row()], testbed="t",
                                         before="base", after="head")
        self.assertEqual(doc["schema"], schema.COMPARISON_SCHEMA)
        self.assertEqual(doc["exit_code"], gate.EXIT_OK)
        self.assertEqual(doc["verdict"]["pass"], 1)


class Renderers(unittest.TestCase):
    def rows(self):
        return [row(), row(after=200.0),
                gate.incomparable("s", "c", "m", "ns", "hash <changed>")]

    def test_markdown_contains_status_and_reasons(self):
        text = markdown.render(self.rows(), reasons=["workload hash changed"])
        self.assertIn("**FAIL**", text)
        self.assertIn("incomparable", text)
        self.assertIn("workload hash changed", text)
        self.assertIn("exit code: 2", text)

    def test_junit_is_well_formed_and_counts_failures(self):
        from xml.etree import ElementTree
        text = junit.render(self.rows(), suite="framing",
                            reasons=["workload hash changed"])
        root = ElementTree.fromstring(text)
        self.assertEqual(root.get("failures"), "1")
        self.assertEqual(root.get("errors"), "1")
        cases = root.findall(".//testcase")
        self.assertEqual(len(cases), 3)
        self.assertIsNone(cases[0].find("failure"))
        self.assertIsNotNone(cases[1].find("failure"))
        self.assertIsNotNone(cases[2].find("error"))
        self.assertIn("changed", cases[2].find("error").text)

    def test_junit_warns_are_not_failures(self):
        from xml.etree import ElementTree
        text = junit.render([row(severity="warn", after=200.0)], suite="s")
        root = ElementTree.fromstring(text)
        self.assertEqual(root.get("failures"), "0")


if __name__ == "__main__":
    unittest.main()
