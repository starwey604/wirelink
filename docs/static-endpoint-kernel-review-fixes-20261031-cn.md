# 静态端点内核审核 F1–F3 修复与复测

日期：2026-10-31。针对[审核发现](static-endpoint-kernel-refactor-review-20261031-cn.md)完成 F1/F2 正确性修复和 F3 当前入口收口，并修复公平调度暴露的一处 USB 应用门禁依赖。默认实现保持同一个小内核，没有恢复 ARQ/replay/deferred/service。下文数据来自修复后的完整新块；[修复前 K6 报告](static-endpoint-kernel-refactor-result-20261031-cn.md)及其只读索引不改标签。

## 修复与提交

| 发现 | 实际修改 | 提交 |
| --- | --- | --- |
| F1 / P1，多调用同到期仍可成功 | `wl_kernel_step` 在 RX 前排空本轮 generation 快照内所有 eligible terminal；预算耗尽则 RX 留队。callback 新建调用仍不参加旧 generation 到期扫描 | 根仓 `7666815` |
| F2 / P2，budget=1 RX 饥饿 | 持久化 RX/TX turn；TX submit 后的 RX 机会跨 completion 和 owner pass 保留。物理 completion、terminal 优先；空／背压 RX 不挡 TX。继续支持 1–16 全部预算 | 根仓 `d7e3941` |
| 受影响 USB echo/object 应用 | 删除“step=1 必先 TX”的隐含门禁；pending output 在 route 返回 WOULD_BLOCK，副作用前施压；有限三事件预算允许 completion、blocked RX、TX submit，避免消耗唯一 wake 后停滞 | 根仓 `5453d03` |
| F3 / P2，常规旧入口依赖已删 runtime | 显式退役旧 Wirelink CLI/CMake 入口，提供统一当前 builder 与独立当前 manifest validator；直接构建正式 `usb/static_kernel` target/module 与 profile v2 | 独立 benchmark `73ad75e` |

F1 新增双／四调用同到期、边界前／恰好边界／边界后、uint32 回绕、cancel 与 timeout 并存、callback 新 generation、唯一终态及迟到响应测试，覆盖 budget 1–16。F2 新增持续双向 telemetry＋已到达 RPC response，分别使用同步 SENT 与分开的异步 CONSUMED/DRAINED，全部预算都实际推进 RX/TX。原实现及 F1-only 版本分别触发新增回归断言；两份审核最小复现原来均 exit2，修复后均 exit0，两个同到期调用均 TIMED_OUT，budget=1 的响应成功且不留在 RX 队列。

RX 公平性保证不等于任意外部调度下的响应耗时保证；eligible terminal 和物理完成仍优先。`rx_turn` 是 owner 私有字段，需共同重建消费者；本次 Xtensa padding 容纳它，endpoint/core 总尺寸没有增加。内核不增加 heap、线程、clock、关联表或新 wire。

## 当前 USB cross-benchmark 命令

在相邻 `wirelink-cross-benchmark` 的实际工作树运行：

```sh
python -B scripts/build-wirelink-usb-fs.py --out /absolute/new-archive \
  --observer off --wlc /absolute/matching-wlc
python -B scripts/build-wirelink-usb-fs.py --verify /absolute/new-archive/build.json
```

on 使用 `--observer on`。旧 `build-usb-fs.py --library wirelink`、旧 Wirelink host/device CMake 在调用已删 helper 前明确拒绝并给出新命令；不恢复 `wirelink_wlc_generate_runtime`。历史非 Wirelink 对照／manifest 仍单独标明，不能把新格式塞进旧 24-capture 验收。

新格式为 `wirelink-usb-kernel-build-v1`，构建本身只作 qualification。builder 归档实际 dirty 源码、matching WLC、完整生成物、ELF/bin/map/config/DTS、compile/link 命令和 source drift；validator 核对 profile2、双方角色、4 calls/4 replies、16 budget、1036 B frame＜2048 B read、单份正式 core/adapter、无旧引擎符号、双方 identity、所有产物 hash。新格式及损坏 metadata／未登记生成头等负例真实执行。两种 observer 的实际 host/device 构建与归档验证均通过，未以 mock 配置代替。

