# 静态端点内核重构：K0–K2 阶段结果

日期：2026-10-31。对应 [执行计划](static-endpoint-kernel-refactor-plan-20261030-cn.md)、[K0 合同](static-endpoint-kernel-contract-20261030-cn.md)、[K2 登记](static-endpoint-kernel-k2-registration-20261031-cn.md) 和[唯一补充块](static-endpoint-kernel-k2-supplement-20261031-cn.md)。**K0、K1 及 K2 筛查完成；主要继续条件满足，可以进入 K3。本文保留 K0–K2 历史结论；当前 K3 已完成、K4 完整对照与重放通过（见下方链接），K5/K6 尚未完成，默认产品仍为旧实现，不能称为整个重构已交付。**

## 结果

两块各 CPU-off/on A→B→A→B→A，合计 20 份新捕获。每份为 1000 warmup＋10000 RPC、5 秒 idle、100 Hz/200 calls、1000 帧至少 2 ms paced Sample、32 B 三方向各 1 秒准备＋5 秒测量。两个 B 分别与相邻 A 的指标均值配对，再取两个降幅的中位数；不池化样本、不合并两块。没有使用历史数字代替本轮 A。

| 指标 | 首轮 B 的两个值 | 首轮配对降幅 | 补充 B 的两个值 | 补充配对降幅 |
| --- | --- | ---: | --- | ---: |
| CPU-off RPC RTT p50，µs | 374.989 / 356.831 | **69.58%** | 339.856 / 354.824 | **70.91%** |
| CPU-on MCU RPC active/call，µs | 327.920 / 331.441 | **74.38%** | 330.329 / 331.163 | **74.06%** |
| CPU-on MCU Sample active/frame，µs | 205.752 / 200.631 | **47.66%** | 191.018 / 197.447 | **50.26%** |

两块各自的主要指标和 MCU Sample active 基线均在 5% 稳定门槛内，两个 B 方向一致。对应 A 的原始单次值：

| 指标 | 首轮三个 A | 补充三个 A |
| --- | --- | --- |
| RTT p50，µs | 1200.574 / 1210.059 / 1190.261 | 1190.001 / 1198.137 / 1190.017 |
| MCU RPC active/call，µs | 1299.402 / 1277.924 / 1291.773 | 1247.332 / 1277.435 / 1297.945 |
| MCU Sample active/frame，µs | 387.840 / 387.929 / 389.189 | 391.680 / 393.135 / 384.182 |

RPC RTT 与 MCU active 分属独立 observer 组，不能相减或合成一个“整体百分比”。本轮观察到的是**静态端点内核＋已授权语义裁剪的组合收益**，不是只改某个 selector 的独立因果估计。

## 保护项及限制

- 两块均无可判定的重复保护回退。首轮 CPU-off RPC p99 的 A 波动跨门槛，不可判定；补充块的 A 稳定，B p99 为 499.669 / 521.184 µs，配对下降 **62.27%**。
- 低频 p99、部分 host CPU/call 或 CPU/frame、部分 Sample API 尾部仍因 A 的 max/min 跨 5% 而不可判定。保留这些限制，不能笼统宣称“所有负载无回退”。
- 补充块 CPU-off host Sample CPU/frame 的 A 为 42.345 / 40.708 / 42.488 µs，B 为 43.016 / 44.769 µs。两个配对变化为 **+3.59% / +7.62%**，出现一份超过 5% 的异常，没有满足两份重复回退条件。首轮该项 A 不稳定，不把两块池化来淡化异常。K3/K4 继续追踪此项。
- idle 的 host CPU 比例及 MCU active 比例没有跨登记保护线。全部 32 B 单向和双向 goodput 分量的 A 均稳定，B 均提高；各分量配对增幅在首轮为约 88%–178%，补充为约 90%–189%。分方向原始字节／帧及计时窗口全部保留，不用双向总数掩盖单侧。
- 已用完唯一完整补充块，不再追加第三块或选择性重测。上述单次异常和不可判定项不等于重复回退，也不等于保护项全面通过；主要收益足够支持 K3 的功能迁移，完整产品替换仍须 K3/K4 验证。

