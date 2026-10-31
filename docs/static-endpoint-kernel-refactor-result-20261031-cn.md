# 静态端点内核重构最终结果（K0–K6）

> 本页保留修复前 `68db823` 的测量与交付记录。审核发现的 F1/F2 调度缺陷及 F3 常规入口缺口已修复；当前修复源码、重新登记的完整矩阵与交回见[审核修复结果](static-endpoint-kernel-review-fixes-20261031-cn.md)。本页旧数字不改标签作为修复后结果。

日期：2026-10-31。按[执行计划](static-endpoint-kernel-refactor-plan-20261030-cn.md)完成默认产品迁移、最终源码重建、三轮完整 A/B/C 采集和独立重放。新内核是 Wirelink 0.13 的默认执行实现，旧引擎已删除。两项主要 A/B 目标分别改善 **71.903% / 69.555%**，超过本轮约 40% 的目标；15 项保护观察仍不可判定，部分 libcsp 目标未达，不能称为全部负载无回退或全面追平。

这次结果属于**新内核与已授权语义裁剪的组合收益**，不是只改变编译期 selector 的独立因果估计。本轮没有 push、发布、部署、修改外部工程 vendor 副本或搜索调频／cache／padding／inline／IRAM／调度组合。

## 当前产品合同与调用链

正式共享实现为 `src/kernel.c`、`packet_port.c`、`native_none.c`、`rpc_wire.c` 和 `types.c`。生成 C 只提供 typed codec、静态消息路由和容量。实际链路为：应用 typed start/send → 编码并快照 → core 有界队列 → packet port → 所选 adapter → 原底层 driver；RX producer 发布完整包 → owner step → 静态路由／handler／terminal callback。Executor、C++ Session、SDK 均调用同一 C core，没有第二套 RPC 匹配表、可靠链路或业务调度器。

| 保留合同 | 最终行为 |
| --- | --- |
| 无分配 C11 core | 不依赖 OS、driver、heap、thread 或 clock；owner 显式提供 `now` 和 budget |
| 调用与回复 | 四 outbound calls、四 reply snapshots、一 ordinary snapshot、九索引 admission FIFO；第五调用明确背压 |
| 输入与副作用 | 成功接纳返回前完成请求快照；解码请求／执行 handler 前预留回复；sink BUSY 保留回复，不重跑该 handler |
| 终态与结果 | 每个接纳调用恰好一次本地 terminal；响应借用到 callback 返回，需保留时当场复制；失败 admission 无未来 callback |
| 时间与重入 | deadline 先于 RX；callback 可 submit/cancel，禁止递归 step/close；操作身份不回绕复用 |
| RX／物理 TX | C 持有恰好两个 RX unit 和原子对象；单物理 TX，CONSUMED 与 DRAINED 独立；timeout/cancel 后仍保留被借用的源内存 |
| 生命周期 | close 拒绝新接纳；真实 driver stop/cancel、buffer 归还、callback/worker join，加上 core/port quiescence 后才释放或重新初始化 |
| wire | 普通 Compact-v1 DATA、native/NONE 和 managed v4 RPC session/operation/status 保留；RPC managed tagged，普通消息可显式 positional |
| connector | 显式 native/NONE、COBS/CRC16、length16/NONE；非二次幂 FIFO、pressure 与实际 byte gap 分开处理 |
| ABI | `wl_packet_submit_t`、`wl_kernel_outcome_t` 固定 `int32_t`，真实混用 `-fshort-enums` 的 C/C++ 消费者通过 |

ARQ／ACK handle／自动重传、replay/cache/TTL／重复抑制、服务端 pending/deferred reply token、通用 policy/service、mapped RPC、v1 binding/runtime 生成与旧 link selector 已删除。旧 DSL 明确报错。独立 FIFO/latest/outbox、frame/CRC/public codec 与冻结 wire vectors 保留，它们不恢复旧执行语义。旧 0.12 standalone public codec 的受限版本谓词不允许链接旧 runtime。