完整测量使用相同冻结 gated main.cpp；on 通过 builder 的显式 `--host-main` 登记，off 在默认入口实构建通过后额外构建同一 gated host。覆盖的只是 harness gate／结束标记，schema、业务 handler、payload、Sample pacing、observer 和窗口没有修改。实际 build 输入与后续 code-head freeze 逐文件相等；off 构建发生在提交相同源码字节之前，其 dirty diff／HEAD 如实保留，未伪报从干净 HEAD 重建。

## 软件验证与失败记录

- 严格 GCC 默认 **28/28 CTest**；Clang ASan/UBSan 含 core、owner、Session、UDP、教程和 USB 应用 **45/45**。
- Zephyr unit_testing/native_sim/Cortex-M3/RV32/x86_64 **37/37 执行配置、200/200 case**，另 18 静态过滤，0 failed/error/warning。
- 未修改 Rust generator；WLC kernel suite **7/7**，当前两种 build 的 metadata/实际产物验证 **2/2 各自通过，无跳过**。
- 审核 minimal repro 修复前后、定向回归失败、首次 sanitizer 暴露 USB gate 依赖及修复后全过、首次 host 选择 Makefiles 导致归档缺 build.ninja 的失败均保留。builder 已显式选择 Ninja；失败归档没有覆盖。最初 fixture 错拼 API 名称的编译拒绝另作尝试说明，不冒充产品运行失败。

完整命令、日志 hash 与范围在 F 的 `software-validation.json`、`reproductions/`、build/test logs。F=`/home/ww/codings/wirelink-static-kernel-fixes-20261031/`。没有把旧 110 个 WLC tests 或此前 TSan／SDK checks 改标签成本次重跑；远程 CI、MSVC/macOS、manylinux、物理 UART/Ethernet/HS 未执行。

## 修复后完整矩阵与环境边界

有效数据仅来自 `repair-environment-rebase/`，注册 SHA-256：

`44c9d6d556208bc7d282bf40becfe62de5ec49883cb02a88928008a999f4e959`

三轮 ABC／CBA／BAC，每组 off/on 独立 flash/reset，共 **18/18 完整 capture**。每份 1000 warmup＋10000 RPC、idle5s、100Hz/200 calls、1000 paced Sample≥2ms、32/120/512/1024 B ×三方向，各1s准备＋5s测量＋drain；按轮配对，不池化、筛尾或混 observer。全部 raw ledger、identity、线程时间线及独立 raw arithmetic replay 通过。

最初复制的 registrar 错误期望 nice16，而六组资格验证的 native threads 实为−4，登记前检查拒绝；另一次准备路径失败也未开始采集。按真实 −4 登记后，首个 full A 在 warmup 放行前实际变为16，门禁停止，只记录 identity/setup/health/research_ready 四个事件，**有效性能样本为零**。这些文件、登记和失败均保留，未作为新矩阵输入。

只读检查发现 ananicy-cpp active，但没有证明它是优先级变化的具体原因。随后预先记录一次常驻 controller 方案：同一 Python PID 完成六组资格验证、登记和全18份；所有 native main/worker 为 policy0/nice16/affinity0–5，事前及每50ms核对，governor/EPP 一致。没有 renice、改 affinity、调频或停服务。该块通过不构成优先级变化根因的证据，也不将不同块绝对值直接相减。

### A/B 主要结果

单位为 µs/call 或 µs/frame，A/B 均保留三轮值。

