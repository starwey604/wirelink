# API 性能回归：CPU、内存与 UDP 延迟

这套基准测量真实生成端点和 C 核心，供修改前后快速对照，不是 H7 性能模拟器。
[English](README.md)。业务可读性示例仍在 [03_device_service](../../examples/03_device_service/README-cn.md)。

## 为什么分两层

- **Google Benchmark + 内存 loopback**：同一线程推进两个真实端点，测 CPU/墙时、
  RPC 吞吐、端点大小、arena 大小及协议时钟读取次数。没有 socket 和 OS 等待。
  [Google Benchmark](https://google.github.io/benchmark/user_guide.html#cpu-timers)
  提供计时、重复采样和 JSON 输出，不另造一套微基准计时器。
- **两个独立 UDP 进程**：使用普通 `_sync` API 与真实 Asio 适配器，测单次 RPC 的
  p50/p95/p99/max、吞吐，以及两端各自的进程 CPU/调用。不用 Python 模拟协议对端。

Zephyr `native_sim` 保留作行为回归；其[模拟时间与主机时间解耦](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html#about-time-in-native-sim)，
不能把 `k_uptime` 的差值当作 MCU 执行耗时。本机基准也不能代替 H7 cycle/线程 CPU 测量。

## 构建

此功能默认关闭；Wirelink 核心不依赖 Google Benchmark、Python 或 Asio。
需要配套 **WLC ABI 28**、C11/C++20 编译器、CMake、Python 3 和 standalone Asio。
Google Benchmark 可使用已安装的 CMake package（1.9+），或独立源码；本轮验证的是
`v1.9.5`（提交 `192ef10025eb2c4cdd392bc502f0c852196baa48`）：

```sh
git clone --depth 1 --branch v1.9.5 https://github.com/google/benchmark.git google-benchmark
cmake -S . -B build/api-bench -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_API_BENCHMARKS=ON \
  -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/absolute/path/to/google-benchmark \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include
cmake --build build/api-bench --config Release --parallel 2
```

使用安装包时省略 `WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR`，必要时传 `CMAKE_PREFIX_PATH`。
不会自动下载 benchmark 依赖。Windows 多配置生成器使用 Release；下方脚本也会查找 `.exe`
和 `Release/` 目录。其他平台的源码路径相同，本轮实际测量仅在 Linux 运行。

## 快速检查和正式对照

```sh
# 正确性 smoke，不设置性能阈值；可重复运行。
ctest --test-dir build/api-bench -C Release -L benchmark --output-on-failure

# 在修改前运行；默认 5 次微基准重复、每组 UDP 3 轮。
python benchmarks/api/run.py --bin-dir build/api-bench/benchmarks/api --out before.json

# 修改、重新生成和构建后运行；已有结果文件不会被覆盖。
python benchmarks/api/run.py --bin-dir build/api-bench/benchmarks/api --out after.json
python benchmarks/api/compare.py before.json after.json
```

`--skip-udp` 只测 CPU；`--skip-micro` 只测 UDP。
可覆盖 `--repetitions 5 --min-time 0.05 --samples 2000 --warmup 200 --udp-rounds 3`。
只定位一个用例时直接运行某个可执行文件，使用 Google Benchmark 的 `--benchmark_filter`。
汇总脚本默认跑完整矩阵，保留原始重复样本、工具环境、构建参数与可执行文件 SHA-256。

对比工具比较重复样本的中位数，检查机器、编译器、构建参数和用例集合一致，拒绝失败/缺失/
非有限数值。ABI 和二进制哈希允许不同，因为它们正是对比对象。
自有稳定 runner 可以选择 `--max-regression-percent 10`，仅对微基准 CPU 时间启用门槛。
共享 CI 默认只跑 smoke；CPU 调频、其他进程、ASLR 和调度都会影响计时，不能随意建立硬阈值。
更换机器或编译参数应重建基线，而不是长期用 `--allow-context-mismatch` 忽略差异。

## 工作负载与计量边界

| 维度 | 覆盖 |
| --- | --- |
| 声明服务数 | 1、12、64；主要调用同一个 Echo，观察未调用服务带来的成本 |
| RPC 槽 | 12 服务分别构建 1 槽/4 槽；服务数不等于并发数 |
| delivery | 可靠双向；另有 12 服务/4 槽的不可靠双向变体 |
| 请求/响应 body | 0、32、512、1024 B；schema 上限均为 1024 B |
| 热路径 | client/server idle step、初始化+关闭、RPC、4 调用批次、512 B 遥测 |

`Rpc/N/B` 的一个 op 是 **B 次 RPC 的批次**，CPU ns/op 不要直接当成 ns/单次调用。
loopback RPC 包含请求快照、编解码、回调结果校验，以及额外两轮 ACK/TX 资源排空。
协议时钟固定为测试时间，只用于超时规则；Google Benchmark 使用独立的真实计时器。
注册服务用例不逐个运行 64 种业务，功能扩展验收另见 03 示例。

UDP 为可靠 Echo、单在途饱和负载，只绑定 localhost；不混入固定的 1 ms 业务 sleep。
预热和控制调用不进入客户端样本；计时区内仍包含必要的正确性校验与计时开销，不事后扣除。
服务端通过开始/结束 RPC 标记 CPU 窗口，包含少量边界处理，与客户端窗口不是完全同一段。
`max` 对比是各轮最大值的中位数，原始每轮 max 也保留；不能把它当跨轮总体最大值。
丢包/乱序语义由已有 UDP 故障测试覆盖，不把这些基准宣称为受控网络故障实验。

内存数字是该主机 ABI 下的 `sizeof(endpoint)` 与静态 arena，不是进程 RSS，也不是固件 RAM。
最新实现和实测结果见 [ABI 28 性能记录](../../docs/api-performance-progress-cn.md)。
多生产者/唤醒争用使用另一个 [executor 矩阵](EXECUTOR-cn.md)，固件 CPU 使用
[H7 测试包装应用](../zephyr/willow_cpu/README-cn.md)。
结果及保留锁的依据见 [争用与 H7 CPU 记录](../../docs/executor-h7-performance-cn.md)。
