# 可靠 RPC 与高频遥测共存：实现与验收

日期：2026-09-09。基线为 Wirelink `c206c1e`；WLC 使用
`115f48132a5a761bc47f5c7880de940ea6fb2275`，生成 ABI 29 未变。
本轮只修改 Wirelink dev 工作区，不修改产品 pin、不发布、不合并 main。

后续已完成独立 H7 的功能与 CPU 验证，见[H7 实测记录](mixed-traffic-h7-cn.md)。
本页保留最初主机/模拟阶段的边界；H7 新鲜度场景仍不是实际 USB/串口链路。
后续本地提交的边界与重新验证结果见 [Git 提交索引](optimization-commits-cn.md)。

## 先确认影响

旧实现把可靠句柄、等待 ACK 和物理 DATA 资源作为同一个发送门槛。
因此接收方已经拿到请求或响应，只要 ACK 丢失，发送方仍会拒绝不可靠遥测；
可靠事务终态后尚未 `take()` 也会阻塞它。

新增[混合流量验收](../benchmarks/mixed_traffic/README.md)，使用真实生成端点与 RPC，
模拟时钟和异步物理发送。每种场景运行 2,400 个 1 ms owner tick，两端都发送遥测；
有 RPC 时，每方向在 100/500/900/1300/1700 ms 各发起一次调用，共十次。
普通遥测为 200 Hz，ACK 超时为 100 ms，RPC 预算为 1,500 ms。
覆盖 COBS、native packet、length16 三种封装和 CRC32C，共 42 条接收端记录。

观察两个不同的“年龄”：

- 新数据到达年龄：收到这一条时，距离采样已经多久。
- 持续数据年龄：业务仍在使用上一条数据时，它在每一个 tick 上有多旧。

`latest` 的读取会消费更新标记，但业务持有的上一条值仍然继续老化。
测试不会只在收到新包时统计年龄，也同时记录最大更新间隔和 RPC 完成时间。

## 前后结果

以下全部是**模拟协议时间（ms）**，取三个封装、两个方向中的最大值。
不代表主机 CPU 耗时、真实串口/USB 延迟或 H7 性能。

| 场景 | 最大持续年龄：前 → 后 | 最大更新间隔：前 → 后 | 最慢 RPC：前 → 后 |
| --- | --- | --- | --- |
| 只有遥测 | 6 → 6 | 5 → 5 | — |
| 遥测＋无故障 RPC | 12 → 9 | 11 → 8 | 5 → 5 |
| 每方向仅丢一个 ACK | 113 → 9 | 112 → 8 | 106 → 106 |
| 每个可靠序列前两个 ACK 丢失 | 2,056 → 8 | 2,055 → 7 | 247 → 247 |
| 每 250 ms 有 20 ms 物理背压 | 26 → 26 | 25 → 25 | 5 → 5 |
| 持续丢 ACK＋上述背压 | 2,074 → 26 | 2,073 → 25 | 265 → 266 |
| 饱和／回绕：1 kHz 采样、3 ms I/O、丢 ACK＋背压 | 2,158 → 36 | 2,155 → 30 | 340 → 458 |

每个 RPC 场景均完成十次正确调用，业务 handler 恰好执行十次。
高频遥测没有饿死 RPC；但在饱和场景中，允许遥测使用链路会消耗带宽，最慢 RPC
由 340 ms 增至 458 ms。**这不是“遥测变好且 RPC 完全无代价”**，仍在本场景的调用预算内。

持续丢 ACK 的旧 COBS 场景尤其容易误判：恢复后新包的到达年龄只有 1–2 ms，
实际却已约两秒没有更新。正常丢一个 ACK 时也有约 112 ms 的断更，不必达到极端负载
才会触发这个问题。

先冻结旧可执行文件，再开发状态拆分。最终又从 Git 导出 `c206c1e`，用相同最终
测试源码和 WLC 链接旧核心复核。旧核心的 `--report` 通过正确性检查，`--check`
明确失败于新鲜度门槛；新核心通过。没有拿重新编译后的新核心冒充旧基线。

## 实现边界

1. **逻辑可靠事务与物理 DATA 分开**。可靠句柄、序号、重试次数和 ACK 起点继续保留；
   物理资源空闲时允许不可靠发送，其成功、失败、BUSY 和回收均不改写可靠事务。
2. **不新增整包缓冲区**。`tx_unit` 仍是唯一物理 DATA 单元。可靠 native claim 的 payload
   原本位于这个单元内；仅在遥测首次要复用它时，才复制到已有的 `tx_payload`。
   其他可靠请求已拥有该 payload，无需重复保存。遥测使编码缓存失效，下一次重传再编码；
   没有插入遥测时仍直接复用编码帧。COBS/length16 的混合 claim 使用已有原地编码回退。
3. **控制与重传优先**。ACK 先于可提交 DATA；到期重传会阻止新的遥测占用发送单元。
   已接受的 BUSY/in-flight 单元不抢占、不覆写。活动 claim 也必须 commit/abort 后才能重传。
