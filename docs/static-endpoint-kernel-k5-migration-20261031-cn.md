# K5 默认产品迁移与删除记录

K4 的 18 份完整采集及独立重放通过，见 [结果与限制](static-endpoint-kernel-k4-result-20261031-cn.md)。开始 K5，尚未完成默认产品迁移或 K6。入口根仓 `cefa8aa`、WLC `af819f7`、benchmark `27d517b`，dirty 输入归档于 `/home/ww/codings/wirelink-static-kernel-final-20261031/entry/`。此前 K3/K4 索引不再追加。

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
