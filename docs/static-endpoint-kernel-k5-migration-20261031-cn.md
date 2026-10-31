# K5 默认产品迁移与删除记录

K4 的 18 份完整采集及独立重放通过，见 [结果与限制](static-endpoint-kernel-k4-result-20261031-cn.md)。K5 默认产品迁移已经完成，并完成最终源码的独立完整硬件确认；K6 的交付状态和证据索引见[最终结果](static-endpoint-kernel-refactor-result-20261031-cn.md)。以下逐节保留当时的进度，不把早期“待完成”解释为最终状态。入口根仓 `cefa8aa`、WLC `af819f7`、benchmark `27d517b`，dirty 输入归档于 `/home/ww/codings/wirelink-static-kernel-final-20261031/entry/`。此前 K3/K4 索引不再追加。

## 逐项替换责任

| 旧文件／对象 | 正式路径与决定 |
| --- | --- |
| `src/protocol.c`、`init.c`、`transport.c`、`context.h`、`link_frame.h`、`link_profile.h` | 删除 link ARQ、ACK、event／TX handle 引擎；由 `src/packet_port.c` 与 `src/kernel.c` 承载 I/O 所有权和一次通信推进 |
| `src/rpc.c`、`rpc_async.c`、`rpc_internal.h` | 删除两层调用表、replay/cache/TTL、服务端 pending／deferred；统一四调用、四预构造回复；托管 wire 在 `src/rpc_wire.c` |
| `src/endpoint.c`、`pump.c`、`schedule.c` | 删除 policy/service/event 通用编排；WLC 静态路由进入 kernel，业务推进由应用 owner loop 管理 |
| `src/unit_frame.c`、`unit_rx.c/.h`、`rx_ring_bipbuf.c/.h` | 旧引擎 RX/TX 层删除；两个显式 RX unit 和物理 TX lease 归 packet port；stream 增量分帧独立 |
| `src/bulk_sender.c`、`bulk_receiver.c` | 删除依赖旧 owner／ARQ 的 bulk 执行状态机，普通分块消息在应用推进；不在新基础内核重建策略 |
| `src/cobs.c`、`crc.c`、`frame.c`、`frame_cobs_crc16.c`、相关内部头 | 保留为独立 framing／primitive 目标；完整 ACK／历史 wire 向量是工具能力，新的端点仍明确拒绝可靠 DATA／ACK |
| `src/fifo.c`、`latest.c`、`outbox.c` | 保留独立纯数据目标及其测试，不能依赖旧 pump 才使用；不把它们重新接成基础内核策略 |
| `experimental/static_kernel/src/*`、`include/wirelink/*` | 迁到正式 `src/`、`include/wirelink/`；实验目录不再保留第二套代码或永久 FAST 开关 |
| `experimental/static_kernel/adapters/astrial/*`、`asio/*`、`zephyr/*` | 替换正式 Astrial serial／USB、Asio UDP、Zephyr UART／USB／UDP adapter；保留底层驱动、borrowed API 和物理 drain 的真实合同 |
| `include/wirelink/link.h`、`endpoint.h`、`pump.h`、`rpc*.h`、`bulk.h`、`adapter.h`、`port.h` 等旧 owner 接口 | 移除安装和公开声明；新公开入口为 packet port、kernel 与生成 typed endpoint；不以同名 facade 包旧引擎 |
| WLC `managed_rpc_codegen*`、`mapped_rpc*`、`runtime_codegen*`、旧 endpoint／RPC runtime 模板 | 删除旧执行输出；默认 profile v2 生成静态 kernel。保留 schema/public codec 和纯类型／边界分析，不保留可达旧运行时 |
| `runtime/host`、`bindings/cpp` 与 WLC SDK | 迁移 owner／等待与同步异步外层包装；包装只排队 owner 操作并复制回调结果，不建第二套通信关联状态机 |
| 根／Zephyr CMake、Kconfig、cmake 包与生成 helper | 默认及安装仅新内核；旧生成器／旧 DSL 明确失败；可选 adapter 与纯工具分别导出 |
| examples、samples、benchmarks、fuzz、tests | 逐个迁移保留能力；旧 replay／ARQ／deferred 专用测试删除并记录原因；保留冻结 codec／wire vectors，补新合同与不支持诊断测试，不能批量 xfail |
| 默认说明、教程与 SDK 文档 | 更新公开承诺并提供中英文迁移清单：业务重试／幂等、重复执行、长任务 job ID、callback 借用期限和本地取消 |

