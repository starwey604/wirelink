# Benchmarks

[中文](README-cn.md).

Opt-in, reproducible measurements for Wirelink. None of them are correctness
gates, none of their dependencies enter the core, and none of the numbers are
performance guarantees. Each directory has one short guide: what it measures,
how to run it, and what it does not measure.

Firmware benchmarks target the ESP32-S3 DevKitC. The `native_sim` and QEMU runs
in the test matrix exist for correctness, not for timing.

| Benchmark | Measures | Runs on |
| --- | --- | --- |
| [api](api/README.md) | Generated-endpoint CPU and memory, loopback RPC, two-process UDP latency | Host |
| [api/EXECUTOR.md](api/EXECUTOR.md) | Multi-producer executor handoff, wakeups and LATEST replacement | Host |
| [rpc_validation](rpc_validation/README.md) | Decode, canonical fingerprint and owned-conversion cost | Host, ESP32-S3 |
| [codec_plan](codec_plan/README.md) | Field lookup and packed-array codec entry cost | Host, ESP32-S3 |
| [mixed_traffic](mixed_traffic/README.md) | Telemetry age and RPC completion under ACK loss and backpressure | Host, ESP32-S3 |
| [framing](framing/README.md) | Single-pass COBS and encoded-retry reuse | Host, ESP32-S3 |
| [owner_pass](owner_pass/README.md) | Bounded dispatch, empty checks and handoffs, by counting rather than timing | Host |
| [rpc_peer](rpc_peer/README.md) | The generated steady-session guard before a reliable RPC request | Host |
| [bulk](../docs/bulk-performance.md) | The sequential bulk state machine against a matched raw sink | Host |
| [fifo](../docs/fifo-performance.md) | The ordered SPSC FIFO runtime | Host |
| [footprint](footprint/README.md) | Static code size per module and the caller storage contract | Host |
| [reliable_loss](reliable_loss/README.md) | Reliable-path convergence, attempts and failures under packet loss | Host |
| [zephyr/rx_backend](zephyr/rx_backend/README.md) | RX ring backend behavior | ESP32-S3 |

The firmware variants under [`benchmarks/zephyr/`](zephyr/README.md) run the
same workloads on the ESP32-S3 DevKitC and must use the same frozen generated
artifacts on both sides.

## Unified entry

[`bench.py`](bench.py) reads the suite registry and presets in
[`presets.toml`](presets.toml) (deterministic `smoke`, sentinel `quick`,
diagnostic `full`); `pair` and `calibrate` land per [SPEC](SPEC.md). The
available commands are:

```sh
python3 benchmarks/bench.py list      # suites and presets
python3 benchmarks/bench.py doctor    # required tools for a preset (exit 2 if one is missing)
python3 benchmarks/bench.py run --preset smoke   # build, execute, collect into out/<run-id>/
python3 benchmarks/bench.py report out/<run-id>  # normalize raw/ into normalized.json (idempotent)
python3 benchmarks/bench.py pair --suite footprint --before out/a --after out/b  # exit 0/1/2
python3 benchmarks/bench.py pair --preset smoke --base origin/main --candidate . # build+alternate+compare
python3 benchmarks/bench.py calibrate --suite footprint --runs 10  # median/MAD noise floors
```

`doctor` gates against `--preset smoke` by default: Python 3.11+, CMake ≥
3.21, a C compiler, `size` and WLC. Timing suites additionally need Google
Benchmark (`--preset quick`/`--preset full`). A run directory is created
exclusively and never overwritten: it holds `manifest.json` plus the
authoritative `raw/`, and `report` rebuilds the derived `normalized.json`
from `raw/` alone. `pair` writes `comparison.json`, `summary.md` and
`junit.xml` (into the `--after` directory, or into `--out` for orchestrated
runs) with the three-state exit code; `--base`/`--candidate` builds both
sides in isolated git worktrees and alternates them (A/B/B/A by default),
while `--before`/`--after` re-compares existing runs without re-measuring.
`calibrate` repeats the same suites on one machine and writes
`calibration.json` (per-metric median, MAD, and the 5×MAD noise floor);
recorded thresholds must be `max(product allowance, noise floor)`. Performance
measurements do not run on ordinary pushes or pull requests. The manual
`benchmark.yml` workflow offers `smoke`, `quick`, and pre-tag `release`
profiles; see the [release benchmark checklist](RELEASE.md). The per-suite
guides below stay authoritative for manual runs.

How to reproduce each result is documented here. Published, machine-specific
numbers live on the
[documentation site](https://docs.silkenkite.ink/wirelink/reference/benchmarks/)
and are refreshed by hand after a real measurement, not committed per run.