4. **生命周期独立**。可靠 cancel/take 不清除遥测的队列、token 或 I/O 借用。
   重传期间收到匹配 ACK 时，可先完成逻辑状态，但终态通知和回收等待其可靠 I/O 结束。
   终态事件被 RX 占位挡住时保留到下一次 poll，不静默丢失。
5. **不增加无效轮询**。物理资源仍被占用时，核心 hint 不把已过期 ACK timer 报成
   可以立即推进的任务，等待 adapter 活动或 claim 释放。普通不可靠发送仍不读取新时钟，
   不把生成接口传入的占位 `now_ms=0` 当作可靠计时来源。

为明确所有权，初始化现在拒绝 `tx_payload`、`tx_unit`、`control_unit` 三个声明区域相互
重叠。检查只在 init 执行。普通 WLC 端点本来就是独立数组，不增加业务设置。

Linux x86-64 上内部实现结构为 768 → 784 B，仍装在现有 896 B opaque reserve 内；
公开 `wl_ctx_t` 为 896 B、`wl_storage_t` 为 80 B，测试生成端点为 3,888 B、
runtime arena 为 1,186 B，均未改变。无堆分配、无线程/锁改动、无多窗口或线上帧变化。
WLC 源码与生成 API 不需要修改或升级 ABI。

## 验证记录

- 新协议用例覆盖：复制/claim 发送、三种封装、SENT/STARTED/BUSY、最大 payload、
  可靠重传逐字节相同、时钟回绕、claim 持有、迟到 ACK、取消/回收并发的遥测 I/O、
  物理失败、ACK 优先级与 TX 存储重叠拒绝。
- 原有“没有其他 DATA 时只编码一次”的重传测试仍通过。
- 混合流量 Release 与 Clang ASan/UBSan：每套 2/2 CTest，通过全部 42 条记录及报告检查。
  分析器另有四项测试，拒绝缺失、重复、错误计数和没有实际执行的故障注入。
- WLC 全套 139/139，通过现有生成 C/C++、所有权和 RPC 消费测试。
- 主机 Release 重建后 20/20；Clang ASan/UBSan 重建后 17/17，排除 Python/extension，
  由 Release 覆盖这两类测试。
- Zephyr 最终统一矩阵 37/37 配置、250/250 用例通过，零警告，包含存储检查与
  完整混合场景；另有两个配置被平台规则静态过滤，不计为通过。
- protocol / poll_hint 另以 GCC 32-bit ASan/UBSan 验证：2/2 配置、56/56 用例通过。
  当前 Zephyr 的 `unit_testing` 未实际应用 Twister 的 sanitizer 开关，因此使用显式
  `EXTRA_CFLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all`
  和 `EXTRA_LDFLAGS=-fsanitize=address,undefined`；已核对实际编译参数及运行日志。

主机计时噪声不会改变模拟时间结果；本轮没有运行 CPU 微基准、UDP 性能采样或 H7。
native_sim / Cortex-M3 QEMU / RISC-V QEMU 的混合应用使用同一个工作负载，
验证的是目标 ABI/工具链和协议行为，不将它们称为 H7 实测。

## 仍然不承诺的事情

- 物理链路永久 BUSY、owner 停止运行、RX 消费过载或事件预算不足时，没有有限延迟保证。
  本矩阵明确采用每毫秒一个 owner tick、默认每次 16 个事件的预算。
- 核心已接受的 BUSY 遥测是一个确定的传输单元，不会被后来的更新替换；因此真实背压
  仍能增加到达年龄。更进一步的 latest-aware 排队是另一项策略优化。
- 核心等待提示的改进不等于所有上层 executor/业务调度都已经证明没有忙等。
  真实 Willow 的调度、USB 队列、固件 CPU 和产品混合流量仍需后续产品/H7 验证。
- 没有宣称重编码的 CPU/Flash 代价已经完成实板验收。当前取舍是避免为罕见重传
  再增加一个完整帧缓存，同时保留无插入流量时的缓存快路径。

## 可复核材料

本机保留 `build/mixed-traffic-before/` 的初始冻结产物，
`build/mixed-traffic-baseline-src/` 的 Git 导出和 `build/mixed-traffic-baseline-confirm/`
的最终基线复核；候选报告、比较、构建日志在 `build/mixed-traffic-after/`。
显式插桩的单测产物在 `build/mixed-traffic-unit-instrumented/`。
原始 JSONL 不删改成“只保留好看的场景”，比较器要求七种场景、三个封装、两个方向齐全。

最终主机程序 SHA-256：

- 基线复核：`507d08bc7c270bca14f84dd7225e691f81ba086a0eb78bf67fbddea5054cb85e`。
- 候选：`274a25d5059a50a84764260ebaa7a0ab7597c2d2b8454a5afbf62cccdbbb05f9`。

可复用源码、IDL、分析器与 Zephyr 测试放在仓库内；`build/` 原始材料仅留本机。

### 最终统一矩阵

运行目录：`build/mixed-traffic-zephyr-final/`。覆盖 unit_testing、native_sim、
qemu_cortex_m3、qemu_riscv32；构建并行度为 2，与 CPU 性能采样无关。
37/37 个执行配置、250/250 个用例通过，零警告；另有两个配置被静态过滤。
