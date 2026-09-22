# SPDX-License-Identifier: Apache-2.0
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from report import normalize, schema  # noqa: E402


def framing_report():
    return {"context": {"workload": "wirelink-framing-v1", "host_name": "test"},
            "benchmarks": [{"name": "busy3/e0/i0/p2/b120", "run_type": "iteration",
                            "repetition_index": i, "cpu_time": 100 + i,
                            "real_time": 120 + i, "time_unit": "ns",
                            "sink_calls_per_op": 4, "context_bytes": 896}
                           for i in range(3)]}


def api_report():
    return {"format": "wirelink-performance-v1", "context": {
        "system": "test", "machine": "test", "host": "test", "samples": 100,
        "warmup": 10},
        "udp": [{"payload_bytes": 64, "p50_ns": 500.0, "p95_ns": 800.0,
                 "p99_ns": 900.0, "max_ns": 1000.0, "cpu_ns_per_call": 40.0,
                 "server": {"cpu_ns_per_call": 42.0}}],
        "micro": {"one": {"context": {"workload": "wirelink-api-v1"},
                          "benchmarks": [
                              {"name": "Rpc/32/1", "cpu_time": n,
                               "real_time": n * 2, "time_unit": "ns",
                               "endpoint_bytes": 1000,
                               "runtime_arena_bytes": 500}
                              for n in [10, 11, 90]]}}}


def footprint_report():
    return {"schema": "wirelink-footprint-v1", "mode": "objects",
            "compiler": "cc 1.0",
            "totals": {"text": 100, "rodata": 20, "data": 4, "bss": 8,
                       "debug": 0, "other": 2},
            "modules": {"core": {"text": 60, "rodata": 10, "data": 0,
                                 "bss": 0, "debug": 0, "other": 0}},
            "sections": {"wirelink.text": 96},
            "storage": [{"envelope": 1, "integrity": 0, "max_payload": 512,
                         "tx_payload": 600, "tx_unit": 64, "control": 128,
                         "rx_fifo": 1024, "rx_fallback": 256}],
            "types": {"wl_ctx_t": 896}}


def reliable_loss_report():
    return {"schema": "wirelink-reliable-loss-v1", "config": {"payload": 64},
            "cases": [{"loss_ppm": 100, "failed": 1, "mean_ms": 12.5,
                       "p95_ms": 20.0, "p99_ms": 30.0, "max_ms": 40.0,
                       "attempts_per_success": 1.2, "completed": 500,
                       "goodput_bytes_per_s": 12345.6}]}


def mixed_traffic_capture():
    loss = {"single_ack", "drop_ack", "drop_ack_backpressure", "saturated_wrap"}
    pressure = {"backpressure", "drop_ack_backpressure", "saturated_wrap"}
    module = normalize._suite_module("mixed_traffic")
    rows = []
    for scenario in module.SCENARIOS:
        for envelope in range(3):
            for receiver in range(2):
                rpc = scenario != "telemetry_only"
                rows.append({
                    "format": 1, "clock": "simulated_ms", "duration_ms": 2400,
                    "scenario": scenario, "envelope": envelope,
                    "receiver": receiver, "accepted": 100, "received": 90,
                    "max_age_ms": 50, "max_arrival_age_ms": 10,
                    "max_gap_ms": 5, "mean_age_ms": 20.5,
                    "rpc_completed": 10 if rpc else 0,
                    "max_rpc_ms": 120 if rpc else 0,
                    "ack_drops": 1 if scenario in loss else 0,
                    "busy": 1 if scenario in pressure else 0})
    return "".join(json.dumps(row) + "\n" for row in rows)


def owner_pass_capture():
    return (json.dumps({"case": "idle", "budget": 0, "delivered": 0,
                        "received": 0, "max_per_pass": 2}) + "\n"
            + json.dumps({"case": "rx_burst", "budget": 0, "delivered": 8,
                          "received": 8, "max_per_pass": 3}) + "\n")


