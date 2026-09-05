# RPC Peer Fast-Path Benchmark

This host microbenchmark isolates the steady-session check generated before a
reliable RPC request. It compares the former unconditional
`wl_rpc_peer_observe()` call with the inline session guard. It does not predict
end-to-end H7 latency; use it to detect whether the avoided call remains
material before running the firmware CPU telemetry gate.

```sh
cmake -S . -B build/rpc-peer -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_RPC_PEER_BENCHMARK=ON
cmake --build build/rpc-peer --target wirelink_rpc_peer_benchmark
./build/rpc-peer/benchmarks/rpc_peer/wirelink_rpc_peer_benchmark
```
