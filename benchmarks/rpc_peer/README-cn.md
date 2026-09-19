# RPC peer 快路径基准

[English](README.md)。这个主机微基准单独隔离可靠 RPC 请求前生成的 steady-session 检查。
它对比原先无条件的 `wl_rpc_peer_observe()` 调用与内联 session 守卫。
它不预测端到端链路延迟；用来判断被省掉的调用在跑固件 CPU 遥测门槛前是否仍然显著。

```sh
cmake -S . -B build/rpc-peer -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_RPC_PEER_BENCHMARK=ON
cmake --build build/rpc-peer --target wirelink_rpc_peer_benchmark
./build/rpc-peer/benchmarks/rpc_peer/wirelink_rpc_peer_benchmark
```
