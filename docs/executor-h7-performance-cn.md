# Executor 多生产者争用与 H7 CPU 实测

日期：2026-09-08。接续 [ABI 28 API 性能收敛](api-performance-progress-cn.md)。
本轮完成测量、插桩和回归工具；**未替换锁、迁移产品 API、提交、推送或发布**。

## 决定：保留当前锁，分别处理不同瓶颈

| 路径 | 本轮判断 | 下一步触发条件 |
| --- | --- | --- |
| Host RPC admission/completion | 保留 mutex；入队临界区短，完成路径还保护栈上 job/CV 生命周期 | 实际业务出现持续排队时，先查 owner 批处理与唤醒，再做独立对照原型 |
| Host LATEST outbox | 满速多生产者有真实竞争，但不直接改成无锁 | 产品确实需要百万级提交，或定频尾延迟不达标时，先试按 message ID 分片/减少锁内复制 |
| H7 ArmProtocol `state_lock_` | 保留；单核短临界区，当前不是明显 CPU 瓶颈 | 完整 Willow/CAN 负载下临界区或中断屏蔽时间显著增长时重新评估 |

“锁短”不等于没有等待；“无锁”也不保证更低尾延迟。尤其不能把 RPC 的 `notify_one`
直接移到解锁后：等待方可能已返回并销毁栈上的 job/CV。若要这样优化，应先改变其存活期合同。
LATEST 的并发覆盖也不能用一个原子索引或未经读者保护的双缓冲替代：必须保持无撕裂、
同生产者顺序、generation 匹配和停止时最终排空。本轮没有无锁 A/B，**不宣称删锁收益**。

## 主机测量方法

用法见 [executor 基准说明](../benchmarks/api/EXECUTOR-cn.md)。
Linux x86-64、Intel Core 5 315（6 核），GCC/G++ 16.2.1，Release `-O3 -DNDEBUG`，无 LTO。
主机使用当前开发 ABI 28；每个 RPC 端点声明 12 服务、8 槽，普通生成 `_sync` API。

矩阵：4 种模式 × 1/2/4/8 生产者 × 32/512 B × 满速/每生产者 1 kHz × 3 次重复。
每生产者每轮满速 5000 次、定频 1000 次。OFF 和 LOCKS 各 192 组，各 216 万次调用/提交。
两套矩阵均零失败；RPC 验响应，LATEST 验 payload、顺序和最终值。

测量时不并行编译或跑其他基准，关闭 J-Link/RTT；但没有独占核心、关闭调频或固定调度。
各构建内部固定随机顺序，OFF 整套先于 LOCKS；**不是交叉 A/B**。
进程 CPU 包含 owner、生产者、系统调用及校验，初始化在测量外；睡眠导致的墙时不算 CPU。
没有网络、USB 或电机业务。`proxy` 仅测真实 executor 的跨线程即时完成，不是协议 RPC。

### OFF：未插锁探针的延迟与 CPU

下表均为三个重复值的中位数；p99 列是“每轮 p99 的中位数”，不是全局 p99。
时间单位 µs，CPU 为所有线程合计、每次调用/提交摊销。LATEST 延迟仅到 submit 返回。

| 32 B 负载 | 生产者 | p50 | p99 | CPU/次 | 提交/调用吞吐（万次/s） |
| --- | --- | --- | --- | --- | --- |
| 满速 RPC | 1 | 3.73 | 12.80 | 6.09 | 20.89 |
| 满速 RPC | 2 | 5.61 | 15.88 | 5.04 | 31.12 |
| 满速 RPC | 4 | 13.79 | 28.16 | 6.80 | 26.84 |
| 满速 RPC | 8 | 29.56 | 49.97 | 7.76 | 26.13 |
| 满速 proxy | 8 | 20.50 | 61.40 | 10.17 | 34.62 |
| 满速 LATEST（不同 ID） | 1 | 0.05 | 4.35 | 0.47 | 334.59 |
| 满速 LATEST（不同 ID） | 2 | 0.06 | 6.66 | 0.80 | 279.72 |
| 满速 LATEST（不同 ID） | 4 | 1.03 | 12.01 | 1.88 | 196.20 |
| 满速 LATEST（不同 ID） | 8 | 1.04 | 37.02 | 2.49 | 191.46 |
| 满速 LATEST（同一 ID） | 8 | 0.97 | 35.20 | 2.42 | 202.60 |
| 每生产者 1 kHz RPC | 8 | 70.59 | 396.60 | 26.01 | 0.80 |
| 每生产者 1 kHz LATEST（不同 ID） | 8 | 1.52 | 25.11 | 11.95 | 0.80 |

