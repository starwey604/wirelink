# Fixed-contract kernel API diagnostic

[中文](README-cn.md).

The current benchmark runs the real generated four-call/four-reply kernel with
both RPC roles and ordinary Sample DATA. The C fixture owns both kernels,
packet ports, C11 atomics and RAM loopback storage. C++ only drives Google
Benchmark through an opaque C function interface. Caller-supplied manual ticks
advance the kernels; there are no hidden clock reads or RPC tables in a wrapper.

```sh
cmake -S . -B build/api -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_EXAMPLES=OFF -DWIRELINK_BUILD_API_BENCHMARK=ON \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc-0.13 \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/path/to/google-benchmark
cmake --build build/api --parallel
ctest --test-dir build/api --output-on-failure
build/api/benchmarks/api/wirelink_api_benchmark \
  --benchmark_min_time=0.01s --benchmark_repetitions=5 \
  --benchmark_out=after.json --benchmark_out_format=json
python benchmarks/stamp.py after.json benchmarks/api/fixture.c benchmarks/api/fixture.h \
  benchmarks/api/micro.cpp benchmarks/api/kernel_api.wl benchmarks/api/kernel_api.bind.wl
python benchmarks/api/compare_kernel.py before.json after.json
```

The seven cases are two idle kernel steps, eight successful RPCs (four per
direction), eight business rejections, and two ordinary Sample messages with
32/120/512/1024-byte payloads. A measured operation includes admission, source
clearing for RPC, owner steps, actual RAM transfer copies/completion and callback
validation. Sample callbacks validate every payload byte. This is not merely
API submit time, the owning C++ Session workload or physical USB/UART latency.
The C fixture aborts on incorrect results, missing callbacks or ownership errors.

`wirelink-kernel-api-v1` records call/sample counts, generated endpoint bytes and
total fixture bytes (including the diagnostic's own storage). The current
comparator rejects failed work, changed counts, inconsistent layouts, missing
repetitions and workload/machine/source-hash mismatches. CPU thresholds are
optional; resource and wall-time rows remain diagnostic. Preserve binaries,
compiler/link flags, source revisions and hashes before performance comparison.
No improvement is inferred from a single shared-host smoke run.