class Normalize(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)

    def emit(self, suite, files):
        for name, content in files.items():
            path = self.dir / name
            path.write_text(content if isinstance(content, str)
                            else json.dumps(content), encoding="utf-8")
        return normalize.normalize(suite, self.dir)

    def find(self, rows, case, metric):
        matches = [row for row in rows
                   if row["case"] == case and row["metric"] == metric]
        self.assertEqual(1, len(matches))
        return matches[0]

    def test_framing_gb_keeps_all_repetitions(self):
        rows = self.emit("framing", {"framing.json": framing_report()})
        cpu = self.find(rows, "busy3/e0/i0/p2/b120", "cpu_ns")
        self.assertEqual([100, 101, 102], cpu["samples"])
        self.assertEqual(101, cpu["median"])
        self.assertEqual(("ns", "lower"), (cpu["unit"], cpu["direction"]))
        self.assertEqual(121, self.find(rows, "busy3/e0/i0/p2/b120",
                                        "wall_ns")["median"])

    def test_codec_plan_full_matrix(self):
        module = normalize._suite_module("codec_plan")
        report = {"context": {"workload": "wirelink-codec-plan-v1"},
                  "benchmarks": [{"name": f"k{k}/p{p}/s{s}", "cpu_time": 100,
                                  "real_time": 101, "time_unit": "ns",
                                  "encoded_bytes": 5}
                                 for k, p, s in module.HOST]}
        rows = self.emit("codec_plan", {"codec_plan.json": report})
        self.assertEqual(len(list(module.HOST)) * 2, len(rows))

    def test_rpc_validation_matrix(self):
        module = normalize._suite_module("rpc_validation")
        report = {"context": {"workload": "wirelink-rpc-validation-v2"},
                  "benchmarks": [{"name": module.name(k), "cpu_time": 100,
                                  "real_time": 110, "time_unit": "ns",
                                  "encoded_bytes": 10, "repetition_index": r}
                                 for k in module.GROUPS for r in range(3)]}
        rows = self.emit("rpc_validation", {"rpc_validation.json": report})
        self.assertEqual(len(module.GROUPS) * 2, len(rows))
        self.assertEqual(100, rows[0]["median"])

    def test_api_medians_stay_in_sync_with_comparator(self):
        module = normalize._suite_module("api")
        rows = self.emit("api", {"api.json": api_report()})
        expected = module.measurements(api_report())
        for key, median in expected.items():
            self.assertEqual(median, self.find(rows, *key)["median"])
        exact = self.find(rows, "one/Rpc/32/1", "endpoint_bytes")
        self.assertEqual(("bytes", "exact"), (exact["unit"], exact["direction"]))
        self.assertEqual("lower", self.find(rows, "udp/64B", "p50_ns")["direction"])

    def test_footprint_directions(self):
        rows = self.emit("footprint", {"footprint.json": footprint_report()})
        self.assertEqual(("lower", 100),
                         (self.find(rows, "total", "text")["direction"],
                          self.find(rows, "total", "text")["median"]))
        self.assertEqual("exact", self.find(rows, "type", "wl_ctx_t")["direction"])
        storage = [row for row in rows if row["case"].startswith("storage ")]
        self.assertTrue(storage)
        self.assertTrue(all(row["direction"] == "exact" for row in storage))

    def test_reliable_loss_directions_and_units(self):
        rows = self.emit("reliable_loss",
                         {"reliable_loss.json": reliable_loss_report()})
        self.assertEqual(("lower", "count"),
                         (self.find(rows, "0.01%", "failed")["direction"],
                          self.find(rows, "0.01%", "failed")["unit"]))
        self.assertEqual("higher",
                         self.find(rows, "0.01%", "completed")["direction"])
        self.assertEqual(("ms", "lower"),
                         (self.find(rows, "0.01%", "mean_ms")["unit"],
                          self.find(rows, "0.01%", "mean_ms")["direction"]))
        self.assertEqual("bytes/s",
                         self.find(rows, "0.01%",
                                   "goodput_bytes_per_s")["unit"])

    def test_mixed_traffic_uses_scenario_maxima(self):
        rows = self.emit("mixed_traffic",
                         {"mixed_traffic.jsonl": mixed_traffic_capture()})
        self.assertEqual(7 * 3, len(rows))
        self.assertEqual(50, self.find(rows, "drop_ack", "max_age_ms")["median"])
        self.assertEqual(120, self.find(rows, "drop_ack", "max_rpc_ms")["median"])
        self.assertEqual(0, self.find(rows, "telemetry_only", "max_rpc_ms")
                         ["median"])

    def test_owner_pass_normalizes_only_deterministic_fields(self):
        rows = self.emit("owner_pass", {"owner_pass.jsonl": owner_pass_capture()})
        self.assertEqual(6, len(rows))
        burst = self.find(rows, "rx_burst", "delivered")
        self.assertEqual(("count", "exact", 8),
                         (burst["unit"], burst["direction"], burst["median"]))
        self.assertNotIn("max_per_pass", {row["metric"] for row in rows})

    def test_invalid_inputs_raise(self):
        with self.assertRaises(ValueError):
            normalize.normalize("unknown-suite", self.dir)
        with self.assertRaises(ValueError):
            normalize.normalize("footprint", self.dir)  # no raw report

    def test_documents_from_several_suites_round_trip(self):
        rows = self.emit("owner_pass", {"owner_pass.jsonl": owner_pass_capture()})
        # Two suites in one directory: write the second raw file too.
        (self.dir / "footprint.json").write_text(json.dumps(footprint_report()))
        rows += normalize.normalize("footprint", self.dir)
        document = schema.document("unit", "abc123", {"cpu": "test"}, rows)
        path = self.dir / "normalized.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        self.assertEqual(document, schema.load(path))
        with self.assertRaises(ValueError):
            schema.document("unit", None, {}, rows + [rows[0]])


if __name__ == "__main__":
    unittest.main()
