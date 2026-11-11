# 静态端点 CPU 优化执行记录

> 本轮研究已收敛。下文实验命令及分支状态为历史记录，临时入口已删除；当前状态见[收敛记录](static-endpoint-cpu-convergence-20261111-cn.md)。

日期：2026-11-01。本文记录 O0、O1 首轮及撤回的 O3 候选；A 是其后优化的对照基线。后续 [O2 两种通知候选及最终交回](static-endpoint-cpu-o2-result-20261101-cn.md)均因保护回退未采用，正式产品仍保持 A。

## O0：本轮 A 已重新构建并通过资格检查

本轮证据目录为 `/home/ww/codings/wirelink-cpu-optimization-20261101/`，不向历史封存目录写入。本轮 A 从 root `28fb84b`、cross-benchmark `f2896f7` 及各仓库当时实际工作树构建；WLC 为 `7eabbf4`。完整 HEAD、dirty patch、未跟踪输入、Zephyr／模块和 matching WLC 身份见 `A-{off,on}/sources/inputs.json`、`build.json`。旧大内核不进入本轮对照。

`O0-baseline-audit.json` 记录 native core、packet port、USB adapter、生成代码和 observer 对象及 ELF sections。核对的 core、USB、应用、schema/profile 和 observer 源文件与已接受版本一致。入口清理后的新工具和本轮研究代码不改变产品路径。

新 A 的 off/on 均保持 240 MHz、16 KiB／8-way／32 B ICache、`-Os`、LTO off、FS/MPS64、2×2048 B RX、1 RX／1 TX in flight、双角色、4 calls／4 replies、budget16。固件、gated host 和完整生成物已重新构建，不能把历史 capture 作为本轮 A 的性能结果。

本轮已实际运行：

- GCC Release、Clang ASan/UBSan，各 47/47 CTest，含 core/port 并发、USB/UART/UDP host adapter、Executor、owner-pass、USB 应用、生成代码及通知模型。
- Zephyr `static_packet_port`（unit_testing）和 `static_usb_bulk`（native_sim），15/15 测试用例。
- A-off/on 各一份板上 smoke：RPC、idle、低频 RPC、1000 Sample、四种大小／三个方向 stream，业务、双端字节账本和 observer 检查通过。原始记录见 `captures/qualification-A-o{0,1}/`。

## O1：先测探针扰动，再选择候选

独立源快照 D 在 core、port、USB callback、owner 及 semaphore 内计数，host 记录 CoalescingEvent 合并／消费和 adapter service。研究工具生成带严格锚点检查的源补丁，不向产品或 SDK 工作树加入 hook，也不启用额外产品开关。

D 和正式 A 分开。已按预登记的三轮 A/D（AD／DA／AD）交错比较 off/on，共 12 份完整 screen capture，工作量为 1000 warmup＋10000 RPC、idle5s、200 次低频 RPC、1000 paced Sample，以及 32 B 三方向 stream。它回答计数与探针扰动，不代替 O5 的完整 A/B/C 验收。计数不换算成独占 CPU 时间或预计提速。

原始重放 `O1-diagnosis.json` 通过全部 12 份采集的身份、环境、业务／字节账本和诊断页检查。新增工具的 9 项测试覆盖缺页／重复页、计数域、业务与 semaphore 账本，以及错 observer／身份／环境等拒绝条件。A-off 的实际编码边界资格验证也通过。测量结束已刷回本轮 A-on 并通过 smoke；I/O 句柄均已关闭。

### 诊断探针有显著扰动

以下为三轮配对 D/A 比的中位数，不是产品优化收益：

| 指标 | off | on |
| --- | ---: | ---: |
| RPC RTT p50 | 1.1243 | 0.9608 |
| Sample API p50 | 3.1529 | 3.1290 |
| MCU RPC active/call | 不适用 | 0.9858 |
| MCU Sample active/frame | 不适用 | 1.3231 |
| 32 B 单向 d2h goodput | 0.7400 | 1.2372 |

Sample API 的实际编码／接纳函数内部未加事件计数，仍出现三倍左右的耗时，说明扰动会延伸到未插桩的 Sample 接纳路径；代码布局／工作集、执行时序及 observer 交互仍需区分。当前没有 miss 计数，不能将它归因于 ICache。后续不使用 D 的 timing 估算可回收的 74 µs，也不把 D 的 owner pass 数冒充 A 的原生调度次数。

