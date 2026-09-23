# SPDX-License-Identifier: Apache-2.0
import contextlib
import io
import json
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

    def test_footprint_uses_fixed_driver_against_selected_source(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        run = bench.load_presets()["suites"]["footprint"]["run"]
        self.assertIn("{driver}/benchmarks/footprint/footprint.py", run)
        self.assertEqual("{source}", run[run.index("--root") + 1])

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

    def test_run_unknown_suite_exits_two(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            with self.assertRaises(SystemExit) as caught:
                bench.main(["run", "--suite", "ghost"])
        self.assertEqual(caught.exception.code, 2)

    def test_run_rejects_non_checkout_source_tree(self):
        with tempfile.TemporaryDirectory() as d:
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as caught:
                    bench.main(["run", "--suite", "footprint",
                                "--source-tree", d])
        self.assertEqual(caught.exception.code, 2)

    def test_report_requires_a_run_directory(self):
        with tempfile.TemporaryDirectory() as d:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = bench.main(["report", d])
            self.assertEqual(code, 2)
            self.assertIn("not a run directory", out.getvalue())

    def test_report_normalizes_raw_evidence(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        from report import manifest
        with tempfile.TemporaryDirectory() as d:
            run = Path(d)
            (run / "raw").mkdir()
            raw = run / "raw" / "owner_pass"
            raw.mkdir()
            (raw / "owner_pass.jsonl").write_text(
                '{"case":"idle","budget":0,"delivered":0,"received":0,'
                '"max_per_pass":2}\n')
            manifest.write_manifest(run / "manifest.json",
                                    manifest.manifest(suite="smoke",
                                                      testbed="unit"))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = bench.main(["report", str(run)])
            self.assertEqual(code, 0, out.getvalue())
            normalized = json.loads(
                (run / "normalized.json").read_text(encoding="utf-8"))
            self.assertEqual("wirelink-benchmark-v2", normalized["schema"])
            self.assertEqual("unit", normalized["testbed"])
            self.assertEqual(3, len(normalized["measurements"]))
            # Idempotent: a second report produces the same document.
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(0, bench.main(["report", str(run)]))
            self.assertEqual(
                normalized,
                json.loads((run / "normalized.json").read_text()))


class Pair(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def make_run(self, name, *, testbed="unit", footprint=None,
                 owner_pass=None):
        from report import manifest
        run = self.root / name
        (run / "raw").mkdir(parents=True)
        if footprint is not None:
            raw = run / "raw" / "footprint"
            raw.mkdir()
            (raw / "footprint.json").write_text(json.dumps(footprint),
                                                encoding="utf-8")
        if owner_pass is not None:
            raw = run / "raw" / "owner_pass"
            raw.mkdir()
            (raw / "owner_pass.jsonl").write_text(owner_pass,
                                                  encoding="utf-8")
        manifest.write_manifest(run / "manifest.json",
                                manifest.manifest(suite="smoke",
                                                  testbed=testbed))
        return run

    def run_pair(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = bench.main(["pair", *map(str, argv)])
        return code, out.getvalue()

    def test_gate_argv_maps_preset_thresholds_to_suite_flags(self):
        if bench.tomllib is None:
            self.skipTest("tomllib needs Python 3.11+")
        data = bench.load_presets()
        quick = data["presets"]["quick"]["gate"]
        framing = data["suites"]["framing"]
        self.assertEqual(["--gate", "warn", "--max-regression-percent", "10.0",
                          "--max-regression-ns", "5.0"],
                         bench._gate_argv(framing, quick))
        smoke = data["presets"]["smoke"]["gate"]
        self.assertEqual(["--gate", "auto", "--max-regression-percent", "0.0"],
                         bench._gate_argv(data["suites"]["footprint"], smoke))
        mixed = data["suites"]["mixed_traffic"]
        self.assertEqual(["--gate", "fail", "--max-regression-ms", "8.0"],
                         bench._gate_argv(
                             mixed, {"mode": "fail", "ms": 8.0}))

    def test_pair_identical_runs_pass(self):
        from report.test_normalize import footprint_report
        before = self.make_run("before", footprint=footprint_report())
        after = self.make_run("after", footprint=footprint_report())
        code, _ = self.run_pair("--suite", "footprint", "--before", before,
                                "--after", after)
        self.assertEqual(0, code)
        document = json.loads((after / "comparison.json").read_text())
        self.assertEqual("wirelink-benchmark-comparison-v1",
                         document["schema"])
        self.assertEqual(0, document["exit_code"])
        self.assertTrue((after / "summary.md").is_file())
        self.assertTrue((after / "junit.xml").is_file())

    def test_pair_exact_change_is_a_regression(self):
        from report.test_normalize import footprint_report
        before_report = footprint_report()
        after_report = footprint_report()
        after_report["types"]["wl_ctx_t"] = 900  # exact: any change fails
        before = self.make_run("before", footprint=before_report)
        after = self.make_run("after", footprint=after_report)
        code, _ = self.run_pair("--suite", "footprint", "--before", before,
                                "--after", after)
        self.assertEqual(1, code)
        document = json.loads((after / "comparison.json").read_text())
        self.assertEqual(1, document["exit_code"])
        self.assertGreaterEqual(document["verdict"]["fail"], 1)

    def test_pair_relative_threshold_gates_section_growth(self):
        from report.test_normalize import footprint_report
        # quick profile: warn only — a large change annotates but never blocks.
        small = footprint_report()
        large = footprint_report()
        large["totals"]["text"] = 200
        before = self.make_run("before", footprint=small)
        after = self.make_run("after", footprint=large)
        code, _ = self.run_pair("--suite", "footprint", "--preset", "quick",
                                "--before", before, "--after", after)
        self.assertEqual(0, code, "quick is warn-only until calibrated")
        document = json.loads((after / "comparison.json").read_text())
        self.assertGreaterEqual(document["verdict"]["warn"], 1)

    def test_pair_testbed_mismatch_is_incomparable(self):
        from report.test_normalize import footprint_report
        before = self.make_run("before", testbed="runner-a",
                               footprint=footprint_report())
        after = self.make_run("after", testbed="runner-b",
                              footprint=footprint_report())
        code, text = self.run_pair("--suite", "footprint", "--before", before,
                                   "--after", after)
        self.assertEqual(2, code)
        document = json.loads((after / "comparison.json").read_text())
        self.assertEqual(2, document["exit_code"])
        self.assertTrue(document["reasons"])
        self.assertIn("not comparable", text)

    def test_pair_missing_raw_is_incomparable(self):
        from report.test_normalize import footprint_report
        before = self.make_run("before", footprint=footprint_report())
        after = self.make_run("after")  # raw/footprint absent
        code, _ = self.run_pair("--suite", "footprint", "--before", before,
                                "--after", after)
        self.assertEqual(2, code)
        document = json.loads((after / "comparison.json").read_text())
        self.assertTrue(any("missing raw report" in reason
                            for reason in document["reasons"]))

    def test_pair_owner_pass_compares_exact_contract_fields(self):
        before = self.make_run(
            "before",
            owner_pass='{"case":"rx_burst","budget":0,"delivered":8,'
                       '"received":8,"max_per_pass":3}\n')
        after_same = self.make_run(
            "after_same",
            owner_pass='{"case":"rx_burst","budget":0,"delivered":8,'
                       '"received":8,"max_per_pass":9}\n')  # racy counter ignored
        code, _ = self.run_pair("--suite", "owner_pass", "--before", before,
                                "--after", after_same)
        self.assertEqual(0, code, "max_per_pass is racy and must not gate")
        after_changed = self.make_run(
            "after_changed",
            owner_pass='{"case":"rx_burst","budget":0,"delivered":7,'
                       '"received":7,"max_per_pass":3}\n')
        code, _ = self.run_pair("--suite", "owner_pass", "--before", before,
                                "--after", after_changed)
        self.assertEqual(1, code, "a delivered-count change is a finding")

    def test_pair_argument_validation_exits_two(self):
        for argv in (["pair", "--before", "x"],
                     ["pair", "--candidate", "."],
                     ["pair", "--before", "x", "--after", "y",
                      "--base", "HEAD"],
                     ["pair", "--suite", "ghost", "--before", "x",
                      "--after", "y"]):
            with self.subTest(argv=argv):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as caught:
                        bench.main(argv)
                self.assertEqual(caught.exception.code, 2)

    def test_merge_conservative_keeps_the_worse_bracket(self):
        rows = [
            {"suite": "s", "case": "c", "metric": "m", "status": "pass"},
            {"suite": "s", "case": "c", "metric": "m", "status": "fail"},
        ]
        self.assertEqual("fail", bench._merge_conservative(rows)[0]["status"])
        self.assertEqual(1, len(bench._merge_conservative(rows)))

    def test_invoke_run_uses_current_driver_for_legacy_source_tree(self):
        legacy = self.root / "legacy"
        legacy.mkdir()
        destination = self.root / "run"
        captured = []
        original = bench.subprocess.run

        def fake(argv, **kwargs):
            captured.append((argv, kwargs))
            return bench.subprocess.CompletedProcess(argv, 0)

        bench.subprocess.run = fake
        self.addCleanup(setattr, bench.subprocess, "run", original)
        args = bench.argparse.Namespace(suite=["footprint"], preset="smoke",
                                        jobs=None)
        bench._invoke_run(legacy, destination, args)
        argv, kwargs = captured[0]
        self.assertEqual(str(bench.BENCH / "bench.py"), argv[1])
        self.assertEqual(["--source-tree", str(legacy)], argv[3:5])
        self.assertEqual(bench.REPO, kwargs["cwd"])


class Calibrate(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def test_calibrate_requires_at_least_two_runs(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as caught:
                bench.main(["calibrate", "--runs", "1"])
        self.assertEqual(caught.exception.code, 2)

    def test_calibrate_refuses_to_overwrite(self):
        out = self.root / "existing"
        out.mkdir()
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            code = bench.main(["calibrate", "--suite", "footprint",
                               "--runs", "2", "--out", str(out)])
        self.assertEqual(code, 2)
        self.assertIn("refusing to overwrite", captured.getvalue())

    def test_calibrate_derives_median_and_mad(self):
        from report.test_normalize import footprint_report
        original = bench._run_suite

        def fake(name, suite, raw, out, args):
            # One deterministic run per directory, drifting by 1 byte each.
            report = footprint_report()
            report["totals"]["text"] = 100 + int(out.name)
            (raw / "footprint.json").write_text(json.dumps(report),
                                                encoding="utf-8")

        bench._run_suite = fake
        self.addCleanup(setattr, bench, "_run_suite", original)
        out = self.root / "cal"
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            code = bench.main(["calibrate", "--suite", "footprint",
                               "--runs", "3", "--out", str(out)])
        self.assertEqual(code, 0, captured.getvalue())
        document = json.loads((out / "calibration.json").read_text())
        self.assertEqual("wirelink-benchmark-calibration-v1",
                         document["schema"])
        self.assertEqual(3, document["runs"])
        self.assertEqual("footprint", document["preset"])
        self.assertTrue((out / "runs" / "01" / "manifest.json").is_file())

        def find(suite, case, metric):
            return next(item for item in document["metrics"]
                        if (item["suite"], item["case"], item["metric"])
                        == (suite, case, metric))

        text = find("footprint", "total", "text")
        self.assertEqual([101.0, 102.0, 103.0], text["samples"])
        self.assertEqual(102, text["median"])
        self.assertEqual(1, text["mad"])
        self.assertEqual(5, text["noise_floor"])
        rodata = find("footprint", "total", "rodata")
        self.assertEqual((20, 0, 0),
                         (rodata["median"], rodata["mad"],
                          rodata["noise_floor"]))


class Build(unittest.TestCase):
    def test_build_passes_definitions_and_forwards_toolchain_env(self):
        captured = []
        original_run = bench._run

        def fake_run(argv, cwd=None, stdout_path=None, step="command"):
            captured.append((step, list(argv)))

        bench._run = fake_run
        self.addCleanup(setattr, bench, "_run", original_run)
        keys = ("WIRELINK_WLC_EXECUTABLE",
                "WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR")
        saved = {key: bench.os.environ.get(key) for key in keys}

        def restore():
            for key, value in saved.items():
                if value is None:
                    bench.os.environ.pop(key, None)
                else:
                    bench.os.environ[key] = value

        self.addCleanup(restore)
        bench.os.environ["WIRELINK_WLC_EXECUTABLE"] = "/fake/wlc"
        bench.os.environ["WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR"] = "/fake/gb"
        suite = {"name": "demo", "build": "top", "source": "benchmarks/demo",
                 "cmake_options": ["WIRELINK_BUILD_X=ON"],
                 "requires": ["wlc", "google-benchmark"]}
        bench._build(suite, bench.argparse.Namespace(jobs=None,
                                                     skip_build=False))
        self.assertEqual(["cmake configure", "cmake build"],
                         [step for step, _ in captured])
        configure = captured[0][1]
        # Preset options reach cmake as -D definitions, never as bare paths.
        self.assertIn("-DWIRELINK_BUILD_X=ON", configure)
        self.assertNotIn("WIRELINK_BUILD_X=ON", configure)
        self.assertIn("-DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/fake/gb",
                      configure)
        self.assertIn("-DWIRELINK_WLC_EXECUTABLE=/fake/wlc", configure)
        build = captured[1][1]
        self.assertEqual(["cmake", "--build", build[2]], build[:3])
        # Suites without a wlc requirement never see the definition.
        del suite["requires"]
        bench._build(suite, bench.argparse.Namespace(jobs=None,
                                                     skip_build=False))
        self.assertNotIn("-DWIRELINK_WLC_EXECUTABLE=/fake/wlc",
                         captured[2][1])


if __name__ == "__main__":
    unittest.main()
