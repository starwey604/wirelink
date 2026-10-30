# Standalone framing tool benchmark

[中文](README-cn.md).

This opt-in benchmark measures the independent public frame utility. It has no
endpoint, pending calls, transport sink, ACK state, retry engine or WLC dependency.
The default packet kernel does not dispatch through this utility. Historical
reliable DATA/ACK encodings remain available to standalone tools and frozen
conformance vectors; that does not enable reliable endpoint execution.

Current workload `wirelink-framing-v3` contains spare-capacity encoding, exact
encoding, overlapping-input encoding, raw decoding, and COBS plus raw decoding.
It covers the public tool's three envelopes and integrity selectors, payload
patterns and sizes. `send`, `claim`, `busy3`, `retry3` and engine `receive` modes
were removed with the old engine. Packet ownership/BUSY/stop contracts now run
in the kernel and adapter tests; the dedicated static-kernel USB benchmark
measures current RPC/Sample work. v1/v2 parsers remain for archived reports and
reject comparisons against the changed v3 workload.

Supply Google Benchmark 1.9+ or a local source checkout; CMake downloads nothing:

```sh
cmake -S . -B build/framing -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_EXAMPLES=OFF -DWIRELINK_BUILD_FRAMING_BENCHMARK=ON \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/path/to/google-benchmark
cmake --build build/framing --parallel
ctest --test-dir build/framing --output-on-failure
build/framing/benchmarks/framing/wirelink_framing_benchmark \
  '--benchmark_filter=^(spare|exact|overlap)/e0/i1/p[012]/b(32|120|512|2048)$' \
  --benchmark_min_time=0.01s --benchmark_repetitions=5 \
  --benchmark_out=before.json --benchmark_out_format=json
python3 benchmarks/stamp.py before.json benchmarks/framing/workload.c \
  benchmarks/framing/workload.h benchmarks/framing/micro.cpp
python3 benchmarks/framing/compare.py before.json after.json
```

Freeze each binary and stamp its capture before changing sources. Record source,
compiler, flags, machine and binary hash. Comparison checks workload, fixture
layout, source hash, repetition completeness and measurement sets. Engine
`context_bytes` is explicitly zero; `fixture_bytes` describes only the diagnostic
scratch object. No comparison against a previous engine size is implied.

`benchmarks/zephyr/framing` uses the same C workload and emits complete v3
begin/end records with a selected multi-format or COBS/CRC16-only matrix. Each
32-operation timing batch masks interrupts; it is an intrinsic tool diagnostic,
not end-to-end CPU, responsiveness or USB throughput evidence. No hardware
flashing is required for the pure framing tests. Independent bitwise CRC/header
and alias/capacity vectors remain in `tests/zephyr/unit/framing_workload`.