上表先登记删除／替换边界；后续提交记录实际路径和验证。最终逐文件 diff 从入口 `cefa8aa` 导出，不能把“未默认链接”冒充旧执行源码已经删除。

## 迁移期间约束

默认 C11 core 不读取时钟、不创建线程、不分配 heap；只把 K3 合格实现迁到正式路径，不为已有结果调整缓存、IRAM、队列容量、优化或业务负载。八份原有 dirty 文档和既有未跟踪研究资料保留；必要的文档修改分别记录，不将用户改动整块提交。WLC 与 benchmark 独立提交。

最终源码必须重新构建并复核：旧执行符号／生成模板不可达、默认安装包 C/C++ 与 SDK、所有保留 core／adapter 合同、主要 RPC/Sample 与 32 B 保护。若 native core 机器码／队列或 host 工作量实质改变，扩大到受影响完整矩阵；不能拿删除前原型成绩替代最终确认。

## 默认 C core 与正式源文件收口

已将隔离目录代码迁至正式 `src/`、`include/wirelink/`、各 adapter 和 `tests/core/`／`tests/fixtures/static_kernel/`，移除原型 CMake／包，根 CMake 仅构建新内核；根 `Wirelink` 安装包及 `Wirelink::wirelink`、独立工具导出已切换。删除 19 份旧 C 引擎源／内部头和旧公开 owner／RPC API，保留原始 error 数值及 framing 格式。frame 工具剥离 `wl_config_t` 和旧 link profile 依赖，保留独立 CRC／COBS／ACK wire 向量。FIFO/LATEST/outbox 仍为独立纯数据目标，不进入基础内核。

默认 C 例程使用确定性的 checked-in WLC 输出，双端各发起四并发 RPC 并发送 telemetry，调用返回后销毁源值，最终 close/drain 到 quiescent。源码核对确认 14 份 native core／USB 源与头字节等同 K4 冻结输入；最终机器码与性能确认仍待完成。

验证：默认 GCC **11/11**；含真实 host adapter 的 GCC 与 Clang ASan/UBSan 各 **14/14**；安装后 C/C++/COBS **3/3**；纯 C-only 配置／安装没有启用 C++ 编译器；Zephyr 纯核心、独立容器与 frozen-v1 framing 向量 **50/50**；真实 UART/UDP/USB focused adapter 在 native_sim、Cortex-M3、RISC-V 合计 **15/15**，四个不支持组合正常过滤。WLC 的 5 项生成检查在迁入正式路径后以 Clang sanitizer 通过。日志在新证据目录的 `core-*`、`all-adapters-*`、`asan-*`、`consumer-*`、`c-only-*`、`twister-*`。

本点不代表 K5 完成：WLC 默认生成／SDK、Zephyr 默认模块、host executor、其余消费者和公开文档仍在后续迁移范围；不能用旧执行生成物作新产品交付。八份原有 dirty 文档未修改或整块提交。

## Zephyr 默认模块与旧 adapter 清理

Zephyr `CONFIG_WIRELINK=y` 已只链接新 kernel／packet port；COBS、length16、framing、独立容器、固定池与 waiter 分别选择。UART DMA/IRQ/poll、USB、UDP 通过正式 Kconfig 进入同一个模块。focused fixtures 现在通过默认模块集成，不再手工并排编译原型源。保留既有 IRQ callback 平台属性映射，删除只引用旧 endpoint/pump 的 owner IRAM 脚本，没有将新 core/codec 搬到 IRAM。

旧 Astrial serial/USB、Asio UDP、Zephyr adapter、旧 loopback 实现与头、旧 USB config/link-apply helper 均删除，改用已迁移的 packet bindings。旧 adapter 专用集成目录被新实际 adapter 合同 fixture 替代；原始文件名单和原因在 `retired-adapter-tests.json`，没有批量 xfail。低层 socket 的 18 项合同保留，claim/commit/abort 场景改为新 packet port；四生产者通知、满槽屏蔽、eventfd 耗尽、ISR 拒绝、错误分类、截断和 stop/reopen 继续真实运行。

