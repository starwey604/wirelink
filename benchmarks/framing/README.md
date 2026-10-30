# Frame TX/RX and retry CPU benchmark

[中文](README-cn.md).

This opt-in benchmark isolates allocation-free frame/link work. It does not need
WLC, Asio, an executor, or a network. For actual two-process latency and
executor contention use [the API benchmarks](../api/README.md). The
[firmware variant](../zephyr/README.md) reuses the same C workload on the
ESP32-S3.

## Build and run

Install Google Benchmark 1.9+ or pass a local checkout; nothing is downloaded by
this target.

```sh
cmake -S . -B build/framing -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_EXAMPLES=OFF -DWIRELINK_BUILD_FRAMING_BENCHMARK=ON \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/path/to/google-benchmark
cmake --build build/framing --parallel 2
ctest --test-dir build/framing --output-on-failure
build/framing/benchmarks/framing/wirelink_framing_benchmark \
  '--benchmark_filter=^(spare|exact|overlap|send|claim|busy3|retry3)/e[01]/i[02]/p[012]/b(32|120|512|2048)$' \
  --benchmark_min_time=0.01s --benchmark_repetitions=5 \
  --benchmark_enable_random_interleaving=true \
  --benchmark_out=before.json --benchmark_out_format=json
```

Freeze the baseline binary **before** changing core sources. Build the candidate
in a different directory and repeat with `after.json`. Record binary SHA-256,
compiler/version, CMake cache/flags, machine and source diff alongside the JSON.
Do not rebuild the baseline directory after editing the sources.

```sh
python3 benchmarks/stamp.py before.json benchmarks/framing/workload.c \
  benchmarks/framing/workload.h benchmarks/framing/micro.cpp
python3 benchmarks/stamp.py after.json benchmarks/framing/workload.c \
  benchmarks/framing/workload.h benchmarks/framing/micro.cpp
python3 benchmarks/framing/compare.py before.json after.json
python3 benchmarks/framing/test_compare.py
```

Stamp each capture immediately after it is written and **before** editing
workload sources: `benchmark_source_hash` records the benchmark code that
produced that report, and the comparison treats reports stamped from
different workload sources as incomparable (exit 2), never as a regression.

The comparison rejects failed workloads, missing/duplicate repetitions, changed
sink counts, context size or measurement sets. It reports median CPU and wall
nanoseconds per operation. Compiler/flags must also be checked separately; JSON
does not contain a complete build identity. `--max-regression-percent 10` is an
optional CPU gate for a dedicated runner, **not** a shared-runner default.
Use longer, alternating A/B/B/A captures to investigate apparent regressions.
If the host is running other CPU-intensive tasks, defer performance acceptance;
correctness smoke can still run, but its timings are not performance evidence.

## Workloads and interpretation

| Mode | One measured operation |
| --- | --- |
| `spare` | Encode one frame into worst-case-sized, disjoint storage |
| `exact` | Encode into exactly the independently calculated encoded length |
| `overlap` | Copy input into the output region, then encode with overlap |
| `send` | Copy-based unreliable send and completion poll |
| `claim` | Claim, copy bytes into the claim, commit and completion poll |
| `busy3` | Unreliable send, three BUSY results, then synchronous SENT |
| `retry3` | Reliable send, three ACK-timeout retries, then cancel/take cleanup |
| `raw_decode` | Fully parse and integrity-check an existing raw frame |
| `cobs_decode` | Copy immutable encoded input, then decode COBS in place and validate the raw frame |
| `receive` | Release the previous event lease, feed, poll and retain the current event for checking outside timing |

`e0/e1/e2` select COBS/native packet/length16. `i0/i1/i2` select
NONE/CRC16/CRC32C. `p0/p1/p2` select all-zero/nonzero/zero-every-17-byte
payloads. `bN` is payload size, excluding link headers and integrity bytes. The
v2 full host smoke matrix has 1,404 groups, including empty payloads and 254-byte
boundaries.
`raw_decode` and `cobs_decode` register only `e0` to avoid duplicate raw workloads.

Tight buffers can still use the fast path when their exact size equals the
worst-case bound. `claim` includes a setup copy, so it is not a direct-codec
zero-copy measurement. `busy3` and `retry3` each make four sink submissions;
they measure work, **not** network RTT, loss probability, or waiting time.
The clock is simulated; no sleep, adapter or real I/O occurs inside an operation.

Initialization, warmup and final byte checking are outside Google Benchmark's
timed loop. The oracle independently constructs the Compact-v1 header and uses
standalone CRC/COBS tools. Per-operation return/event checks and a constant-time counting sink
are included. Protocol unit tests additionally check every retry's bytes,
actual encoding call count, I/O token freshness and ACK storage isolation.

`busy3` explicitly notifies writable after each BUSY result. RX payload comparisons
remain outside timing; the `cobs_decode` setup copy is timed, while `receive`
includes the actual ring/fallback and event lease lifecycle. The independent
`tests/zephyr/unit/framing_workload` suite checks every workload without Google
Benchmark and cross-checks bytes with bitwise CRC references.

Both host and Zephyr accept `-DWIRELINK_FRAMING_COBS_CRC16_ONLY=ON` to limit the
diagnostic matrix. This does not specialize the core. The v2 Zephyr `matrix`
field distinguishes the full 936-group/4,680-sample matrix from the filtered
120-group/600-sample matrix. The parser also reads historical v1 reports, but
rejects comparisons across workload versions, matrices or source hashes.

For sanitizer smoke, configure a separate Clang build with
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and the
same `CMAKE_CXX_FLAGS`. Never compare its times with Release results.
`WIRELINK_BUILD_FUZZERS=ON` adds `wirelink_fuzz_frame_encode`, which checks all
three envelopes over arbitrary payloads, capacities, aliases and output
alignment. COBS uses the standalone encoder as an independent oracle; packet
envelopes compare overlapping/unaligned output against a disjoint native
reference.