| 指标 | A 三轮 | 修复 B 三轮 | 配对中位降幅 | 判定 |
| --- | --- | --- | ---: | --- |
| off RPC RTT p50 | 1216.639 / 1191.245 / 1178.088 | 334.718 / 333.515 / 332.665 | 72.003% | 可判定 |
| on MCU RPC active/call | 1299.046 / 1288.725 / 1300.288 | 413.663 / 407.043 / 415.354 | 68.156% | 可判定 |
| on MCU Sample active/frame | 385.061 / 391.750 / 396.450 | 158.615 / 158.809 / 158.645 | 59.462% | 可判定 |
| off RPC RTT p99 | 1361.459 / 1338.877 / 1314.757 | 508.238 / 492.506 / 481.570 | 63.215% | 可判定 |
| off host RPC CPU/call | 67.429 / 67.313 / 66.276 | 49.251 / 47.551 / 47.443 | 28.415% | 可判定 |
| on host RPC CPU/call | 69.465 / 68.021 / 67.500 | 56.094 / 54.720 / 54.672 | 19.248% | 可判定 |

三个主要／期望指标本块都稳定、超过约40%目标。仍属于静态内核＋已授权语义裁剪的组合收益，不是 cache miss 的独立因果估计，也不是修复前 B 与修复后 B 的同块配对。

重复保护失败 **0**、单次保护异常 **0**、不可判定 **15**。max/min−1＞5%的 reference 保持不可判定；不能称为所有保护通过。off/on host idle 分别变化 +0.000606/−0.002009 个百分点，on MCU idle −0.021751 个百分点，均在0.5个百分点阈值内。32 B 八个分量本块 A 均稳定，B/A off 为2.415/2.326/2.793/3.153，on 为2.304/1.992/2.600/2.408（依次h2d、d2h、双向两腿）。

不可判定的完整集合：

- `B/A-o0:low_p99_us`
- `B/A-o0:host_low_cpu_us_call`
- `B/A-o0:host_sample_cpu_us_frame`
- `B/A-o0:stream_s512_d3_h2d_bytes_s`
- `B/A-o0:stream_s512_d3_d2h_bytes_s`
- `B/A-o0:stream_s1024_d3_h2d_bytes_s`
- `B/A-o0:stream_s1024_d3_d2h_bytes_s`
- `B/A-o1:host_low_cpu_us_call`
- `B/A-o1:host_sample_cpu_us_frame`
- `B/A-o1:sample_api_p50_us`
- `B/A-o1:sample_api_p99_us`
- `B/A-o1:stream_s512_d3_h2d_bytes_s`
- `B/A-o1:stream_s512_d3_d2h_bytes_s`
- `B/A-o1:stream_s1024_d3_h2d_bytes_s`
- `B/A-o1:stream_s1024_d3_d2h_bytes_s`

### libcsp 与尚未定位项

稳定可判定的未达项如下；latency/CPU 用 B/C≤1.2，goodput 用 B/C≥1/1.2，不能互换。

| 指标 | B/C 配对中位比 | 目标 |
| --- | ---: | --- |
| B/C-o1 / mcu_rpc_active_us_call | 1.460380 | ≤1.2 |
| B/C-o1 / mcu_sample_active_us_frame | 1.216530 | ≤1.2 |
| B/C-o1 / stream_s32_d2_d2h_bytes_s | 0.620715 | ≥0.833333 |
| B/C-o1 / stream_s32_d3_d2h_bytes_s | 0.601082 | ≥0.833333 |
| B/C-o1 / stream_s120_d2_d2h_bytes_s | 0.800330 | ≥0.833333 |
| B/C-o1 / stream_s120_d3_d2h_bytes_s | 0.803107 | ≥0.833333 |

off 没有稳定未达项，不代表 off 全部达到；其不可判定项仍见[完整指标附录](static-endpoint-kernel-review-fixes-metrics-20261031-cn.md)。尤其 on MCU RPC 约1.460倍、MCU Sample约1.217倍，32 B d2h约0.621倍，差距明确保留。libcsp 120% 是后续对齐目标，不是本轮硬继续条件；没有搜索布局／队列／inline／调度组合追数。

Sample API 的 B p50 off=2.317、on=3.783 µs；on 的 A p50/p99 仍不稳定。新 send 返回前编码快照，USB submit 由 step 推进，API 整段降幅不能称为纯 codec 提速。相同 IRQ 数量不是缓存／路径／observer 或计时错误的归因证明；本轮未完成该定量定位。整段 MCU Sample active 的本块参考稳定并改善59.462%，不会反过来改写旧块不可判定的结论。