独立工具复核后保留 `wl_allocator_t`／固定池和 `wl_waiter_t`／纯 deadline 算术，它们没有旧 RPC 表、线程或 clock fallback；API 注释改为调用方显式管理。无分配诊断 writer 与 FIFO/LATEST/outbox 格式保留，旧 link/replay/bulk 记录删除，新 terminal 仅输出 outcome/status，整数范围、截断及 sizing regression 在 GCC/Clang sanitizer 通过。

默认模块实际通过 **96 个 case/platform**：UART 三平台及 USB 13，native32/64 UDP 与 waiter（含32-bit timeout）29，socket 三平台54；固定池另 **3/3**。首次 Twister 在非 west 工作区启动被拒，随后 5-config 块中 UDP 缺少 fixture 私有 include；补跑块又遇到新 port 断言误写 `payload_length`（实际 `length`）。失败日志均保留，修复后只补跑受影响场景：默认模块其余4配置合格、UDP/waiter7配置合格、socket3配置合格。没有修改内核以绕过测试。独立 native platform/storage 的纯 C 构建与安装也通过。

## 可选 host adapter 安装导出

`Wirelink::asio_udp`、`::astrial`、`::astrial_usb` 分别导出为显式组件；核心包不加载 C++／Threads／Astrial。Astrial 由应用提供真实 backend target（或 `WirelinkBackend::astrial[_usb]`），不修改／捆绑外部 driver，缺失时明确 configure 失败。保留 driver 的公开 include 和链接依赖；首次将其误置 private 导致 fixture 找不到 `astrial/Usb.hpp`，已修正为 public，并保留失败日志。新增 pkg-config 核心导出，仅链接新 kernel／port／wire／types。

安装后真实 UDP／受控 USB／PTY serial **3/3**；缺 backend、缺 C++ 的特定诊断检查通过；含全部 C++ 组件的安装上，C-only CMake 与 pkg-config **2/2**，没有启用 C++ 或安装旧执行头。证据 `adapter-export-*`、`adapter-consumer-*`、`adapter-component-diagnostics.log`、`c-core-consumer-*`。下一点仍包括默认 WLC／SDK、host executor 和其余消费者，K5/K6 尚未结束。

## Host owner 与 C++ Session

Executor 已改为同一个 C kernel 的线程／等待包装：32 项有界命令队列、每轮最多 8 项命令，然后推进 core；合并外部 I/O readiness 和 core deadline，没有 RPC 表、replay、业务调度或协议计时副本。stop 在 owner 拒绝未执行命令，close core，再停止／join 真实 I/O 并等待物理 lease quiescent。notify 需要非阻塞、锁存、线程安全且不重入 Executor。初始化仅绑定，start 成功前 core 仍归调用方。删除旧 endpoint executor fixture 与旧活动计数器头。独立 host_profile 工具保留为应用显式采样，不宣称新 core 自动插桩。

C++ Session 使用 C-owned port／endpoint 描述符和真实 Asio UDP；submit 等到 owner 接纳，成功返回时请求已快照，失败不创建 core 调用。Operation 自持到唯一终态，回调借用期内复制 typed response，结果可在 Session 关闭后读取；取消仅排队本地取消，不回滚远端执行。业务拒绝与本地错误独立，保留原始 status。外部 close 等待入队者退出并 join owner；owner 内 close 只请求停止，禁止在 owner 回调销毁／替换 Session。手动 clock 经显式 notify 唤醒；last_error 保留底层失败。host／cpp_host 是显式安装组件，C-only 包不因此加载 C++。

GCC 全部 **17/17**；Clang ASan/UBSan **17/17**，新增手动 clock 跨 UINT32_MAX 场景随后单独 **1/1**；GCC TSan owner／Session **2/2**；安装后的独立 C++ 消费者 **1/1**。真实 UDP fixture 覆盖四并发／第五次 WOULD_BLOCK、请求快照、业务 -31 拒绝、本地超时／取消、丢弃 Operation 的安全自持、并发关闭、关闭后读结果；owner fixture 覆盖四生产者、队满、非致命 I/O 错误下 deadline 继续推进、注入 bad_alloc、关闭拒绝线程归属和物理 quiescence。失败的早期 fixture 与修复日志保留在 `cpp-*`。这是外层包装收口；WLC SDK 生成迁移仍待完成。