### D-on 实测账本决定首个候选

下表为三个 D-on 各自每单位计数的中位数。RPC 分母 10000，Sample 分母 1000；包括 START／STOP 边界控制流量。stream 计数包含准备和 drain，不能与其 5 秒 CPU 窗口混用。

| 项目 | 每 RPC | 每 Sample |
| --- | ---: | ---: |
| semaphore give | 6.0006 | 2.0060 |
| give 时 token 已满 | 4.5046 | 0.0290 |
| 实际进入阻塞分支 | 0.6306 | 2.9760 |
| take 超时 | 0 | 1.9990 |
| owner step | 2.3229 | 4.9890 |
| 0 事件 step | 0.7053 | 2.9870 |
| adapter service | 4.6458 | 9.9780 |
| active service | 1.0001 | 0.0010 |
| terminal 槽访问 | 16.3700 | 27.9640 |
| generation 快照槽访问 | 9.2916 | 19.9560 |
| ready 调用槽访问 | 8.7500 | 15.9560 |

全部 D step 都没有 outbound call；业务 handler 次数与 10000 RPC／200 低频 RPC 匹配。正常阶段无 RX abort、TX error、submit BUSY、terminal 回调；回收／错误仍由既有健康和 console 账本覆盖。本轮没有新增硬件故障注入或逐事件 trace。

**据此先试 O3 的空调用路径，再回到 O2。** 空调用表的扫描和 generation 快照是确定存在且可按状态跳过的工作；通知虽大量合并，却不能由此推出它是 CPU 主因。Sample 的空 pass 又包含应用 1 ms pacing timeout，不能当作可随意删除的通知冗余。该候选只增加 owner 私有 active-call count，保护当前轮 generation 快照与 F1/F2，不同时改 TX ready 队列、USB service 或通知；后续筛查及撤回结果见下节。

本节计数不足以给出 74 µs 的独占成本分解。O2 已在后续独立记录中评估，O4／O5 仍未执行。


## O3：空调用快路径 CPU 有下降，但延迟保护失败，撤回候选

候选只在 owner 内维护 `active_calls`：成功 start 加一，逻辑 finish 在 callback 前减一；取消、timeout／TX 错误标记和 close 保持计数，直到唯一终态交付；init/reinit 清零。没有调用时跳过 terminal／ready 调用槽扫描和 generation 快照。空入口使用空快照，即使 RX callback 创建或取消已到期调用，也不将新 generation 纳入当前轮。未同时修改 TX ready 队列、packet port、adapter 或通知。

候选通过 GCC Release、Clang ASan/UBSan 各 47/47 CTest；Zephyr core／USB 16/16 用例、UART／UDP socket／waiter 30/30 用例，后者 4 配置运行、1 个配置静态过滤。native_sim 实际采用 `-m32`。测试独立扫描调用状态核对派生计数，覆盖 start 失败、cancel、response、TX 错误、timeout、callback 重入、close/reinit、耗尽及 budget1–16；另加空入口 callback 创建／取消新 generation 的回归测试。

在新登记块 `O3-screen/` 中，A/B 产品固件均无新增探针，三轮顺序 AB／BA／AB，每轮 off/on 分开刷写重置，共 12 份 screen capture。1000 warmup＋10000 RPC、idle5s、200 低频 RPC、1000 Sample、32 B 三方向 stream 的工作量与 O1 对齐；A/B 四大小 smoke 和 A 的编码边界资格验证通过。逐 native worker 的 policy0、nice16、affinity0–5、系统功耗设置与 USB 拓扑门禁均通过，未修改调度条件。全部原始数据已独立重放；不是 O5 的完整 A/B/C。

| 指标 | A 三轮原值 | B 三轮原值 | 三轮配对 B/A 中位数 |
| --- | --- | --- | ---: |
| on RPC active，µs/call | 413.728／415.026／419.234 | 394.994／395.034／402.471 | 0.95472（下降4.53%） |
| off RPC p50，µs | 331.893／333.628／334.279 | 358.185／357.603／363.350 | 1.07922（回退7.92%） |
| off RPC p99，µs | 483.256／496.886／494.650 | 527.846／527.831／539.143 | 1.08995（回退8.99%） |
| on Sample API p99，µs | 7.4625／7.4625／7.4625 | 14.0292／14.0292／14.0292 | 1.87996（回退88.00%） |
| on Sample active，µs/frame | 158.657／158.624／166.678 | 152.063／152.005／143.957 | 不可判定：A/B跨度均超过5% |