重复请求独立接纳后可以再次执行。丢响应无法证明远端未执行；业务重试必须自行定义 operation ID、幂等范围与冲突处理。长任务先同步返回应用 job ID，由应用 loop/status/完成消息推进；本地 timeout/cancel/close 不撤销远端副作用。详见[中文迁移清单](migration-0.13-cn.md)、[English](migration-0.13.md)、[API 与所有权](static-endpoint-kernel-api.md)。

USB 保留 native 边界、严格读 headroom、MPS/ZLP、stale token、fault latch 和真实 reset/reconfigure；没有重写 USBD/UDC 或 Astrial。UART DMA/IRQ/poll、serial、UDP、loopback 和 C++ 包装均有当前合同测试。**当前 Zephyr CDC API 无法证明物理 TX drain，packet 集成明确不支持**；没有以软件队列空冒充完成。

## 破坏性迁移与删除证据

[逐文件 CSV](static-endpoint-kernel-retirement-20261031.csv)包含 K5 入口到代码边界的全部 **358 项删除**及 80 项修改的 core／公开 API／adapter／runtime／构建／generator 文件，每行给出入口和最终 SHA、原因及替代位置。原型搬迁单独标为 promotion；其余新增文件及普通文档修改由 git 历史记录。[K5 迁移账本](static-endpoint-kernel-k5-migration-20261031-cn.md)保存各点的真实测试和修复记录。

旧 `src/protocol.c`、`rpc.c`、`rpc_async.c`、`endpoint.c`、`pump.c` 等 19 个执行／内部源文件及旧公开 owner 头已经删除。WLC 的旧 endpoint／managed/mapped／runtime/pending/cache/service 模板删除；原 `experimental/static_kernel` 不再保留第二套实现。旧特性专用 tests、fuzz、ARQ/Bulk/peer-cache/rx-backend workload 各有退役映射，当前合同及拒绝声明有真实测试，没有批量 xfail。

最终 ESP 构建审计证明每个 core 源和 USB adapter 只编译一次，均来自正式路径；没有 prototype source。最终 ELF 保留 `wl_kernel_call_start`、`wl_kernel_reply_reserve`、`ub_backend_request`、packet RX 和 USB submit 的真实符号，保留 client/server 两角色及四调用／四回复；`wl_init`、`wl_poll`、旧 RPC client/server/endpoint 和 Bulk init 符号均不存在。证据为 `final-B-structural-code-audit.json`、`final-B-o{0,1}-symbols.txt`、完整 compile/link 命令及安装消费者测试。

仓内 examples、Zephyr samples、checked-in SDK、host executor、Session、安装导出、CMake/Kconfig、benchmark registry/normalizer、fuzz、教程、双语公开文档及站点已迁移。基础 C-only 包不加载 C++ 或 thread；生成 helper 要求匹配的本地 WLC，不下载旧发布工具。应用的 object upload/job/status/retry 逻辑留在应用，不暗中恢复成框架 service。

## 实际输入、生成器与构建边界

证据根目录 **F**：`/home/ww/codings/wirelink-static-kernel-final-20261031/`。最终性能代码在文档收口前冻结；后续提交只改交付文档，不能把较新的文档 HEAD 冒充测量用代码 HEAD。

| 输入 | 最终实际边界 |
| --- | --- |
| Wirelink 代码 | `68db8235771f6d5d14e74007b79c48917351d3a7`，含实际原有脏文档输入 |
| 独立 WLC | `7eabbf4dd767f968123917ba02431fa0a0ef184e`，0.13.0 / codegen 0.13 / SDK API 3 |
| 独立 benchmark | `cd8b9cab0732d446128215c0daee2ce5fb84ce69`，正式 target/module/helper 集成 |
| Astrial | `7ffc5c34327d231e4bbca4fadb2157b8bba5a42d` 加原有 dirty driver 输入；全部 685 份文件归档／核对 |
| Zephyr | `ca10f7fc7a5b5b1129823cfc57506e81bb109743` 加四份原有平台修改；三个受审补丁在隔离树复现其字节 |
| libcsp C 控制 | `48f7fb0b57f610bf65bab1aa2d1357c3b9722782`，279 个常规源文件与冻结输入相同 |
| 工具链 | Zephyr SDK 1.0.1 Xtensa GNU、host GCC 16.2.1、CMake 4.4.5、Rust 1.96.1；当前完整版本见 `final-toolchain.json`，每次构建 flags 见 compile commands |