## 默认 WLC、生成合同与 SDK

WLC 升为开发版 0.13.0／codegen 0.13／SDK binding API 3。`compile --profile` 与 `compile-kernel` 使用同一个静态 generator；无 profile 只生成纯 public codec。所有公开 analysis／validate／identity／plan 入口统一检查 profile v2，旧 v1、mapped、guarantee（含 correlate）、reliable、LATEST/FIFO/deferred 明确失败。内部共用的名字／codec resolver 不是旧运行时。ordinary 保留显式 positional public codec；RPC 按计划统一 managed tagged，positional RPC 明确要求迁移，不做无声 wire 替换。输出目录若带旧 runtime/bindings/advanced 文件则拒绝覆盖，SDK 也要求显式迁移到新目录。

删除 WLC 的旧 endpoint／transport／RPC／managed／mapped／runtime backends、pending/cache/service/layout 模板、旧 link bindings 输出和旧 RPC resource planner。`plan` 直接引用实际 kernel deployment 资源，再给出 codec 字段事实。保留独立 public canonical helper，它们没有通信执行依赖，kernel 不调用、不生成 replay hash。纯 codec 的完整值域继续测试；Clang 严格检查发现无界 message measure 包装、未走 scalar path 的 uint32 writer 和未调用的 hash writer，收紧生成条件后 sanitizer 全块通过。不是压制 unused warning。

根核心版本同步 0.13.0；旧 0.12 仅允许冻结 standalone public codec 的 contract predicate，旧执行头／引擎已经移除，不能借此编译旧 runtime。安装的 helper 只接受匹配本地 WLC，不假定已有新发布包、不下载旧工具；`wirelink_wlc_generate` 生成同一个 endpoint，旧 separate-runtime/layout/bootstrap helper 删除或明确失败。`module-name` 与实际输出共享 C identifier 规范，keyword 文件名真实编译通过。安装后 native C／C++、COBS、keyword codec 以及旧 profile／旧 compiler／缺工具诊断 **5/5**。

SDK 的 C bridge 拥有 port 原子对象与两份 RX unit，typed callback 在借用期内复制 response；C++ sync/async 使用 Session 和同一个 C kernel，不再生成 invoke／第二套 RPC 表。同步等待拒绝 owner 线程调用，失败 admission 不生成未来 terminal。Python native 暴露 outcome/status/OS，不伪造已删除的错误域。两个 checked-in SDK 从新编译器确定性生成（64 份文件比对通过），schema 业务内容未变；C++ 同步／异步／嵌套数据／关闭结果 **3/3**，wheel 中 Python 同步／asyncio／双 SDK 隔离 **59/59**。恢复了新 factory 的初始化失败／无效地址／端口占用回滚测试；旧 HeldOwner/RPC queue fixture 被当前 host owner 与 kernel 四槽测试替代。

旧特性测试逐文件记录在 `retired-wlc-tests.json`。managed wire 的合法 payload 向量、完整 metadata ID/session/status 范围移植到新 primitives；只删除旧 inspect/release/长期终态槽相关执行部分。完整 Rust/GCC 块只因该移植 fixture 缺共享 metadata 头 失败，补跑通过；其余保留纯 codec、schema、SDK、新 kernel 测试通过。Clang ASan/UBSan 完整块通过，新增当前 CLI/managed-wire focused 块随后通过；Clippy all-targets/all-features 和 fmt 通过。首次工具解析、编译和旧断言失败日志保留，未做批量 xfail。此点仍不表示 K5/K6 完成，其余示例／Zephyr sample／benchmark／fuzz／测试和当前公开文档仍需逐个迁移，最终硬件确认尚未进行。

## 基础 C 教程 00–02

