# API 性能收敛与回归基线（ABI 28）

日期：2026-09-08。接续 [ABI 27 维护体验迭代](maintainer-api-progress-cn.md)。
本轮只修改 Wirelink/WLC 开发工作区，不迁移产品、不操作实板、不提交、推送、发布或合并 main。
此处保留第一轮基线；后续新增的多生产者与实板测量见
[Executor/H7 性能记录](executor-h7-performance-cn.md)，不要把两轮的 ABI/测量边界混用。

## 结论与实现

本轮确定的收益是**多服务端点静态内存减少**；64 服务基准的初始化也变快。
稳态 RPC CPU 基本持平，UDP 延迟没有可以稳定归因于代码的改善。
不能把下面的主机数字当作 libflorid、Willow 或 H7 的结果。

- WLC 的规范化请求缓冲由各服务容量相加改为取最大值。去重缓存只保存指纹和结果，
  不借用这块临时内存；各服务配置的编码上限仍分别检查。
- 有默认静态存储配方的有界 profile，共用一个跨服务解码 union。每个端点有自己的
  缓冲，不是全局共享。含无界消息的高级 runtime 保留逐服务解码对象，避免破坏
  调用方配置的可变长数组 backing 指针。
- 指纹算法的固定域前缀在编译器里预计算；逐字节指纹值不变。主机优化编译器也可能
  已经折叠旧循环，因此不把这一项宣称为主机加速。
- RPC 服务端 deadline 查询合并重复的 cache 扫描；READY 响应立即返回零等待。
  不改变超时、TTL、缓存淘汰、回绕规则，也不在查询中清理状态。

解码输入只在当前回调内有效；延迟业务复制输入和 token，不能保存暂存区指针。
同一 runtime 的分发不能重入，普通端点已有 owner/重入约束。以上不新增动态分配或锁。
生成布局变化使 **codegen ABI 27 → 28**；codec、Compact-v1、托管 RPC v2 和指纹值不变。

## 为什么选择两层基准

[基准使用说明](../benchmarks/api/README-cn.md)包含完整构建和执行命令。