512 B、8 生产者时：满速 RPC p50/p99 为 67.04/90.18 µs、CPU 10.95 µs/次；
定频 RPC 为 98.31/272.74 µs、CPU 28.83 µs/次。满速 LATEST p99 为 37.19 µs，
定频 LATEST p99 为 28.09 µs。全部大小、生产者数、min/max 保留在原始 JSON。

需要注意：

- 满速 LATEST 的吞吐是提交吞吐，不是链路送达吞吐；只保留最新会大量合并。
  32 B、8 生产者每轮 40000 次提交，实际 dispatch 中位数仅 2744 次；定频不同 ID 则为 8000 次。
- `coalesced` 还包括替换已被 owner 复制但未 complete 的有效项，不是准确丢帧数。
  不能用 `dispatched + coalesced == calls` 做断言；shared 另有一条收尾 marker。
- 32 B 定频 RPC 的三轮 p99 为 222.80–443.84 µs；512 B 为 247.57–591.20 µs。
  这组尾延迟明显受调度影响，不能只比较两个中位数便断言小包比大包慢。
- 定频会反复进入睡眠/唤醒，CPU/次高于满速是本基准观察，不是协议处理本身变慢。

### LOCKS：等待、持有与唤醒分开看

只启用 `WIRELINK_HOST_LOCK_PROFILING`，关闭广域 CPU Scope。记录解锁后的聚合计数，
条件变量阻塞不计入 mutex 持有。下表为 32 B、8 生产者，各轮**阶段均值**的中位数，单位 µs。
通知延迟按合并唤醒批次统计，不是每个调用的排队时间。

| 模式 | admission/submit 等锁 | admission/submit 持锁 | complete 持锁 | 通知到 owner |
| --- | --- | --- | --- | --- |
| 满速 RPC | 1.06 | 0.19 | 0.79 | 2.65 |
| 满速 proxy | 3.57 | 0.21 | 0.89 | 10.12 |
| 满速 LATEST | 3.55 | 0.21 | 0.12 | 9.86 |
| 定频 RPC | 2.17 | 0.55 | 2.40 | 17.34 |
| 定频 LATEST | 1.15 | 0.40 | 0.10 | 6.62 |

LATEST 满速提交的平均等锁时间从 1 生产者的 0.13 µs 增至 8 生产者的 3.55 µs，
且 OFF 吞吐下降，说明确实存在扩展性成本；不应说“剩余 mutex 没有影响”。
但定频 8 kHz 能正确排空，尚未给出需要付出无锁所有权复杂度的产品需求。

RPC admission 不是唯一成本；`proxy` 即使省去协议，也有明显跨线程完成与通知开销。
据此优先调查线程交接和批处理，而非只换入队容器。不能用 `RPC − proxy` 精确分解协议 CPU，
因为两者线程交错不同。

锁墙时包含抢占，LOCKS 的计数器和读钟仍会扰动调度；`cpu_ns=0` 表示未测量，
不是锁不花 CPU。部分阶段最大等待超过 1 ms，原始 max 未删去。跨线程/嵌套阶段不能相加
称为整机占用，OFF/LOCKS 差额也不是无锁实现的预期收益。

## H7 实板：500 Hz 与 1 kHz

