# 静态端点 CPU 优化执行记录

日期：2026-11-01。按[CPU 优化计划](static-endpoint-cpu-optimization-plan-20261031-cn.md)执行，当前完成 O0 和 O1 首轮，首个产品候选选择 O3 空调用路径。

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

Sample API 的实际编码／接纳函数内部未加事件计数，仍出现三倍左右的耗时，说明整套诊断构建的扰动包括代码工作集和执行时序变化。当前没有 miss 计数，不能将它进一步归因于 ICache。后续不使用 D 的 timing 估算可回收的 74 µs，也不把 D 的 owner pass 数冒充 A 的原生调度次数。

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

**先做 O3 的空调用路径，再回到 O2。** 空调用表的扫描和 generation 快照是确定存在且可按状态跳过的工作；通知虽大量合并，却不能由此推出它是 CPU 主因。Sample 的空 pass 又包含应用 1 ms pacing timeout，不能当作可随意删除的通知冗余。首个候选只增加 owner 私有 active-call count，保护当前轮 generation 快照与 F1/F2，不同时改 TX ready 队列、USB service 或通知。

O2／O4／O5 尚未开始；本轮计数还不足以给出 74 µs 的独占成本分解。
