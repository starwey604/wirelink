# 可靠 RPC / 遥测共存

[English](README.md)。这是一个确定性的 **协议时间** 回归，不是 CPU 基准，也不预测
UART、USB 或 OS 调度延迟。它用假时钟和串行化异步 transport 跑真实生成端点、owned async RPC、
retained 遥测和核心。不需要 socket、线程、sleep 或 Google Benchmark 依赖。

`benchmarks/zephyr/mixed_traffic` 下的固件变体复用同一负载，并加入真实计时测量，
包括缓存重传与重建重传。把模型跑在板子上不会把它的年龄变成 USB/UART 延迟。

## 运行

先独立构建配套 WLC，再让 CMake 指向它：

```sh
cmake -S benchmarks/mixed_traffic -B build/mixed-traffic \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc/target/release/wlc
cmake --build build/mixed-traffic --parallel 2
ctest --test-dir build/mixed-traffic --output-on-failure
build/mixed-traffic/wirelink_mixed_traffic --report > after.jsonl
python3 benchmarks/mixed_traffic/compare.py before.jsonl after.jsonl
```

`--report` 仍要求完整正确的 RPC 结果、每次调用恰好一次业务执行、已触发的故障注入和不可变的 in-flight 字节。
`--check` 另外按场景的物理背压、采样间隔和串行化余量约束遥测年龄/间隔。
这个门槛会让改动前的核心失败；它不是百分比计时阈值。

为了不替换工作树就对比另一份核心，用 `-DWIRELINK_SOURCE_DIR=/path/to/exported/wirelink` 配置独立构建。
harness/schema 来自本目录；链接的核心来自所选源码树。两侧使用同一 WLC 和 harness。
记录源码 revision/diff 和二进制哈希；`compare.py` 在打印最大值前校验全部 42 条 JSONL 记录。
不要用 candidate 源码覆盖之前冻结的基线二进制。

## 负载与指标

每个 case 跑 2,400 个 1 ms owner tick、默认 16 事件的端点 pass，以及三种 CRC32C 帧封装。
两个方向每 5 ms 产出一个样本。RPC case 在 tick 100/500/900/1300/1700 各方向发起五次调用，
预算 1,500 ms，ACK 超时 100 ms，重试四次。

| Case | 故障/负载 |
| --- | --- |
| `telemetry_only` | 无 RPC；1 ms 物理 I/O |
| `rpc_clean` | RPC 加遥测，无故障 |
| `single_ack` | 每个方向只丢第一个 ACK |
| `drop_ack` | 每条可靠序列丢前两个 ACK 副本 |
| `backpressure` | 每 250 ms 中有一个 20 ms 窗口返回 BUSY |
| `drop_ack_backpressure` | 组合周期性 ACK 丢失和 BUSY 窗口 |
| `saturated_wrap` | 组合故障，1 ms 采样、3 ms I/O、step 前尝试遥测、回绕时钟 |

应用只保留一个最新未发送样本，每个采样 tick 替换。核心接受一次 BUSY 提交后，
该帧已归 transport 路径所有，不会被替换；有限背压仍然可见。

- `max_age_ms`：接收端最后一个应用值在**每个 tick** 的年龄，包括没有新包的 tick。读取 mailbox 不重置它。
- `max_arrival_age_ms`：只在新值到达时计的年龄。单看它会漏掉「长时间中断后又来一个新包」。
- `max_gap_ms`：相邻更新之间的最大间隔。年龄指标还覆盖延伸到观察窗口末尾的中断。
- `mean_age_ms`：观察到的值年龄之和除以全部 2,400 个 tick；首次接收前的 tick 计零。
- `max_rpc_ms` / `rpc_completed`：最差调用完成时间和两个方向的正确调用总数。
  它们重复出现在每条 receiver 记录里，不是两组要相加的调用。

永久 BUSY、owner 停转、RX 消费过载和串行化/唤醒行为不同的真实介质不在本模型内。
公平访问不代表遥测不会占用带宽或延迟 RPC。

## Zephyr

同一负载也是 Ztest 集成 app，位于 `tests/zephyr/integration/mixed_traffic`。
从已初始化的 Zephyr workspace：

```sh
west twister -T /path/to/wirelink/tests/zephyr/integration/mixed_traffic \
  -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -j 2 \
  --extra-args=WIRELINK_WLC_EXECUTABLE=/path/to/wlc/target/release/wlc
```

按 workspace 需要隔离无关模块。这些运行用模拟协议时间验证 target 工具链行为；
其墙时和打印的年龄不是固件 CPU 测量。