使用本机 STM32H723/dm_mc02（DWT 550 MHz）+ J-Link + USB Bulk。
[测试包装应用](../benchmarks/zephyr/willow_cpu/README-cn.md)直接编译 Ragtime 现有 HIL/ArmProtocol，
**不改产品源码或依赖 pin**。产品明确要求 ABI 26；保留断言并使用其配套编译器。
因此下面测的是当前产品协议路径，**不是 ABI 28 的板端迁移验收**，也不是完整 Willow 的
CAN、电机、NVS、看门狗负载。

Zephyr 线程 runtime stats 使用 DWT；每 2 秒由低优先级线程报告一次。
分析器只取完全落在正式 HIL 命令窗口内的 CPU 区间，排除 warmup/idle/跨界区间。
分母为频率 × 窗口墙时，另存总记账周期核对。ISR 归属遵从 Zephyr 架构的调度记账，
不是排除所有中断的纯函数 CPU。[Zephyr runtime statistics](https://docs.zephyrproject.org/latest/kernel/services/threads/index.html#runtime-statistics)

### CPU 对照

OFF 只关闭 ArmProtocol 锁探针；ON/OFF **均保留 CPU 记账、RTT 和断言**。
百分比取完整 2 秒样本的中位数，不是整轮的简单平均。

| 命令频率 | 锁探针 | owner 记账占用 | 全系统非 idle 记账 | 有效 CPU 样本 |
| --- | --- | --- | --- | --- |
| 500 Hz | OFF | 4.43% | 5.41% | 16 |
| 500 Hz | ON | 4.66% | 5.66% | 12 |
| 1 kHz | OFF | 8.85% | 10.80% | 5 |
| 1 kHz | ON | 9.31% | 11.31% | 4 |

总记账/墙时中位数为 99.10%–99.56%，不隐去记账/窗口边界差异。
锁探针增加的 owner 占用约 0.23/0.46 **个百分点**，这是插桩成本，不是删除锁能省的 CPU。

### 临界区与功能门槛

只拦截 ArmProtocol 编译目标的 `k_spin_lock/unlock`，不拦截内核/USB/HIL 其他锁。
单核进入临界区后关中断，计数不额外引入锁或 64 位原子；测试断言检查锁配对和无嵌套。

| 频率 | 平均获取耗时 | 平均持有耗时 | 累计持有/窗口墙时 | 启动以来最大持有 |
| --- | --- | --- | --- | --- |
| 500 Hz | 73.0 ns | 127.1 ns | 0.153% | 1.371 µs |
| 1 kHz | 73.0 ns | 127.0 ns | 0.305% | 1.316 µs |

获取耗时包含读钟、关中断和此前抢占；最大获取约 16 µs 不能解释为单核“自旋争用”。
持有区间从成功获取后的读钟到 unlock 探针入口，包含获取探针记账，但不包含解锁探针
自身收尾和实际解锁；不能称为完整 IRQ-off 时间的上限。最大值是启动以来 max，不是窗口 p99。

每组合正式验证 3 轮 × 5000 次回显，500 Hz OFF 另补 1 轮以补全分段 RTT 采集：
**13/13 严格窗口通过，65000 次回显正确，零 gap**（不含预热）。每轮重新打开连接。

| 频率/探针 | 三轮中最差 p99 | 最大 RTT |
| --- | --- | --- |
| 500 Hz ON | 2062 µs | 2373 µs |
| 500 Hz OFF | 2069 µs | 2656 µs |
| 1 kHz ON | 1071 µs | 1955 µs |
| 1 kHz OFF | 1088 µs | 1678 µs |

保留产品 host 的 `--strict-performance --require-diagnostics` 门槛；未为通过测试放宽阈值。
这些是本 HIL 调度/回显 RTT，不是 wire 编解码耗时。

## 回归、证据和复跑入口

| 检查 | 结果 |
| --- | --- |
| 主机 Release 既有回归 | 20/20，通过 UDP 故障注入、第 13 个 RPC 扩展等 |
| Clang ASan/UBSan 主机回归 | 17/17，排除 Python bridge 和独立 Release 扩展构建 |
| 安装后独立消费者 | 6/6 |
| executor OFF / LOCKS 构建完整 CTest | 19/19、20/20，含已有微基准/UDP smoke |
| ThreadSanitizer | 8 项各重复 10 次通过；另跑 4 模式 × 8 生产者 × 1000 次 × 512 B |
| WLC Rust/生成 C/C++ 全套 | 122/122；fmt、Clippy `-D warnings` 通过 |
| WLC 共享 scratch ASan/UBSan | 3/3 |
| 汇总脚本单元测试 | executor 4/4，H7 2/2 |

- [主机矩阵及汇总脚本](../benchmarks/api/EXECUTOR-cn.md)：OFF/LOCKS 分开构建。
- [H7 构建、RTT 与分析](../benchmarks/zephyr/willow_cpu/README-cn.md)：无需复制或修改 ArmProtocol。
- 本轮 Clang 严格 C++ 编译暴露了 ABI 28 解码 union 中嵌套匿名类型的扩展用法；
  WLC 改为预声明 detail typedef，字段路径/布局不变。新增类型名冲突检查和严格 C++ 消费测试。
  已重新生成完整 fixture；ABI 仍为未发布的 28，线上格式不变。
- 采集脚本最初的 stage 解析错误已修复并补回归；失败原始日志保留，重新完整执行 LOCKS 矩阵。

原始证据保存在本机 `build/performance-deps/`（构建产物，不随源码提交）：

- `executor-off.json`、`executor-locks-v2.json` 及对应日志目录、`*-summary.json`：全部 384 组。
- `executor-locks-run.log`、`executor-locks/`：首轮解析失败证据，不混入成功数据。
- `h7-cpu-*-host.log`、`h7-cpu-*-rtt*.log`、`h7-cpu-*-summary.json`：板端原始记录与有效窗口。
  500 Hz OFF 的两个 RTT 段来自同次启动，可合并；其他镜像之间不能合并计数。
- `executor-regression-*.log`、`executor-sanitize-*.log`、`executor-package-*.log`：主机/安装后回归。
- `executor-tsan-final-*.log`、`executor-tsan-p8-*.log`：ThreadSanitizer 及 8 生产者检查。
- `wlc-executor-final.log`、`wlc-executor-clippy.log`、`wlc-executor-shared-sanitize.log`：编译器检查。

测量二进制 SHA-256：

```text
executor OFF:   da0adf0b91d499883a1bab80b1430c9264db23d5778455e3831fd24fb80f28e5
executor LOCKS: 5671e6cec9cbe2d29c084bb818cb674236e3e0f77ab9a7fa6d224f2838b7a8d2
H7 500 Hz ON:   b7407c3ccd4f6b4eb4e513bb289cab708663808e129b0172751fec8560dd3925
H7 500 Hz OFF:  f530080bad49eae1dfdd55468d4b9780dc3e83f7f432ab72fd77702cfaf6886e
H7 1 kHz ON:    4589fdef64d88eaad9e675081c40567899bb6e21b6f62c649f6db9271aa5c5f0
H7 1 kHz OFF:   db9226e4f8de8a8f628004d01b10847ac70242b8d60228eb1db9adfb5c56bf7b
H7 host:        b6533cc00a2190cc27bbeeaf80e5b244d858a84f4ae9de95735757d4d3d849a1
```

H7 产品仓库基点 `b7777aad8b44a47d7feec6346c64a4199ce7bfd0`，其 Wirelink 模块
`4b650ba03f6d4d60fcbec76520a28757f834a1af`，WLC `c6b6a8fa560a15c45d564aad0afd197b13682de8`。
主机 Wirelink 基点 `337fede0a9d10f9ca63a9658784ff8707dc7c170` 加开发工作区修改，
因此以二进制 SHA 标识本轮测量，不能仅凭提交号复现未提交修改。

Windows/macOS 尚未实跑本轮矩阵；完整 Willow 电机负载、ABI 28 产品迁移仍是后续任务。
测试结束已关闭 J-Link/RTT 会话；**板上保留 1 kHz、锁探针 OFF 的 CPU HIL 镜像，不是 Willow main 应用**。