Sample API p50 在首轮的配对降幅为 CPU-off 95.50%、CPU-on 87.38%；补充的 CPU-off 为 94.28%，CPU-on 因 A 不稳定只描述原值。**这些 API 数字包含执行位置变化**：新 send 在返回前完成编码快照，USB 提交由 step 推进；旧 send 在 API 链内已经进入 sink。不能把 API 的全部降幅称为业务 codec 提速。整段 MCU active/frame 独立下降约 48%–50%，提供了更完整的 MCU 工作证据。

首轮五份 CPU-on Sample 均为 1002 次 USB IRQ，on/off 的 API 绝对耗时差异仍存在。没有据此认定计时问题或否定编译期方向；缓存压力、observer 构建和执行路径的定量归因仍不能只靠 IRQ 数量或 `.text` 大小完成。本轮未运行 IBUS 探针、未搜索 padding/函数顺序/强制 inline/IRAM/cache/调度组合。

## 实际改变与不变条件

旧路径的 link/event、endpoint/pump、generated runtime、RPC pending/cache、response service 和 link TX 对象链，收敛为一份四槽 call 表、四请求快照、四待发回复、普通 TX、RX 借用及单物理租约。WLC 静态生成消息路由、typed handler/client 和实际容量；native/NONE 到达实际机器码。没有旧 RPC server/client、pump 或 `wl_poll` 后备路径。

replay/history/fingerprint、ARQ/ACK/retry、服务端 deferred、通用业务调度不进入新内核。新 profile v2 显式拒绝旧声明，保留托管 RPC v4 元数据、原私有业务 codec、普通 DATA 和 public codec 字节规则。四调用乱序、双向 RPC、BUSY 单次 handler、响应 admission、取消／超时／关闭、晚到响应、旧 token、异步 TX 与错误恢复均有软件覆盖。

USB 产品绑定沿用原描述符、net_buf、MPS、ZLP、明确重配置流程；USBD/UDC driver 未改。实际 benchmark 继续编译并调用原 Setpoint、Control、Sample、stream、CPU 和资源业务。client 入口在 B ELF 中保留，没有改成 server-only。两个 net_buf 额外 token 共 32 B，应用四个业务 continuation 共 32 B，均计入 RAM。

完整 `.config` 差异只有旧 Wirelink/module/platform/adapter 的四个开关及新实例身份选择开关；外部平台参数相同：ESP32-S3、240 MHz、16 KiB/8-way/32 B I-cache、`-Os`、LTO off、2×2048 RX、单 RX/TX、4 call、16 budget、相同栈／heap／USB 和 SMP 设置。两组 `.iram0.text` 均与 A 相同；未搬移 core 到 IRAM。

每个 observer 共用一份冻结旧执行引擎 host，仅外围 harness 加入事前 gate、K2 工作量选择和结束标记。实际 main 与两个 I/O worker 均为 policy 0、nice -4、affinity 0–5，这是继承现状，本轮没有设置或优化。governor/EPP 事前冻结，每次 warmup 前及期间每 50 ms 检查。全部 20 份环境、身份、pacing、错误及帧／字节账本通过。

## 资源与验证

Xtensa 上新 endpoint 4400 B（已含 416 B core、typed reply scratch 和 TX 池）、port 88 B、USB adapter 152 B、RX 4096 B；请求池 4×53 B，回复池 4×544 B，普通 TX 1036 B。A 的 endpoint 为 11832 B。完整固件 `.bss + .noinit` 在 CPU-off/on 均减少 **7248 B**，包括所有新增应用和 adapter 数据；其他 DRAM data/平台区相同。main/driver 栈大小不变，CPU-on 原始资源行保留实际高水位和 allocator 观察，不把可用 heap 增加冒充库 RAM 节省。

CPU-off 的总 Flash `.text` 为 A 79824 B、B 59360 B，减少 20464 B。它包括冷路径，不能直接当作频繁执行工作集、cache miss 或收益归因证明；bin 中 MMU 对齐页减少也没有计为热代码收益。

