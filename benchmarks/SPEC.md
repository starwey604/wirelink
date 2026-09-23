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
python3 benchmarks/bench.py hil --preset <name> --flash-port P --console-port P
```

- `run` configures CMake from the preset, builds, executes, and normalizes.
- `pair` compares two runs with each suite's own comparator and writes
  `comparison.json`, `summary.md`, and `junit.xml`. With `--base`/`--candidate`
  it builds both sides in isolated source trees and alternates them (default
  A/B/B/A, keeping the worse status per metric across brackets); with
  `--before`/`--after` it re-compares existing run directories without
  re-measuring (idempotent, outputs land in the `--after` directory).
- `report` re-derives `normalized.json` from `raw/` without re-measuring
  (idempotent); the comparison outputs are re-derived by `pair`.
- `calibrate` repeats one baseline on one testbed and emits the median/MAD
  noise band that threshold values must be derived from.
- `hil` builds, flashes, resets, captures, and validates firmware suites.

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

## CI layering

| Layer | Content | Blocks a PR? |
| --- | --- | --- |
| PR deterministic | compare-parser tests, mixed traffic, reliable loss, owner pass, storage contract, code size | Yes |
| PR quick timing | selected framing/codec/RPC sentinels, base/head alternating on one runner | Initially warn-only |
| Nightly host | full Google Benchmark, API UDP, executor, bulk, FIFO | May block on a dedicated runner |
| Nightly/manual HIL | ESP32-S3 cycles, RX backend, USB/CDC/UART transports | Blocks release, not ordinary PRs |
| Release | full host + HIL, archiving ELF, map, config, raw data | Yes |

Notes:

- Timing layers start as `continue-on-error` annotations and are promoted to
  blocking only after `calibrate` has been run on the same runner and the
  sentinel thresholds are in `presets.toml`. Regular CI without dedicated
  hardware keeps timing suites warn-only: only dry-run/validate them.
- The PR deterministic and quick-timing jobs run on Ubuntu only; they are not
  multiplied across the Linux/macOS/Windows host matrix.
- HIL runs on a self-hosted runner with a hardware map and never gates an
  ordinary PR.

## Adoption status

The contract above is staged: exit codes, `--json-out`/`--markdown-out`, and
gates land suite by suite first; `bench.py` and `presets.toml` provide the
shared registry with `list`/`doctor`/`run`/`report`/`pair`/`calibrate`, and
`.github/workflows/benchmark.yml` runs the PR layers — the deterministic
smoke gate blocks, quick timing stays advisory until its thresholds are
calibrated. Nightly and HIL layers follow. Until a suite is converted, its
`compare.py` keeps working directly and its native output stays
authoritative in `raw/`.
