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
| [zephyr/rx_backend](zephyr/rx_backend/README.md) | RX ring backend behavior | ESP32-S3 |

The firmware variants under [`benchmarks/zephyr/`](zephyr/README.md) run the
same workloads on the ESP32-S3 DevKitC and must use the same frozen generated
artifacts on both sides.

How to reproduce each result is documented here. Published, machine-specific
numbers live on the
[documentation site](https://docs.silkenkite.ink/wirelink/reference/benchmarks/)
and are refreshed by hand after a real measurement, not committed per run.
