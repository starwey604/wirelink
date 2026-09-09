# Executor 多生产者性能回归

本基准单独量化线程之间的提交、唤醒、结果通知与 LATEST 替换。
普通 [API 基准](README-cn.md)仍用于编解码/端点 CPU 和 UDP RTT，不能代替这里的争用测量。

## 构建与运行

沿用 API 基准的 WLC、Asio、Google Benchmark 参数，增加
`-DWIRELINK_BUILD_EXECUTOR_BENCHMARK=ON`。它启用 host runtime，额外生成
12 服务/8 槽端点；不会改变已有 1/4 槽基线。

分别配置两个 Release 构建目录：

- OFF：`-DWIRELINK_HOST_PROFILING=OFF -DWIRELINK_HOST_LOCK_PROFILING=OFF`，用于真实 CPU/延迟。
- LOCKS：`-DWIRELINK_HOST_PROFILING=OFF -DWIRELINK_HOST_LOCK_PROFILING=ON`，用于定位锁和唤醒。

```sh
cmake --build build/executor-off --target wirelink_executor_benchmark --parallel 2
ctest --test-dir build/executor-off -R '^executor_benchmark_' --output-on-failure
python benchmarks/api/executor_run.py \
  --binary build/executor-off/benchmarks/api/wirelink_executor_benchmark \
  --profile off --out executor-off.json
python benchmarks/api/executor_run.py \
  --binary build/executor-locks/benchmarks/api/wirelink_executor_benchmark \
  --profile locks --out executor-locks.json
python benchmarks/api/executor_analyze.py executor-off.json > executor-off-summary.json
python benchmarks/api/executor_analyze.py executor-locks.json > executor-locks-summary.json
```

脚本默认每项重复 3 次，随机交错顺序固定；保留每次原始日志和二进制 SHA-256。
每生产者饱和发送 5000 次，定频发送 1000 次；`--skip-paced` 可跳过定频组。
不覆盖报告，也不在共享机器上自动设置性能门槛。失败进程的输出同样留在旁边的日志目录。
汇总保留各项重复的 min/median/max；“p99 的中位数”不是合并所有调用后的全局 p99。
阶段 `mean_ns` 是每次获取锁/唤醒批次的平均值，`ns_per_call` 则按业务提交次数摊销。

## 负载含义

| 模式 | 实际执行内容 |
| --- | --- |
| `rpc` | 业务线程调用生成 `_sync`；一个 executor owner 同时推进两个真实 loopback 端点 |
| `proxy` | 同一个真实 executor 的入队/唤醒/CV 完成；测试 submit 在 owner 即时完成，不经过协议 |
| `latest` | 每个生产者使用独立 message ID；真实 outbox、帧编码和 owner sink |
| `shared` | 所有生产者共用同一 message ID，测“只保留最新”时的替换竞争 |

矩阵为 1/2/4/8 生产者、32/512 B，以及不休眠或**每生产者** 1000 µs 间隔。
8 个定频生产者就是合计约 8 kHz，不是合计 1 kHz。

单独运行的参数为 `MODE PRODUCERS CALLS_EACH BYTES PERIOD_US`：

```sh
build/executor-off/benchmarks/api/wirelink_executor_benchmark rpc 8 5000 32 0
```

RPC 校验每次响应，LATEST 校验完整 payload、每生产者顺序与转入 idle 后的最终值。
LATEST 的延迟是 **submit 返回时间**，不是送达时间；输入吞吐不能等同发送吞吐，
应同时查看 `dispatched` 和 `coalesced`。共享通道允许最终值被另一生产者替换，
因此用生产者退出后的一条独立 marker 检查最终排空。
`coalesced` 是替换仍有效 outbox 项的次数，可能包括 owner 已复制、尚未 complete 的项；
它不是精确丢帧数，不能与 `dispatched` 相加校验总提交量。shared 的 marker 计入这两个计数，
但不计入业务调用数。

进程 CPU 包含 owner 与业务线程；墙时包括屏障、调度、结果校验和最终排空。
初始化在测量前；本基准不含 socket、USB、H7、CAN 或电机业务。`proxy` 不是 RPC 网络基准。
Windows 的进程 CPU 读数使用 `GetProcessTimes`，不使用当地返回墙时的 `std::clock`；
本轮仅在 Linux 执行，未把跨平台源码等同于 Windows/macOS 性能验收。

## 插桩读法与限制

`latest_{submit,take,finish}_{wait,hold}` 与 `rpc_{admit,collect,finish}_{wait,hold}`
分别记录获取 mutex 和持有 mutex 的墙时。汇总发生在解锁后，避免把诊断原子加法计入临界区。
`notify_to_owner` 是首个合并通知到下一 owner pass 的批次近似，不是每个调用的排队时间。

锁阶段的 `cpu_ns=0` 表示**未测量**，不能解释为没有 CPU 开销；等待墙时包含被抢占。
条件变量等待会释放 mutex，不能将整段 RPC 等待算成锁持有。
完成通知仍在 mutex 内，保护调用方栈上 job/CV 的存活期。

LOCKS 模式关闭广域 Scope 的 CPU 读钟，但诊断汇总仍有开销，并可能影响线程交错。
OFF 数据用于最终 CPU/延迟判断，LOCKS 只用于定位；不可把 ON/OFF 差额当作“删锁收益”。
每阶段范围有嵌套或跨线程重叠，不能相加后称为整机占用。
完整 `WIRELINK_HOST_PROFILING=ON` 仍用于其他阶段归因，运行脚本时选 `--profile full`。

本轮结果及无锁决策见 [争用与 H7 CPU 记录](../../docs/executor-h7-performance-cn.md)。