telemetry、calculator、device-info 均使用正式 WLC v2 endpoint 与一个应用 owner loop；两端 port/C storage 显式持有，TX snapshot、四并发请求、反向业务拒绝、bounded UTF-8/NUL response 及 close/drain 均真实执行。删去 deferred-server 源，说明长任务改走应用 job/status；ordinary profile 同时声明 send/direct，不再倚赖旧通用 send binding 或 retained mailbox。应用保存结果的 helper 只复制 typed 借用视图，没有关联表／计时副本。

默认纯 C 例程仍使用 checked-in 生成物；`WIRELINK_BUILD_WLC_EXAMPLES=ON` 显式启用需要本地 matching WLC 的教程，生成 helper 与安装后相同。GCC **15/15**，Clang ASan/UBSan 对应块验证。旧多业务／组合／UDP 与 Zephyr 例程仍在后续迁移中，不能把本点视为全部消费者已完成。

## 多业务 C 例程与应用 job

03 保留设备／算术等原业务类型与 ID，handler 改为 callback-scoped view；两端共用一个 v2 静态声明，保留双 RPC 角色，普通 100 通道 telemetry 纳入容量计算。原 SelfTest 同步短计算保留；删除 reply token／deferred 回调，显式新增例程应用 message 132–134 做 StartSelfTest(job-ID ack) 与普通完成消息。该例程协议扩展没有改 benchmark schema 或内核 wire。任务字段、due、BUSY、ordinary TX 背压均由应用循环负责，不加入框架 service。文档明确 lost status／重试／幂等由业务定义，取消不回滚。

业务／长任务／close 后 owned response 的实际 demo 与“四份应用文件新增 RPC、基础设施不变”的隔离复制构建测试，GCC **2/2**；Clang 块也 **2/2**，其中 demo 使用 ASan/UBSan，隔离扩展用 Clang 严格编译；扩展测试不修改原树。旧双进程测试 runner 迁为当前 demo runner；真实 UDP 合同仍由 SDK／adapter／UDP 示例承担。日志 `device-tutorial-*`。

## 组合应用：上传逻辑回到 owner loop

04 保留 imported schema、message ID／shape 及旧示例 phase/status 数值，普通 BulkCommand/BulkStatus、telemetry 与 Query/Reboot RPC 共用生成 kernel。删除旧 bulk sender/receiver、service rotation、peer reset、reliable ACK／response terminal hook。4096 B RAM 上传由应用处理有界 chunk、一次 BUSY、一次丢 status、有限重试、重复 chunk 校验、外层 CRC／一次 commit、关闭时一次 abort。重复 END 不再 commit；Reboot 仅短业务确认，不是 ACK／实际重启。没有把旧通用状态机重新塞进框架。

初次 demo 仍要求关闭中的 Query 成功，实际新合同返回 CLOSED（outcome=5/status=-33，tick=151）；改为仅在显式 closing 时接受 CLOSED，timeout／其他错误仍失败，日志保留。GCC demo 与真实 import-edge 重建 **2/2**；Clang ASan/UBSan demo 与独立依赖重建也 **2/2**。生成 helper 返回匹配 compiler property，并在 Zephyr 连接正式模块、检查对应 stream Kconfig；模块新增独立 packet loopback 选项，CRC primitives 可显式选择。Zephyr 组合 sample 不再手工编译另一套 core，native_sim／Cortex-M3／RISC-V／x86_64 四平台真实运行 **4/4**。日志 `composed-tutorial-*`、`twister-composed*`。没有刷写板卡或改已封存 K4 证据。

## Desktop UDP 例程

05 的 C client/server 改用 typed kernel、ordinary telemetry 和真实 packet UDP；common C++ 支持仅持有 socket/wait，不建线程／RPC 关联表。C 持有 port 原子对象和两份完整 RX unit，外层一次取 fresh instance，owner 显式提供 now，wait 合并 deadline 且重算耗时；close 停真实 I/O 后 quiescent 再释放。client 入队后销毁请求，借用回调内保存响应。

三个新 client 进程重用同一地址／端口，RPC/telemetry 实际成功；server 退出后 client 本地 timeout。含全部当前教程／UDP adapter 的 GCC **21/21**；Clang ASan/UBSan 对应双进程测试 **1/1**。不改变已封存 benchmark 或板卡。Zephyr UDP peer／validation 及其他旧消费者继续单独迁移，不能据此宣称其已完成。日志 `udp-tutorial-*`。

