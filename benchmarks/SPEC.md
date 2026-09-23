# Benchmark specification

[中文](SPEC-cn.md).

This is the normative contract for the Wirelink benchmark system: result
schema, exit codes, output layout, comparison rules, and CI layering. Policy
background lives in [docs/development.md](../docs/development.md) ("Benchmark
policy"); per-suite guides live in each `benchmarks/<suite>/README.md`.

Benchmarks remain opt-in and are never correctness gates. What this document
adds is a uniform way for a *performance regression gate* to fail
predictably, on an identified testbed, with evidence a human can audit.

## Entry point

All suites are driven through one Python 3.11+ entry point (no third-party
dependencies; `tomllib` reads the preset file):

```sh
python3 benchmarks/bench.py doctor      # toolchain/deps/serial-port checks
python3 benchmarks/bench.py list        # suites and presets
python3 benchmarks/bench.py run --preset <name> [--out DIR]
python3 benchmarks/bench.py pair --preset <name> --base REV --candidate REV [--order ab|abba]
python3 benchmarks/bench.py pair --preset <name> --before DIR --after DIR
python3 benchmarks/bench.py report RUN_DIR
python3 benchmarks/bench.py calibrate --preset <name> --runs N
```

- `run` configures CMake from the preset, builds, executes, and collects raw
  evidence. Run `report` to derive `normalized.json`.
- `pair` compares two runs with each suite's own comparator and writes
  `comparison.json`, `summary.md`, and `junit.xml`. With `--base`/`--candidate`
  it uses the candidate's driver and preset to build both sides in isolated
  source trees, so the base may predate `bench.py`, and alternates them
  (default A/B/B/A, keeping the worse status per metric across brackets); with
  `--before`/`--after` it re-compares existing run directories without
  re-measuring (idempotent, outputs land in the `--after` directory).
- `report` re-derives `normalized.json` from `raw/` without re-measuring
  (idempotent); the comparison outputs are re-derived by `pair`.
- `calibrate` repeats one baseline on one testbed and emits the median/MAD
  noise band that threshold values must be derived from.

The unified HIL command remains planned; firmware suites currently use their
per-suite build, flash, and capture instructions.

Presets are declared in `benchmarks/presets.toml`. Each suite entry names its
`kind` (`deterministic`, `timing`, `size`), the CMake options it needs, its
command, and its gate profile. Initial presets: `smoke`, `quick`, `full`.

## Exit codes

Every `bench.py` subcommand and every `compare.py` uses the same three-value
contract:

| Code | Meaning |
| --- | --- |
| `0` | Pass: measurements are valid and all enabled gates hold |
| `1` | Regression: measurements are valid, a gate threshold is exceeded |
| `2` | Invalid: measurements are missing, incomplete, mismatched, or otherwise not comparable — no regression verdict is implied |

Exit `2` is never a performance verdict. It means the run produced no
trustworthy comparison, and CI must treat it as a failed run to rerun or
investigate, not as a passing or regressing benchmark.

## Output layout

Each execution writes one directory; existing directories are never
overwritten (evidence preservation, matching `benchmarks/api/run.py`):

```text
out/<run-id>/
  manifest.json       # run identity: revision, testbed, environment
  raw/                # untouched original outputs (Google Benchmark JSON,
                      #   console captures, per-suite native reports)
  normalized.json     # wirelink-benchmark-v2 measurements
  comparison.json     # machine-readable verdict (pair/report only)
  summary.md          # human summary for $GITHUB_STEP_SUMMARY or a PR comment
  junit.xml           # one test case per gated metric
  artifacts/          # binaries.sha256, CMakeCache.txt, firmware.elf,
                      #   firmware.map, zephyr.config (as applicable)
```

`raw/` is authoritative. Normalization and reporting must be reproducible
from `raw/` alone via `bench.py report`; a report may never hide or mutate
raw data.

`manifest.json` uses schema `wirelink-benchmark-manifest-v1` and, like the
rest of the directory, is created exclusively. Google Benchmark JSON captured
directly by a suite records its workload identity with `benchmarks/stamp.py`
(`context.benchmark_source_hash`); every native comparator applies the same
both-sides rule as the normalized layer below, so differing hashes yield
`incomparable`, never a regression verdict.

## Result schema: wirelink-benchmark-v2

`normalized.json` is the single logical model every suite is converted into.
Suite-native formats stay in `raw/`; only this layer is uniform.

```json
{
  "schema": "wirelink-benchmark-v2",
  "testbed": "github-ubuntu-x64",
  "revision": "<git sha>",
  "environment": {
    "compiler": "gcc (Ubuntu ...) 13.2.0",
    "flags": "-O3 -DNDEBUG",
    "cmake_options": "WIRELINK_BUILD_FRAMING_BENCHMARK=ON;...",
    "cpu": "<model>",
    "affinity": "<pinned cpu list or empty>",
    "governor": "<performance/schedutil/unknown>",
    "wlc": "<version and source digest or empty>",
    "benchmark_source_hash": "<sha256 of the suite's workload sources>"
  },
  "measurements": [
    {
      "suite": "framing",
      "case": "retry3/e0/i2/p2/b512",
      "metric": "cpu_ns",
      "unit": "ns",
      "direction": "lower",
      "samples": [120.1, 121.8, 119.7],
      "median": 120.1
    }
  ]
}
```

Rules:

- `direction` is `lower`, `higher`, or `exact`. `exact` covers storage
  contract bytes and public struct sizes: any change is a finding.
- `samples` holds all repetitions the suite produced; `median` is the gating
  statistic. Percentile metrics (p99) record their estimator in `metric`.
- `benchmark_source_hash` covers the workload's own sources. When it differs
  between two reports, the pair is **incomparable**: exit `2`, verdict
  `incomparable`, no numbers presented as a regression.
- The same incomparability rule applies to `testbed`, `unit`, `direction`,
  case sets, repetition counts, and, for firmware, clock/`hz` and batch
  size. Suites keep enforcing their existing workload invariants.

## Comparison and gates

`comparison.json` carries, per gated metric:

```json
{
  "suite": "framing",
  "case": "retry3/e0/i2/p2/b512",
  "metric": "cpu_ns",
  "direction": "lower",
  "before": 120.1, "after": 126.5,
  "delta_abs": 6.4, "delta_rel": 0.053,
  "threshold": {"rel": 0.10, "abs": 5.0},
  "severity": "warn",
  "status": "pass"
}
```

- `status` is `pass`, `warn`, `fail`, or `incomparable`.
- A timing metric regresses only when **both** the relative threshold and
  the absolute threshold are exceeded, so a 1 ns → 2 ns jump (100%) does not
  fail by itself. The default profile is `relative > 10% AND absolute > 5 ns`.
- Initial threshold values are never guessed. They are
  `max(product-allowed change, 5 × measured noise band)`, where the noise
  band is the MAD-based spread from `bench.py calibrate --runs 20` on the
  gating testbed.
- Gating uses 10–20 sentinel cases per suite. The full matrix runs for
  diagnostics and trends only and never fails a build.
- Severities: `info` (report only), `warn` (annotated, non-blocking), `fail`
  (blocking in layers that declare it).

Threshold classes by metric type:

| Metric type | Rule |
| --- | --- |
| Storage contract, public struct size | `exact`; any change is a finding |
| Deterministic protocol metrics (reliable loss, mixed traffic, owner pass) | exact or very tight relative bound |
| Linked flash/RAM (footprint `--elf`) | absolute bytes **and** percent |
| Host CPU timing | PR: `warn` only; dedicated runner: may `fail` |
| UDP/USB p99 | trend and report; never blocks a PR |
| Firmware cycles | `fail` permitted once frequency and config are fixed |

## Testbeds

A `testbed_id` names one stable measurement environment (machine class +
toolchain + pinned settings), for example `github-ubuntu-x64` or
`esp32s3-devkitc-01`. Reports from different testbeds, toolchains, or clock
configurations are never compared; the comparison exits `2`. Firmware reports
additionally record board identity, because two ESP32-S3 boards are two
testbeds.

## Automation layering

| Layer | Content | Automatic? |
| --- | --- | --- |
| Ordinary push/PR | benchmark tool unit tests only; no measurements | Yes |
| Manual smoke | deterministic models, storage contract, code size | No |
| Manual quick | selected framing/codec/RPC host timing | No |
| Pre-tag release | smoke + quick + API/UDP against the previous tag | No |
| Future HIL | ESP32-S3 cycles, RX backend, USB/CDC/UART transports | No |

Notes:

- `.github/workflows/benchmark.yml` has only `workflow_dispatch`; it never
  consumes runner time for an ordinary push or pull request.
- Its `release` profile covers all eight registered host suites. Hosted-runner
  timing is diagnostic; an authoritative release comparison belongs on the
  same stable machine and toolchain used for its baseline.
- The initial `smoke` preset gives deterministic metrics a zero directional
  regression tolerance: any worsening needs release review, while an
  improvement passes.
- Run the comparison before creating the release tag. See
  [RELEASE.md](RELEASE.md) for the checklist and evidence policy.

## Adoption status

The shared registry, three-state comparisons, reports, calibration, and manual
release workflow are implemented. Automatic push/PR measurements are
deliberately disabled during early development. A unified HIL command and a
dedicated stable runner remain future work. Until a suite is converted, its
`compare.py` keeps working directly and its native output stays authoritative
in `raw/`.
