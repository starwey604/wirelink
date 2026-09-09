# Generated API performance regression

[中文](README-cn.md). Use Google Benchmark for single-thread CPU/loopback work,
and two real UDP processes for end-to-end latency. Neither predicts H7 CPU time.
Zephyr native_sim remains a behavior gate, not the microbenchmark's clock.
For real multi-producer executor handoff and lock probes, see
[EXECUTOR-cn.md](EXECUTOR-cn.md); H7 measurements have a separate
[test-only harness](../zephyr/willow_cpu/README-cn.md).

Enable `WIRELINK_BUILD_API_BENCHMARKS=ON` in a Release build. Supply matching WLC
ABI 28, standalone Asio, Python 3 and either an installed Google Benchmark 1.9+
CMake package or `WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR`. Tested source: v1.9.5,
commit `192ef10025eb2c4cdd392bc502f0c852196baa48`. Nothing is downloaded automatically
and the benchmark dependencies are not core dependencies.

```sh
cmake -S . -B build/api-bench -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_API_BENCHMARKS=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/absolute/path/to/google-benchmark \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include
cmake --build build/api-bench --config Release --parallel 2
ctest --test-dir build/api-bench -C Release -L benchmark --output-on-failure
python benchmarks/api/run.py --bin-dir build/api-bench/benchmarks/api --out before.json
# Modify, regenerate and rebuild, then:
python benchmarks/api/run.py --bin-dir build/api-bench/benchmarks/api --out after.json
python benchmarks/api/compare.py before.json after.json
```

The matrix covers 1/12/64 declared services, 1/4 slots, reliable/unreliable RPC,
0–1024-byte bodies, idle/initialization, four-call batches and 512-byte telemetry.
All services have the same bounded shapes; Echo is the hot service. This measures
the cost of additional definitions, not execution of 64 business handlers.
`Rpc/N/B` reports time **per B-call batch**, including validation and TX retirement.
Endpoint and arena bytes are host-ABI static sizes, not process RSS or MCU RAM.

UDP reports per-round p50/p95/p99/max, throughput, and separate client/server
process CPU per call. It uses real ordinary sync RPC, localhost sockets, readiness
waiting, warmup and payload verification, without an artificial millisecond sleep.
Server measurement uses start/stop RPC markers and includes minor boundary work;
its window is not identical to the client's. Python only orchestrates processes.

Defaults: `--repetitions 5 --min-time 0.05 --samples 2000 --warmup 200 --udp-rounds 3`.
Use `--skip-udp` or `--skip-micro` to isolate a layer. Output files are never
overwritten; omitting `--out` performs a non-persistent smoke run. Direct binaries
also accept Google Benchmark filters and reporting options.

Comparison uses medians of repetitions and rejects mismatched environments,
build flags, workloads, missing cases and invalid measurements. Comparing max
means the median of round maxima, not a global maximum. Timing gates are opt-in:
`--max-regression-percent 10` gates micro CPU only on a stable dedicated runner.
Shared CI should use correctness smoke tests; frequency scaling, ASLR and other
processes introduce noise. Source is cross-platform; this iteration ran on Linux.

See the [implementation/evidence record](../../docs/api-performance-progress-cn.md)
and the [Google Benchmark timing guide](https://google.github.io/benchmark/user_guide.html#cpu-timers).