`O3-screen/O3-analysis.json` 保留全部 CPU、延迟、goodput、idle、host 全线程 CPU、资源和每轮原值。重复保护失败有 off RPC p50/p99、off host RPC CPU、on Sample API p99 共四项；七项因参考波动不可判定，另有一次 off 双向 d2h goodput 异常。不能以 RPC CPU 下降掩盖这些结果，也不追加采样追求通过。

本候选的无调用分支在源码上不再访问上述调用槽；未制作 B 的诊断计数固件，不能将 O1 D 的 owner pass 次数当作 B 的实测减少量。A/B 六份 on 记录的 RPC IRQ 均为 20002、Sample IRQ 均为 1002，仍不能用 IRQ 次数解释每事件 CPU 或 Sample API 变化。

### 资源与机器码对照

`O3-resource-audit.json` 在删除编译产物前完成检查：port、native/RPC wire、USB adapter、生成 codec/kernel 和两个 observer 对象的 `.text*`／`.literal*` 内容均相同，只有 kernel 对象改变。A/B endpoint4408 B、port88 B、adapter152 B、静态 data/BSS 和 IRAM 均不变；B `.text` 增加80 B（off59616→59696，on61872→61952）。未迁入 IRAM、改变缓存、padding、编译／线程参数或业务工作量。该对照缩小变更范围，但不能把候选整体 CPU 差异拆成扫描的独占成本，更不能宣称 miss 根因。

**不采用当前 active-call 候选。** 已精确撤销本轮对 `src/kernel.c`、`include/wirelink/kernel.h` 的改动；产品保持单一 A 路径，无新增字段、hook 或 fallback。保留与候选无关的空入口 generation 回归测试（budget1–16、创建／取消两类），在恢复 A 后再次通过 GCC 和 Clang ASan/UBSan kernel 测试及严格 C11 warning 检查。

## 第一轮交回与后续安排

板卡已由同一 controller 刷回本轮 A-on：`kernel-usb-6b7697df31604f96-wirelink-on`，bin SHA256 `dc129aa2a42f32cb7a9d18e95e426e62f5664ccf1a7905bc7d8e4b1e2ad80275`；最后一份 `O3-screen/captures/return-A-on/` smoke 通过，采集器和通信句柄已退出关闭。

第一轮据此转向 **O2 的 USB callback 通知合并**，先完成发布／持久条件／最终 wake 的责任表与交错测试。优先研究 callback 内 RX commit、RX_REARM 与尾部 wake 的先后关系，而不是继续叠加空扫描快路径。尾部 wake 的停止确认职责必须保留；owner release 的资源通知也不能按“通常已满”一概删除。若拆分 activity 发布与外层通知，必须迁移正式 sample 与 benchmark 共用 adapter，使用无新增探针的 A/B 重新筛查。O2 的后续结果与当前下一步见独立执行记录；O4／O5 尚未执行，未对 libcsp 的120%目标作新结论。

### 已删除本轮废弃产物，未归档镜像

按用户要求，永久删除 D 的两个固件／host 构建、拒绝的 B 固件／host／重复 compiler 二进制、全部本轮软件检查构建树，以及 A 的中间对象、archive、CMakeFiles、Ninja cache 和生成发现链接。相关 Python cache 也已删除。没有压缩、备份或转移这些编译产物；只保留源代码／源补丁、原始 capture、注册和测试记录。删除明细见 `cleanup.json`、`python-cache-cleanup.json`。

本轮 A-off/on 的当前固件、gated host、生成物、配置及必要的 `--no-rebuild` 刷写元数据保留，构建身份验证与只读 flash context 检查通过。24 份已登记 O1／O3 screen capture 在删除后均可从 raw 独立重放，结果未变。D/B 的历史构建清单仍记录原镜像身份，但镜像已删除，不能再宣称可完整验证这些历史 build bundle；需要重建时登记新身份，不恢复旧缓存。