## Zephyr UDP peer 与生成 API regression

peer client/server 共用 desktop 的业务 schema／typed handler／v2 profile，C 显式持有 port/core/RX storage，网络与 fresh identity 在应用配置。owner helper 只合并 UDP I/O、core deadline、业务期限及 close/drain，没有 RPC 表或 adapter thread。native32/64 的四个 peer 配置 **仅构建通过**；首次 Twister 因从仓库而非 west workspace 启动被拒，修正目录后通过，失败日志保留。

udp_validation 移除旧 ARQ／CRC selector／replay／inspect-release 用例，改为新生成端点与两真实 UDP socket：每端四并发／第五次 WOULD_BLOCK、请求失效后快照、ordinary telemetry、close 后 owned response、业务拒绝、本地取消、UINT32_MAX 回绕 timeout、迟到响应不二次完成、close CLOSED、旧 handle 拒绝、有限 idle wait／锁存通知。native32／native64／Cortex-M3 合计 **9/9** 真实运行通过。独立 frozen-wire／完整 adapter socket/guard/stop 合同仍在原测试中；没有声称两块真实 Ethernet/Wi-Fi 板卡已互通。日志 `twister-udp-peer*`、`twister-udp-generated*`。

## 显式时钟与生成 RPC 所有权 sample

endpoint_clock 改为 owner 显式传 now，验证 idle 后首调用、短 handler／拒绝／未安装 handler 的本地 timeout，并断言 admission／codec／core 不取时钟。20,000 次 idle step 的 core clock reads 为零，仅报当前诊断 cycles。rpc_usability 固定四槽，验证第五次拒绝、callback 再提交、反向拒绝、UTF-8/NUL、完整 2023 B owned response、物理 TX 仍被借用时的回绕 timeout、真实 stop/drain 及新实例旧 handle 拒绝。原 one-slot knob 删除且明确失败；四槽大响应在 Cortex-M3 64 KiB 也实际跑通，没有缩 schema／server-only。

首次八配置失败来自两条 fixture 假设：未安装 handler 的 step 会返回 NOT_SUPPORTED；timeout 后停止仍借用 TX 的模型 driver 会发布 TX_ERROR，step 报 IO。按明确场景接收这些错误，仍断言本地唯一 terminal、未提前释放的物理 lease、最终 quiescent；未修改 core、未 xfail。修正后 native／M3／RISC-V／x86_64 **8/8** 运行通过。共用 loopback helper 的 fixture 身份改为每次初始化递增；生产仍必须提供真实 fresh identity。日志 `twister-clock-rpc*`。

## 两 owner、固定池及同步等待 sample

rpc_platform 使用同一 C kernel 与每端一个 owner task，两个单生产者实际 copy 到 peer RX unit 后才返回 SENT；满 RX 通过真实空间与锁存通知解锁。外层仅一项命令指针队列和 callback 返回／core pass 后的完成交接，不建 RPC matching 表或 deadline 引擎。固定池由应用显式 allocate/init/free；初始化失败回滚、池满、join 前不释放，热路径无分配。

peer 暂停时实际填满 RX、四调用接纳／第五次拒绝，恢复后全部完成；100 次同步 UTF-8/NUL 调用、业务拒绝、2023 B response、外层队列已过期不创建 core call、第三任务唤醒长等待 close、双 owner／producer join、池回收及 idle 不自旋均通过。native／RISC-V／x86_64 **3/3**；软件 cycles/stack 仅诊断，native 虚拟 cycles 为零如实保留，IRQ-off 有界 primitive 批次只在其他线程 join 后执行，core reads=0。不重现旧一槽／create facade。日志 `twister-rpc-platform*`。


## K5 收口：全部仓内消费者与正式构建

最终代码边界为根仓 `68db8235771f6d5d14e74007b79c48917351d3a7`、独立 WLC `7eabbf4dd767f968123917ba02431fa0a0ef184e`、独立 benchmark `cd8b9ca`。后续仅提交交付文档。逐文件[删除／替换清单](static-endpoint-kernel-retirement-20261031.csv)覆盖入口到代码边界的全部 **358 个删除文件**及 80 个修改的核心／公开接口／adapter／runtime／构建／generator 文件；区分原型迁移与旧引擎删除，包含入口与最终源码 SHA。新增文件和其余消费者修改可由 git 历史核对。已有测试、benchmark、fuzz、安装消费者 CSV 保留各自更具体的退役原因。没有整批 xfail 旧测试。

