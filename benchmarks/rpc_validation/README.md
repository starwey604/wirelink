# RPC validation work

Opt-in CPU regression workload, shared by Google Benchmark and a standalone H7
Zephyr app. No transport, allocation, timing probes or benchmark dependencies
are added to production. Use the [API benchmark](../api/README.md) separately
for complete RPC/UDP measurements.

## Build and run on a host

Supply a matching development WLC (ABI 29) and Google Benchmark 1.9+:

```sh
cmake -S . -B build/rpc-validation -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_EXAMPLES=OFF -DWIRELINK_BUILD_RPC_VALIDATION_BENCHMARK=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/absolute/path/to/google-benchmark
cmake --build build/rpc-validation --parallel
ctest --test-dir build/rpc-validation --output-on-failure
build/rpc-validation/benchmarks/rpc_validation/wirelink_rpc_validation_benchmark \
  --benchmark_min_time=0.05s --benchmark_repetitions=7 \
  --benchmark_out=after.json --benchmark_out_format=json
python3 benchmarks/rpc_validation/compare.py before.json after.json
```

Use an idle host, identical compiler/options and separate frozen binaries. Run
A/B/B/A; do not build or run other benchmarks during timing. CPU time and wall
time are reported independently. Smoke tests check correctness, not speed; no
fixed timing gate is imposed on shared CI machines.

To reproduce the old implementation, generate `validation.wl` with a preserved
ABI 28 compiler. Set `RPC_VALIDATION_CODEC_DIR` to that frozen output directory
and `RPC_VALIDATION_CODEGEN_ABI=28`; the benchmark will not regenerate it.
Keep the old and new codec artifacts and binaries separately with SHA-256 hashes.

## Matrix and interpretation

There are 65 groups, each with an independent canonical-byte/hash oracle:

- `k0`: string/bytes capacity 32, actual lengths 0/16/32.
- `k1`: string/bytes capacity 512, lengths 0/16/32/256/512.
- `k2`: nested k1 plus 128 packed float32 values, same actual lengths as k1.
- `decode`: public view decode; `canonical`: old canonical encode + seeded hash,
  or the new private canonical sink; `convert`: public checked view-to-owned.
- `pipeline`: decode + canonical fingerprint + ordinary-handler owned conversion.
  This does **not** include cache lookup, handler execution, link I/O or an RPC RTT.
- `owned_decode`: public owned decode, including its internal temporary view.

`b` is the length of **each** string and byte field, not total payload length.
The host `encoded_bytes` counter records the total. The v2 workload gives both
implementations the precomputed RPC domain seed and fingerprints before copying
owned values. Do not compare v1 exploratory results against v2.

The private functions are used here only to isolate generated-internal work.
Applications must keep using public owned APIs. Full outer owned zeroing remains;
only duplicate validation/clearing and canonical byte storage are removed.

## H7

Build `benchmarks/zephyr/rpc_validation` twice from an initialized Zephyr workspace
with the same board, SDK, module list and flags. Pass the frozen codec directory
and corresponding ABI via CMake as above. Board roots and modules are installation
specific; the local validation uses `dm_mc02` / STM32H723ZG.

The app records DWT cycles at the reported timing frequency. It warms each group,
then takes 5 batches of 32 operations with interrupts masked **only inside the
test batch**. Assertions, RTT output and sleeps are outside timing. This is a
hot-cache CPU microbenchmark, not production scheduling or total firmware CPU.

Use an active J-Link Commander RTT server and
`benchmarks/zephyr/willow_cpu/capture.py` to capture raw boot-to-pass output.
Start capturing before running the reset core; do not attach a second probe.
Then compare two complete logs with `compare.py before.log after.log`. The parser
requires all 65 groups × 5 repetitions, one begin/end and `result=pass`; it rejects
missing/duplicate rows, resets, errors and mismatched versions or frequency.

Run `python3 benchmarks/rpc_validation/test_compare.py` for parser contracts.
Measured evidence and limitations: [中文记录](../../docs/rpc-validation-performance-cn.md).