## 资源、源码与交回

| 资源，bytes | A off/on | B off/on | C off/on |
| --- | --- | --- | --- |
| 静态 RAM data+noinit+bss | 57224 / 57760 | 49984 / 50520 | 151952 / 152536 |
| Flash .text | 79824 / 82080 | 59616 / 61872 | 51952 / 54208 |
| IRAM .text | 45100 / 46372 | 45100 / 46372 | 45268 / 46536 |

A→B 静态 RAM 减少7240 B、Flash .text减少20208 B；相对修复前 B 的 .text增加48 B，静态 RAM／IRAM不变。Xtensa endpoint仍4408 B。main（8192 B）最小 stack_unused A/B/C为4240/4848/5152 B，B 的栈观察不能套用旧值4912。所有18份 runner max-RSS均报告34336 KiB，这是当前进程观察，不能据此认定库内存相同或得到独立峰值改善。全部 ELF sections、线程栈／allocator／RSS 在 `resource-audit.json`；全部 phase/stream CPU 和字节账本在 `repair-environment-rebase/all-cpu.csv`、raw JSONL及 analysis 中，没有省略较差分量。

| 实际源码边界 | HEAD |
| --- | --- |
| Wirelink | `5453d035a11cbc1136164b42a30096bae3e8bfcb` |
| 独立 WLC | `7eabbf4dd767f968123917ba02431fa0a0ef184e` |
| 独立 benchmark | `73ad75edba2e1452fbf8d2f6a445b2ceabe5a32f` |
| Astrial＋原 dirty 输入 | `7ffc5c34327d231e4bbca4fadb2157b8bba5a42d` |

`entry/` 保存修复前实际输入，`final-input/`／`final-source/` 保存修复代码及逐文件 hash，当前两份 builder archive 保存真实构建期间输入与命令；WLC静态工具 SHA `0093942ebd61119c8c63775b2a357b644bc9cf6309f15b26981eb464a2519d8d`。Zephyr仍为 `ca10f7fc7a5b5b1129823cfc57506e81bb109743` 加四份既有平台修改；C仍为同一冻结镜像。kernel object改变，其余 packet port/native/RPC wire object及业务/observer/schema字节核对通过，故重做完整矩阵。构建与 ELF证据证明只有一份正式core，client/server均保留，无旧引擎 fallback。

原有9份 tracked脏文档（含本次入口已存在的站点修改）和93份未跟踪输入、Astrial685份输入保持原样。按F3授权差异修改并开始跟踪三份既有 benchmark 脏文件：旧builder、host/device CMake；原件与补丁归档，其余219份原始脏输入 hash未变，未批量提交它们。独立WLC干净。后续文档提交不冒充测量代码HEAD，源码漂移和保存检查见 `source-preservation-and-closeout.json`。

板卡交回最终修复 **B-on**，native serial `10003B6DEC94`，USB FS12/MPS64、拓扑3-2.4。build ID `kernel-usb-d63e0c5f6465c96b-wirelink-on`，bin SHA `8bf6481d13f0de8f5249fb36a11c14c14488ae6c01cd5ab0c691fedfe8da41c0`。`handoff/captures/review-fixed-B-o1/` 的独立smoke通过，未计入18份性能样本；`final-handoff.json`记录USB/串口fuser无打开句柄。未恢复历史固件，未push、发布、部署或修改其他工程vendor。

证据索引与独立重验 SHA 见[修复封存记录](static-endpoint-kernel-review-fixes-seal-20261031-cn.md)。原 K2/K3/K4/K6 证据目录继续只读，修复前最终索引仍为 `12fcd9f873495e3b06dd31d351fe5115a438f4edcab193d76c9cf726e9daea5c`。复现、预登记失败、被门禁阻止的尝试、有效完整块及最终交回分开保存，不覆盖或重贴旧数据标签。