`entry/` 保存 K5 入口，`final-input/` 保存最终源码 tar 与逐文件 SHA，`final-source/` 是实际用于正式 B 构建的提取树。原有八份用户文档正文、benchmark 222 份 dirty 输入、Astrial 全部入口输入与原有研究文档保持原样；当前文档的历史 wrapper 与原正文分开核对。独立 WLC 工作区干净。`final-source-preservation.json` 记录最终代码无漂移及随后文档差异。

B 使用从匹配 WLC 源构建的静态 musl Release 可执行文件，SHA-256 `0093942ebd61119c8c63775b2a357b644bc9cf6309f15b26981eb464a2519d8d`；64 份 checked-in SDK 生成物确定性核对通过。实际 `west build`／CMake 命令、ELF/bin/map/`.config`/compile_commands/build.ninja、host runner 与生成 C 摘要在 `final-B-builds.json` 和 `devices/final-B-o{0,1}/`、`hosts/final-B-o{0,1}/`。

与 K4 已资格验证的 14 份 native 源／头比较，仅两个公开 header 因固定宽度 ABI 修复改变；四个核心 C object 的 `.text`/literal 与各自 K4 observer 相同。但最终链接布局改变，**因此完整重做受影响的 18 份矩阵**，没有套用迁移前原型数字。A/C 继续使用不可变的冻结控制镜像和 runner，重新采集它们的实际数据。A/B 的 wire/schema/业务 handler/Sample pacing/window/observer workload 保持登记范围；A/C 资源与能力差异如实保留。

## 最终完整测量与独立重放

注册文件 `final-validation/final-registration.json` 的 SHA-256：

`4ff395a808448838ead312f0a0ea0b4955ec2d1a2fe873c99a0f5f1dd33a492a`

三轮顺序分别 ABC、CBA、BAC；每组独立 observer off/on、重新 flash/reset，共 **18/18 有效完整 capture**，无替换或选择性补测。每份有 1000 warmup＋10000 RPC、5 秒 idle、100 Hz/200 calls、1000 帧至少 2 ms pacing 的 Sample，以及 32/120/512/1024 B × 单向 h2d/d2h/双向，每项 1 秒准备＋5 秒测量＋drain。三轮按轮配对，取三个比值的中位数，不池化样本，不混 observer。

ESP32-S3、240 MHz、16 KiB/8-way/32 B I-cache、`-Os`、LTO off、USB FS 12 Mbps/MPS64、两份 2048 B RX、read-depth/TX-inflight 各一、四 calls/replies、budget16。host main 及所有 I/O workers 均继承 policy0/nice16/affinity0–5；governor/EPP 冻结，warmup 前及测量中约每 50 ms 核对。没有更改这些参数，完整测量期间没有编译或运行其他 benchmark。

全部 capture 的身份、工作量、错误、pacing、字节／帧和线程环境账本通过。`final-replay.json` 独立从原始 JSONL/console/线程时间线与 SHA 重算全部 18 份、配对、中位数、稳定性及保护决定，结果与分析器一致。注册工具 hash 未在采集中途改变。manifest 中 `full_valid=true` 是本次完整合同判据；保留的 `formal_valid=false` 属历史 all-project schema 预留字段，注册已明确其含义，不改字段伪装旧全项目矩阵。

损坏输入负例实际拒绝缺 RPC、错误 Sample pacing、四槽不符、stream 账本不符、缺 1024 项、错误测量窗口；巨大 RTT outlier 保留而非裁剪。这些是标明 synthetic 的 validator 测试，不充作性能样本。

### 主要指标与 CPU

数值单位为 µs/call 或 µs/frame；表内 A/B 为三个独立轮次的值。

