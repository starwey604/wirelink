# 顺序 Bulk Runtime 性能测试方法

> 英文版 [`bulk-performance.md`](bulk-performance.md) 是规范来源。

host bulk benchmark 在脱离 transport 的条件下测量 Wirelink 无动态分配的顺序
sender/receiver 状态机。它用于 CPU 和状态机回归，不代表 USB、UDP、flash 或端到端
latency；物理 transport 必须保留自己的硬件 benchmark。

```sh
cmake -S . -B build/bulk-bench -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_BULK_BENCHMARK=ON \
  -DWIRELINK_BUILD_EXAMPLES=OFF
cmake --build build/bulk-bench --target wirelink_bulk_benchmark
build/bulk-bench/benchmarks/bulk/wirelink_bulk_benchmark \
  --bytes 4194304 --iterations 8 --latency-iterations 2 --warmups 1
```

默认 fixture 使用可重复的 4 MiB source 和等大 sink。heap 只在 benchmark 启动时分配；
bulk runtime 本身不用 heap。每次测量执行完整 `Begin -> Chunk x N -> End`/Status 流程，
receiver 把借用的 Chunk 直接写入调用方 sink。chunk 为 256、512、1024、2016 bytes；
2016 为 2048-byte v1 payload ceiling 预留 32 bytes 字段和长度前缀，是该 fixture 的
安全上限，不承诺未来 schema 固定只有 32 bytes overhead。

raw baseline 对同一 sink 执行同样大小的 `memcpy`，并对完整对象计算同样的 CRC32C。
volatile checksum 和每轮 `memcmp` 防止编译器删除工作，也避免只向 runtime 收取完整性
成本。

指标含义：

- `raw_goodput_mib_s`/`runtime_goodput_mib_s` 使用 monotonic wall time，只计算应用字节；
- `runtime_cpu_ns_per_byte` 使用 process CPU time，包含状态转换、callback、copy、统计和
  最终 CRC32C；
- `goodput_ratio_pct`/`cpu_overhead_pct` 对比匹配的 raw sink；
- `status_state_p50_ns`/`p99_ns` 来自独立插桩轮次，覆盖本地提交、receiver 处理、Status
  acquire、sender 处理和 release，不含编码、transport、调度与硬件 latency；
- 无故障 fixture 的 retry/BUSY 计数必须为零。

空闲机器至少运行五次，记录 CPU、compiler、优化参数、CPU affinity 和 governor。
吞吐/CPU 取中位数，p99 保留最差值。程序会强制检查正确性、至少 1 MiB 对象和至少两轮
测量。

同 host Release 初始门限为：达到匹配 raw goodput 的 90%，额外 CPU ns/byte 不超过
10%。性能 miss 会打印 `missed` 但不会伪装成 correctness failure；必须记录并调查，
不得改写为通过。stop-and-wait 每个 Chunk 都有一次 Status，因而门限主要保护
1024-byte 和最大安全 chunk；小 chunk 用于观察未来 bounded window 可摊销的固定成本。

## 初始 Host 记录（2026-08-31）

环境：Intel Core 5 315（6 cores）、Linux x86-64、Release、4 MiB、8 个 measured
iterations、2 个 latency iterations、1 次 warm-up，`taskset` 固定 CPU 2，frequency
scaling 开启。GCC 16.2.1、Clang 22.1.8。除 worst p99 外均取五次中位数。

| Compiler | Chunk | Raw MiB/s | Runtime MiB/s | Goodput ratio | Runtime ns/byte | CPU overhead | Status p50 | Worst p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GCC | 256 | 347.75 | 337.47 | 98.40% | 2.7271 | 1.88% | 49 ns | 256 ns |
| GCC | 512 | 340.78 | 339.97 | 99.76% | 2.7086 | 0.70% | 60 ns | 286 ns |
| GCC | 1024 | 343.47 | 328.96 | 94.45% | 2.7860 | 4.34% | 267 ns | 691 ns |
| GCC | 2016 | 341.69 | 343.53 | 99.01% | 2.7374 | 1.40% | 450 ns | 1,101 ns |
| Clang | 256 | 345.83 | 332.30 | 97.57% | 2.7248 | 1.91% | 51 ns | 183 ns |
| Clang | 512 | 339.66 | 333.07 | 98.56% | 2.7093 | 1.67% | 64 ns | 333 ns |
| Clang | 1024 | 342.44 | 332.97 | 98.37% | 2.7637 | 2.76% | 251 ns | 637 ns |
| Clang | 2016 | 339.72 | 336.10 | 99.20% | 2.7293 | 1.28% | 407 ns | 2,006 ns |

所有中位数都满足门限，且每轮 retry/BUSY 为零、字节与 CRC32C 精确。一次受调度/频率
影响的 GCC 1024-byte 离群轮次只有 88.10% goodput、14.31% CPU overhead，特意保留以
说明门限不是无固定 governor 时的确定性承诺。约 2.7 ns/byte 主要来自两边都执行的
table-driven whole-object CRC32C；两者小差值才是围绕真实 integrity sink 的状态机成本。