UART DMA/IRQ/poll、USB bulk、真实 UDP、serial/USB desktop adapter、两 owner 和固定池均已接到正式 packet port。USB CDC 因当前驱动无法提供真实物理 drain，明确不支持，不用软件队列清空伪装完成。raw USB echo 和 packet tools 在实际 driver stop/join 前持有 buffer／callback；typed serial 保留完整业务字段，只承诺本地发送。应用 object upload 明确处理 chunk/status、有限重试、重复与 commit，不把它重新命名成基础可靠链路。

framing、codec validation、API、mixed traffic、owner-pass、footprint 与统一 benchmark registry 已迁移到可区分的当前格式；旧 ARQ/Bulk/peer-cache/rx-backend 执行 workload 删除，历史解析器和 frozen vectors 保留并标明范围。mixed traffic 的模型时间、firmware cycles 和 IRQ-off 代理分别报告，不能互作硬件延迟。当前 parser 真实拒绝缺项、重复、错误窗口和旧格式；normalizer 不把新旧语义数据自动拼接比较。六个保留 fuzz 入口和有结构的 kernel/stream 输入通过。

中英文入门、RPC、async、values、adapter、storage、session、开发、协议及迁移清单已更新；教程代码与真正编译源逐字核对。站点 39 页构建和 902 条站内链接通过；文档检查只剩一条原有历史记录的段落长度提示。原有八份脏文档正文保留在历史折叠区，提交只纳入新的当前合同说明。原有 benchmark 222 份脏输入和 Astrial 685 份输入 hash 未变，没有修改其他工程的 vendor 副本。

CI 使用 matching WLC 源码 pin、固定 Zephyr 与显式平台补丁、当前安装消费者和独立 C/C++ ABI 检查；移除旧 bootstrap/runtime 选项并明确诊断。一次实际发现的 `-fshort-enums` 调用边界错误已修复：kernel outcome 与 packet submit 的公开 ABI 改为固定 `int32_t`，C/C++ 消费者真实通过。Linux 等价构建与检查已本地执行，macOS/MSVC、manylinux 容器和远程 CI 尚未实际执行，不以工作流文本代替运行证据。WLC pin 发布可用性须由维护者在后续发布流程确认，本轮没有 push 或发布。

## 最终源码确认

`/home/ww/codings/wirelink-static-kernel-final-20261031/final-input/` 归档实际源码，包括原有脏输入；`final-source/` 从归档提取后构建。B 的正式 host target 与 Zephyr 模块没有 prototype include／source 列表，不再手工编译另一份 core。实际四个核心 object 的代码及 literal 与 K4 相同；两个公开 header 因 ABI 修复改变，最终链接布局也改变，因此重新登记并完成 **三轮 A/B/C × observer off/on 的全部 18 份采集**，没有用 K4 原型结果替代最终产品。

全部 18 份的完整工作量、身份、环境、错误与字节账本在线通过，独立 raw replay 通过。主要配对收益为 off RPC RTT p50 **71.903%**、on MCU RPC active/call **69.555%**。MCU Sample 中位数下降 63.656%，但 A 跨 5% 稳定阈值，该项不可判定；15 项保护观察不可判定，无可判定的重复保护回退。libcsp 部分目标未达，详见最终结果；未再调整调频、布局或 workload 追数。

最终软件确认：core 五平台 **37/37 执行配置、198/198 case**，另 18 个静态平台过滤；sample 四平台 **21 个执行配置、25/25 case**，另两个仅构建配置；WLC **110 passed、0 failed、0 ignored**，clippy/fmt 通过。完整命令、之前各迁移点的 sanitizer／TSan／安装／SDK／fuzz 结果与失败修正均保留于证据根目录。最后将板卡交回 `static-kernel-final-20261031-B-o1`，smoke 合格，host I/O 关闭。K5 至此完成，K6 由最终结果与封存索引交付。