| observer／指标 | A 三轮 | B 三轮 | 配对中位降幅 | 判定 |
| --- | --- | --- | ---: | --- |
| off RPC RTT p50 | 1201.654 / 1179.978 / 1205.066 | 337.633 / 337.644 / 333.511 | **71.903%** | 主要目标达到 |
| on MCU RPC active/call | 1285.181 / 1271.447 / 1291.384 | 391.280 / 388.004 / 389.217 | **69.555%** | 主要目标达到 |
| on MCU Sample active/frame | 394.050 / 380.529 / 400.562 | 143.213 / 150.139 / 143.314 | 63.656% | A 的 max/min−1 超 5%，不可判定 |
| off RPC p99 | 1356.513 / 1327.419 / 1349.720 | 496.379 / 496.658 / 491.639 | 63.408% | 保护通过 |
| off host RPC CPU/call | 68.282 / 68.527 / 66.369 | 50.142 / 50.762 / 49.965 | 25.924% | 保护通过 |
| on host RPC CPU/call | 68.971 / 67.575 / 68.106 | 54.198 / 52.784 / 51.795 | 21.888% | 保护通过 |
| off host Sample CPU/frame | 45.043 / 40.176 / 41.393 | 43.461 / 44.207 / 45.362 | −9.590% | A 不稳定，不可判定 |
| on host Sample CPU/frame | 49.573 / 49.069 / 46.335 | 40.473 / 44.339 / 46.393 | 9.639% | A 不稳定，不可判定 |

host all-thread CPU 和 MCU active 独立测量，不相减、不合成“总体 CPU”。off/on host idle 分别变化 −0.011/−0.007 个百分点，on MCU idle 变化 −0.021 个百分点，均通过 0.5 个百分点保护。全部低频、Sample、stream host/MCU CPU 与 IRQ/thread 分量见[最终指标附录](static-endpoint-kernel-final-metrics-20261031-cn.md)、`final-validation/final-all-cpu.csv` 和原始采集。

### 保护项与 Sample 问题的边界

按事前登记：relative reference 三轮 max/min−1 超过 5% 则不可判定；稳定 A 下至少两轮超过 5% 回退才为重复失败，idle 使用 0.5 个百分点阈值。**重复保护失败 0、单次保护异常 0、不可判定 15 项**。这不等于所有保护通过。

不可判定项完整集合：off 的低频 p99、host 低频 CPU、host Sample CPU、512 与 1024 双向的两腿；on 的 host 低频 CPU、host Sample CPU、Sample API p99、MCU Sample active、32 双向 d2h、512 双向 h2d、1024 双向的两腿。各项每轮数值及 reference 波动均保留，不靠追加采集消除限制。

Sample API p50：off 的 A 150.675/147.512/147.275，B 2.317/2.987/2.317；on 的 A 139.554/139.504/143.938，B 三轮均 9.979。新 API 返回前编码快照，实际 USB submit 由 owner step 推进；旧 API 内已经进入 sink，故不能把 API 的全部降幅称为 codec 提速。最终 B 依然存在 on/off 绝对耗时差异。

九份 on capture 的 Sample USB IRQ 均为 1002。相同 IRQ 数量不能区分 API 路径、缓存压力与 observer 构建交互，也不能证明计时错误。本轮没有完成该定量归因；MCU Sample A 的不稳定进一步限制结论。最终核心 object 相同而链接布局改变，只能证明必须重新测量，不能证明布局是全部差异的原因。

### 吞吐与 libcsp

全部 payload／方向／observer 的逐轮 goodput、配对比值和稳定性在[附录](static-endpoint-kernel-final-metrics-20261031-cn.md)。32 B 的 B/A 配对 goodput 比值 off：h2d **2.370**、d2h **2.276**、双向两腿 **2.669 / 2.604**；on 为 **2.651 / 2.619 / 2.591 / 2.459**，其中 on 双向 d2h 因 A 不稳定不可判定，其余稳定改善。512/1024 的部分双向保护同样不可判定，不能用总吞吐掩盖一腿。

libcsp 的 latency/CPU 目标为 B/C≤1.2；goodput 则为 B/C≥1/1.2，不能混用。最终稳定的未达项为：

| 指标 | B/C 配对中位比 | 目标 |
| --- | ---: | --- |
| on MCU RPC active/call | **1.37358** | ≤1.2，未达 |
| off 32 B 双向 d2h goodput | **0.69199** | ≥0.83333，未达 |
| on 32 B 单向 d2h goodput | **0.82294** | ≥0.83333，未达 |
| on 32 B 双向 d2h goodput | **0.60481** | ≥0.83333，未达 |
| on 120 B 双向 d2h goodput | **0.78612** | ≥0.83333，未达 |

