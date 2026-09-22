# SPDX-License-Identifier: Apache-2.0
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bench  # noqa: E402

ALL_SUITES = ("owner_pass", "reliable_loss", "mixed_traffic", "footprint",
              "framing", "codec_plan", "rpc_validation", "api")


class Presets(unittest.TestCase):
    def test_repo_presets_parse(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        data = bench.load_presets()
        self.assertEqual(set(ALL_SUITES), set(data["suites"]))
        self.assertEqual({"smoke", "quick", "full"}, set(data["presets"]))
        for preset in data["presets"].values():
            self.assertTrue(set(preset["suites"]) <= set(data["suites"]))

    def test_smoke_is_deterministic_and_quick_is_timing(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        data = bench.load_presets()
        suites = data["suites"]
        deps, build, size = bench.requirements_for(
            [suites[name] for name in data["presets"]["smoke"]["suites"]])
        self.assertIn("wlc", deps)            # mixed_traffic generates code
        self.assertNotIn("google-benchmark", deps)
        self.assertTrue(build and size)
        quick_deps, _, _ = bench.requirements_for(
            [suites[name] for name in data["presets"]["quick"]["suites"]])
        self.assertIn("google-benchmark", quick_deps)
        for name in data["presets"]["smoke"]["suites"]:
            self.assertNotEqual(suites[name]["kind"], "gb")

    def test_every_suite_declares_kind_build_run_and_stamp_for_gb(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        for name, suite in bench.load_presets()["suites"].items():
            with self.subTest(suite=name):
                self.assertIn(suite["kind"], bench.KINDS)
                self.assertIn(suite["build"], bench.BUILDS)
                self.assertTrue(suite["run"])
                self.assertTrue(suite["description"])
                if suite["kind"] == "gb":
                    # A gb suite without compare() would bypass the shared
                    # three-state surface; api stamps inside its runner.
                    self.assertIn("compare", suite)

    def test_unknown_placeholder_rejected(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        with tempfile.TemporaryDirectory() as d:
            bad = Path(d) / "presets.toml"
            bad.write_text(
                '[suites.x]\nkind = "model"\nbuild = "none"\n'
                'description = "x"\nrun = ["echo", "{nope}"]\n'
                '[presets.p]\nsuites = ["x"]\ngate = { mode = "auto" }\n')
            with self.assertRaises(SystemExit):
                bench.load_presets(bad)

    def test_preset_with_unknown_suite_rejected(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        with tempfile.TemporaryDirectory() as d:
            bad = Path(d) / "presets.toml"
            bad.write_text(
                '[suites.x]\nkind = "model"\nbuild = "none"\n'
                'description = "x"\nrun = ["echo"]\n'
                '[presets.p]\nsuites = ["ghost"]\n')
            with self.assertRaises(SystemExit):
                bench.load_presets(bad)


class Commands(unittest.TestCase):
    def test_list_prints_suites_and_presets(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = bench.main(["list"])
        self.assertEqual(code, 0)
        text = out.getvalue()
        for token in ALL_SUITES + ("smoke", "quick", "full", "gate:"):
            self.assertIn(token, text)

    def test_doctor_runs_with_two_state_result(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = bench.main(["doctor"])
        # 0 or 2 only: tool availability is machine-specific, but doctor must
        # never pretend a missing tool is a regression (1).
        self.assertIn(code, (0, 2))
        text = out.getvalue()
        self.assertIn("requirements for preset smoke", text)
        for tool in ("python", "cmake", "c compiler", "size", "wlc"):
            self.assertIn(tool, text)

    def test_doctor_unknown_preset_exits_two(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            with self.assertRaises(SystemExit) as caught:
                bench.main(["doctor", "--preset", "nope"])
        self.assertEqual(caught.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