验证包括 strict C11 GCC、Clang ASan/UBSan 的 5 项 host tests，10 项 Ztest unit、真实 USBD class 的 native_sim integration、WLC 完整 197 tests 与 clippy，以及新 view API 的专项测试。实板 stock 旧 host 的 CPU-off/on smoke 和九组双向编码帧边界均通过。K2 两块 20 份由独立脚本从原始 JSONL、console、线程时间线及 SHA 重新计算，`k2-replay.json` 全部通过。

分析器初稿对 CPU-on RTT/low p99 多标了保护标签，已按事前 off-only 登记修正；原源码和 JSON 保留。没有改动原始数据、计时、工作量或保护阈值。K1 的命名、PATH、tracing include 构建失败尝试也全部保留。

## 提交、证据及阶段交回

| 仓库 | 主要提交 |
| --- | --- |
| Wirelink | `a5f6800` 独立 claim 修复（进入 A）；`876df6f` K0；`7409077` 共享内核；`ddc6c3a` 七消息 fixture；`44155bf` USB 产品绑定；`b547e08` view 快照测试 |
| WLC 独立仓库 | `ac6c29b` profile v2 与生成端点；`f79807a` borrowed-view 普通消息入口 |
| benchmark 独立仓库 | `f47904d` 真实 USB 业务集成 |

证据根目录：`../wirelink-static-kernel-20261030/`。`baseline/` 冻结旧 A/WLC/benchmark/Astrial 和平台身份；`k1-usb/` 保存输入、构建、完整配置差异与结构审计；`k2-host-source/` 和 `k2-host-harness.patch` 保存共同旧 host；两份 registration、`captures/k2-*`/`k2s-*`、两份 analysis、`k2-replay.json` 保存完整筛查。每份含原始 samples、分段趋势、CPU、IRQ、pacing、字节／帧、thread/heap 及工具/产物摘要。原有研究证据未覆盖。

| 产物 | SHA-256 |
| --- | --- |
| B CPU-off bin | `176ad8a702ce195e61a6a2be241532759ecfaf4fa9495eb2cd299e34732699e6` |
| B CPU-on bin | `520b94e163c565420ef9cf9ce5107aa47549ebcf2f55ff4f8cfffbbc5e8ea6d8` |
| 共同旧 host CPU-off | `c9660cf609c8a65b0c84ba9b34efe4bc10726d1c703ce6bcbadc12b53d3bdf3e` |
| 共同旧 host CPU-on | `37cc4e1cebc90fb2baef12d848cfa741460c8be22ef9fc0f8b35a55147495dd4` |

板卡已恢复本轮**新冻结 A** 参考：`static-kernel-20261030-A-o0`，bin SHA-256 `cf7b3d84b891ac4863dde038414a58621a28eaedc37183baf3df43134560d5ca`，multi、observer off、16 KiB。最终 smoke 再次通过，`k2-handoff.json` 记录 UART/USB `fuser` 检查无打开句柄；没有恢复历史 COBS/UART 固件。

K3 下一点是把 host 接到同一新内核，完善剩余部署／生命周期／非 benchmark schema 与受影响 adapter；保留 K2 的共同旧 host 控制。K4 还需完整新 host/new device A/B 与 libcsp 对照，尤其复核 host Sample CPU 和低频尾部。K5 才删除旧产品路径并迁移全部消费者，K6 再交付完整产品结论。本阶段没有 push、发布、修改 vendor 或把原型设为默认。

## K3／K4 后续状态

K3 保留合同、非 benchmark schema、安装消费者及 UART／UDP／USB adapter 通过，见 [K3 记录](static-endpoint-kernel-k3-progress-20261031-cn.md)。K4 新 host/device 三组 18 份完整采集和独立重放通过，见 [K4 结果](static-endpoint-kernel-k4-result-20261031-cn.md)。允许进入 K5；14 项保护观察仍不可判定，跨库部分 32 B goodput 未达，不改判为全面通过。K4 后板卡为新 B-on 且最终 smoke 合格，具体身份见 K4 结果；上文新 A 是 K2 交接时的历史状态。