RPC RTT p50 的 B/C 为 off 0.81270、on 1.03362；MCU Sample 为 1.09862，均在各自稳定可判定范围达到相应目标。其余达到或不可判定项见附录。C 仍有原有 heap、buffer 和 route worker，原始工作量与资源没有为追数缩减。libcsp 120% 是后续对齐目标，不是本轮硬继续条件；上述差距没有被改写为“追平”。未对其原因作已证实的 cache/I/O/算法归因。

## 内存、代码、栈与观察范围

静态 RAM 口径为 ELF `.dram0.data + .dram0.noinit + .dram0.bss`，不是 allocator 峰值或进程 RSS。

| 资源，B | A off/on | B off/on | C off/on |
| --- | --- | --- | --- |
| 静态 RAM | 57224 / 57760 | **49984 / 50520** | 151952 / 152536 |
| Flash `.text` | 79824 / 82080 | **59568 / 61824** | 51952 / 54208 |
| `.iram0.text` | 45100 / 46372 | **45100 / 46372** | 45268 / 46536 |

A→B 静态 RAM 减少 **7240 B**、Flash `.text` 减少 **20256 B**，IRAM 相同。Xtensa endpoint 4408 B、port 88 B、USB adapter 152 B，RX 4096 B 另计；完整固件数字已计入应用与 adapter 新字段。`.text` 含冷路径，不等于热执行工作集、cache miss 或因果证据。

on 三轮最小 stack_unused：main（8192 B 栈）A/B/C 为 **4240/4912/5152 B**；USBD（1024）416/432/432，UDC 592/592/592，sysworkq 720/720/720，idle 784/784/784；C 另有 4096 B route worker、unused 3664。A/B allocator 原有 4 KiB heap 的峰值 allocated 为 52 B，C 原有 64 KiB heap 峰值 72 B；共同另一 allocator 峰值 204 B。静态区已包含这些配置，不能另加或把 free heap 增长当库内存节省。

host max-RSS 原始三轮：A off 28776/34564/34564、on 32700/34564/34564 KiB；B off/on 均 32700/34564/34564；C off 34404/34564/34564、on 均 34564。它们是当前 runner 报告的进程观测，含 runtime/driver/分配器，不能据此宣布库统一峰值改善。`final-resource-audit.json` 保存全部 ELF sections、endpoint symbol size、每线程栈、allocator 与 RSS；全部 stream CPU 比例及原始字节代价保留在附录／账本。

## 软件验证、失败与未执行范围

`final-test-ledger.json` 记录命令、阶段、日志 SHA、通过/失败/过滤/仅构建及执行范围；各迁移点的测试不冒充在最终 HEAD 全部重跑。

| 验证 | 结果与范围 |
| --- | --- |
| 最终 core/adapter Twister | unit_testing/native_sim/Cortex-M3/RV32/x86_64，37/37 执行配置、198/198 case；18 静态平台过滤，0 fail/error/warning |
| 最终 sample Twister | native/M3/RV32/x86_64，21 个执行配置、25/25 case；另 2 配置仅构建、9 静态过滤，0 fail/error/warning |
| 最终 WLC | 110 passed、0 failed、0 ignored；clippy all-targets/all-features `-D warnings`、fmt check 通过 |
| Linux CI 等价与安装 | 44/44；安装后 C/C++、matching WLC、真实 UDP 共 9/9；最终 parser focused 15/15 |
| 内核／owner／Session | strict GCC/Clang；ASan/UBSan、TSan 并发／关闭／manual clock 等逐点通过 |
| 固定宽度 ABI／packet tools | 实际 poisoned enum ABI regression 修复；混用 short-enum C/C++ 2/2；相关 Clang sanitizer 14/14；packet tools GCC 16/16、focused Clang 4/4 |
| SDK／教程／fuzz | 64 生成物一致、59 Python SDK tests、真实 C++ SDK／UDP 教程；六 fuzz ×5000，结构化 kernel/stream 各10000 次 |
| 文档／站点 | 编译教程与文档代码相等；Node 24.21.0，Astro 39 页、902 站内链接无断链；一条原有历史段落长度提示保留 |
| 实板 | 六组启动 smoke、18 完整采集、独立 replay、最后 B-on smoke 通过 |