1. **Google Benchmark + loopback**：真实生成端点/核心，测线程 CPU、墙时、初始化、
   idle step、RPC 批次、遥测、静态容量和协议时钟读取次数。采用其原生重复采样和 JSON，
   不另外实现微基准计时器。[计时器说明](https://google.github.io/benchmark/user_guide.html#cpu-timers)
2. **独立 UDP client/server**：真实普通 `_sync` API、Asio 适配器，Python 只启动进程。
   测 RTT 分位数及两端各自进程 CPU；不把等待 socket 的墙时算成固件 CPU。
3. **Zephyr native_sim/QEMU**：继续做行为和平台集成验证。native_sim 的模拟时间
   [与主机执行时间解耦](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html#about-time-in-native-sim)，
   不是 H7 性能模型。

基准默认关闭，核心消费者不增加依赖。本轮 Google Benchmark 为 v1.9.5，源码提交
`192ef10025eb2c4cdd392bc502f0c852196baa48`，没有自动下载或源码内嵌。

## 同条件前后测量

Linux x86-64，Intel Core 5 315，6 核；GCC/G++ 16.2.1，Release `-O3 -DNDEBUG`，
未启用 LTO。未关闭系统调频、ASLR，也未独占 CPU；工具已报告相关噪声警告。
冻结优化前 ABI 27 可执行文件，优化后使用 ABI 28；没有在修改后重建旧目录冒充基线。

正式复测每项微基准重复 5 次、最短 0.05 s；UDP 每种 body 大小预热 200 次，
测量 2000 次，独立启动 3 轮。下表取重复样本中位数。
所有服务的请求/响应 body 上限都是 1024 B；主要调用 Echo，其他声明用于观察服务规模成本。

### 静态内存

| 服务/槽数 | `sizeof(endpoint)`：前 → 后 | 减少 | runtime arena：前 → 后 |
| --- | --- | --- | --- |
| 1 / 4 | 24,960 → 24,960 B | 0% | 11,934 → 11,934 B |
| 12 / 1 | 27,168 → 15,472 B | 43.05% | 16,350 → 4,998 B |
| 12 / 4 | 37,552 → 25,840 B | 31.19% | 23,286 → 11,934 B |
| 64 / 4 | 97,040 → 30,000 B | 69.08% | 76,950 → 11,934 B |

arena 已包含在 endpoint 内，不能把两列相加。这里不是 RSS、产品 schema 的大小或固件 RAM。
剩余逐服务增长包括类型化 handler/路由配置，尚未做端侧角色裁剪。

### CPU 与 UDP

| CPU 用例 | 前 → 后（ns/op） | 变化 |
| --- | --- | --- |
| 12 服务/4 槽 client idle step | 48.18 → 48.03 | −0.31% |
| 同配置 server idle step | 51.26 → 51.26 | 持平 |
| 同配置 32 B 单次 RPC | 1435.49 → 1385.99 | −3.45% |
| 同配置 512 B 单次 RPC | 5136.74 → 5093.70 | −0.84% |
| 同配置 1024 B 单次 RPC | 9033.60 → 8970.76 | −0.70% |
| 同配置初始化+关闭 | 142.75 → 146.87 | +2.89% |
| 64 服务/4 槽初始化+关闭 | 488.19 → 375.50 | −23.08% |

第一次较短测量中，32 B RPC 几乎持平；12 服务初始化曾快 9.50%，此次反而慢 2.89%。
不宣称这些小幅变化是稳定收益。64 服务初始化两次分别改善约 24%/23%。
RPC 计时包含正确性校验和 ACK/TX 排空；四调用用例的一个 op 是整个批次。
12 服务可靠配置的协议时钟读取次数前后相同：idle 每步 1 次、单 RPC 9 次、
四调用批次 24 次、遥测 2 次；这是完整基准操作的计数，不是单个公开调用的固定成本。

| UDP body | p50：前 → 后（µs） | p99：前 → 后（µs） | client CPU（µs/call） | server CPU（µs/call） |
| --- | --- | --- | --- | --- |
| 32 B | 17.18 → 18.97 | 44.03 → 37.70 | 10.51 → 10.93 | 10.43 → 10.82 |
| 1024 B | 24.18 → 24.38 | 75.13 → 79.93 | 14.65 → 14.80 | 15.96 → 15.66 |

32 B p50 在前两组测量中均上升，因此追加 **A/B/B/A 交叉复测**：每种大小每组 5 轮，
每轮 5000 次、预热 500 次。两组 32 B p50 对比分别为 17.24 → 17.72 µs（+2.78%）、
19.03 → 17.52 µs（−7.93%）；1024 B p50 分别变化 −0.22%/+0.07%。
这说明当前环境不足以确认小幅延迟回归或提升，不能只保留有利的一次。
尾部也不稳定：正式复测 512 B 各轮 max 的中位数上升约 160%；原始 max 全部保留。
后续在固定 runner/调度条件下再定门槛，不以共享主机的一次 p99/max 阻断功能提交。

## 验证与证据

| 检查 | 结果 |
| --- | --- |
| WLC 全套 Rust/生成 C 测试 | 121/121；fmt、Clippy `-D warnings` 通过 |
| 共享 scratch 专项 | 2/2；含 ASan/UBSan 运行 |
| 既有 async endpoint ASan/UBSan | 通过；混合服务、槽数及可靠/不可靠组合 |
| Release 主机 CTest | 20/20；含第 13 个 RPC 扩展验收及现有 UDP 故障测试 |
| 安装后独立消费者 | 6/6；含 C/C++ 与组合 profile |
| Clang ASan/UBSan 主机 CTest | 17/17；排除 Python bridge 和独立 Release 扩展构建 |
| 新 benchmark 构建 CTest | 11/11，其中 7 个 benchmark smoke；报告解析含 4 个单元测试 |
| Zephyr RPC / rpc_async / generated_codec / application_runtime | 6/6 配置、53/53 用例；unit_testing、native_sim、Cortex-M3 QEMU |

新增用例检查不同容量/对齐、整数溢出、无 server 时不分配 canonical storage、
无界 repeated 解码对象不重叠、延迟请求经历另一服务后仍能完成，以及长度 0…257 的
新旧指纹逐一相同。deadline 用例检查 READY、已送达、在途、pending/过期组合和查询不修改状态。

本机证据在 `build/performance-deps/`（构建产物，不随源码提交）：

- `before-recheck.json` / `after-recheck.json` / `comparison-recheck.txt`：正式复测。
- `udp-before-{a,b}.json` / `udp-after-{a,b}.json` / `udp-compare-{a,b}.txt`：交叉复测。
- `before.json` / `after.json` / `comparison.txt`：首次较短测量，保留作噪声对照。
- `wlc-tests-final2.log`、`clippy-final2.log`、`shared-sanitize-final.log`、`owned-sanitize.log`。
- `ctest-regression-final.log`、`ctest-benchmark-final2.log`、`ctest-sanitize.log`、`package-test.log`。
- `twister/twister.json`：显式隔离产品 Zephyr 模块，只加载测试所需 CMSIS/Wirelink。

正式报告保存全部可执行文件 SHA-256。例如 12 服务/4 槽可靠微基准：

```text
before: 7a706de34e4215204e91d765509d79408d58fe009457a28719cc8964c2a75788
after:  7dafb77fed8c7617402964e46a831bb6abd7ef1855149d6af266fb2100696718
```

`run.py` 拒绝覆盖已有报告；`compare.py` 检查机器、编译器、构建参数和用例集合，
拒绝失败/缺失/非有限值。日常可只跑 benchmark 标签 smoke；正式性能评审应保存两份 JSON
作为 CI artifact 或评审附件。性能阈值默认关闭，启用后也只对微基准 CPU 中位数设门槛。

## 尚未收尾的性能项

- **帧编码与重复提交**：后续已实现常规 COBS 单遍编码和 DATA 重试复用，新增独立
  主机/H7 微基准，H7 两轮 A/B 已完成；常规路径收益和板端重叠/精确容量退化见
  [首轮记录](framing-performance-cn.md)。后续已完成 [H7 回退收敛](framing-fallback-performance-cn.md)，
  最终实现的主机性能对照因当前 CPU 竞争暂缓；不代表产品已同步或所有平台都更快。
- **Host 多生产者争用**：后续已完成 384 组矩阵和锁/唤醒分项测量，决定保留当前锁。
  LATEST 满速多生产者仍有扩展性成本；见 [实测及触发条件](executor-h7-performance-cn.md)。
- **按端侧角色裁剪代码与存储**、产品实际并发槽数调整：本轮未做，也不要求业务新增包装。
- **固件 CPU/锁等待**：后续已完成 ABI 26 产品 HIL 的 H7 500 Hz/1 kHz 插桩与对照。
  完整 Willow 电机/CAN 负载及 ABI 28 板端迁移仍未覆盖；主机数据不替代实板证据。
- **跨平台和发布集成**：Windows/macOS benchmark 尚未实跑；远程 WLC pin 仍是 ABI 26。
  ABI 28 需显式使用本地配套编译器，后续配对提交并更新 pin/SHA 后再跑远程 CI。
  不能把本地回归通过等同于已经发布或产品迁移完成。