典型重放命令：

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
cargo test --manifest-path wlc/Cargo.toml
cargo clippy --manifest-path wlc/Cargo.toml --all-targets --all-features -- -D warnings
cargo fmt --manifest-path wlc/Cargo.toml -- --check
# 从初始化的 /home/ww/zephyrproject 运行；完整 paths/options 见 Twister JSON。
west twister -T /home/ww/codings/wirelink/tests/zephyr/unit \
  -T /home/ww/codings/wirelink/tests/zephyr/integration \
  -p unit_testing -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64 \
  --extra-args WIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
```

历史失败日志未删除，包括错误 west cwd、fixture include/字段、关闭预期、generator warning、ABI 修复前 poisoned outcome、工具路径/Node 选择等。修复后只重跑受影响场景。Google Benchmark 1.9.4 与 Clang 的 `__COUNTER__`/C2y `-Werror` 诊断阻止整个 API Clang sanitizer 构建；GCC API sanitizer 和 core/adapter/Session Clang sanitizer 通过，没有修改 vendor 或压制 warning。

macOS/MSVC、manylinux 容器、远程 CI 和发布／部署未在本地执行；工作流和静态 musl WLC 核对不是这些平台的运行证明。WLC 独立源码 pin 需要在维护者后续发布流程确认远程可取，本轮未 push。模拟／受控 adapter 测试不宣称物理 UART、Ethernet 板对或 USB HS 已资格验证。

## 证据封存与最终硬件交回

F 内 `final-evidence-index.json`／`.sha256` 索引实际证据文件和 symlink；范围、排除的可再生缓存／构建中间物、外部工具／旧控制依赖在索引内明确记录。最终索引 hash 和独立 hash 重验结果见[交付封存记录](static-endpoint-kernel-final-seal-20261031-cn.md)。源码归档、raw captures、失败日志、生成 C、ELF/bin/map/config、编译链接命令、分析／重放／validator 源码均保留，不能把外部依赖只写一个 HEAD 当完整 dirty 输入。

旧 K2 证据 `/home/ww/codings/wirelink-static-kernel-20261030/` 的索引 SHA 为 `d9b0a0ef2b72df6c6d455aa873d6225dc546a0eba66ba0cace6077fc51cbef8a`；K3/K4 证据 `/home/ww/codings/wirelink-static-kernel-k3k4-20261031/` 为 `165ef78c765d267be13596354248ed5b270ccea9385c3c58044275ad09fd76e4`，均只读、不追加。K4 最初 nice 期望不符的中止与重新事前登记全部保留；本次最终块使用继承的 nice16，未更改调度。K2 阶段报告已[逐字保留](static-endpoint-kernel-k2-result-20261031-cn.md)，[K3](static-endpoint-kernel-k3-progress-20261031-cn.md)／[K4](static-endpoint-kernel-k4-result-20261031-cn.md)历史结果不混入最终数字。

| 最终板卡状态 | 值 |
| --- | --- |
| native serial | `10003B6DEC94`，USB FS 12/MPS64，拓扑 `3-2.4` |
| 交回实现 | **最终新 B，observer on** |
| build ID | `static-kernel-final-20261031-B-o1` |
| bin SHA-256 | `d9dc8892dce5fcbf850e2a39ab13c4c9b93a36fa9508336c7ac0bca46db8ef22` |
| 最后 smoke | `final-validation/captures/final-handoff-B-o1/`，独立 qualification，未计入 18 份性能样本 |
| I/O | `final-handoff.json` 记录动态发现的 USB address 和串口／USB fuser 无打开句柄；host I/O 已关闭 |

板卡保留最后一个功能合格的新镜像，未恢复历史 COBS/UART 固件。K5 产品迁移与 K6 的最终测量、独立重放、结果及交回据此完成；后续 libcsp 对齐、Sample observer/cache/path 定量归因和未执行平台验证，均作为明确限制交给维护者，不冒充本轮已解决事项。
